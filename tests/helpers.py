import os
import shutil
import struct
import subprocess
import time
import zlib
from pathlib import Path

TESTS_DIR = Path(__file__).parent
SCREENSHOTS_DIR = TESTS_DIR / "screenshots"
REFERENCE_DIR = SCREENSHOTS_DIR / "reference"
FAILED_DIR = SCREENSHOTS_DIR / "failed"

REQUIRED_BINS = ["cage", "grim", "wlrctl"]
TEST_FONT = "Liberation Mono:pixelsize=12:antialias=true:autohint=true"


def check_deps():
    missing = []
    for bin in REQUIRED_BINS:
        if not shutil.which(bin):
            missing.append(bin)
    if missing:
        raise RuntimeError(
            f"Missing required tools: {', '.join(missing)}. "
            f"Install them (e.g. pacman -S {' '.join(missing)})"
        )


class STInstance:
    def __init__(self, cage_proc, xdg_runtime):
        self.cage_proc = cage_proc
        self.xdg_runtime = xdg_runtime
        self.env = {
            "WAYLAND_DISPLAY": "wayland-0",
            "XDG_RUNTIME_DIR": xdg_runtime,
        }

    def type(self, text):
        warmup = subprocess.Popen(
            ["wlrctl", "keyboard", "type", ""],
            env=self.env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        try:
            # Cage only advertises a keyboard after the virtual keyboard exists.
            # Keep one alive long enough for st to bind wl_keyboard and get focus.
            time.sleep(0.5)
            result = subprocess.run(
                ["wlrctl", "keyboard", "type", text],
                env=self.env,
                capture_output=True,
                timeout=10,
            )
            if result.returncode != 0:
                stderr = result.stderr.decode(errors="replace").strip()
                raise RuntimeError(
                    f"wlrctl keyboard type failed (exit {result.returncode}): {stderr}"
                )
        finally:
            if warmup.poll() is None:
                warmup.terminate()
                try:
                    warmup.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    warmup.kill()
                    warmup.wait()

    def pointer_move(self, dx, dy):
        result = subprocess.run(
            ["wlrctl", "pointer", "move", str(dx), str(dy)],
            env=self.env,
            capture_output=True,
            timeout=10,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace").strip()
            raise RuntimeError(
                f"wlrctl pointer move failed (exit {result.returncode}): {stderr}"
            )

    def pointer_click(self, button="left"):
        result = subprocess.run(
            ["wlrctl", "pointer", "click", button],
            env=self.env,
            capture_output=True,
            timeout=10,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace").strip()
            raise RuntimeError(
                f"wlrctl pointer click failed (exit {result.returncode}): {stderr}"
            )

    def pointer_scroll(self, dy, dx=0):
        result = subprocess.run(
            ["wlrctl", "pointer", "scroll", str(dy), str(dx)],
            env=self.env,
            capture_output=True,
            timeout=10,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode(errors="replace").strip()
            raise RuntimeError(
                f"wlrctl pointer scroll failed (exit {result.returncode}): {stderr}"
            )

    def assert_running(self):
        if self.cage_proc.poll() is not None:
            stderr = self.cage_proc.stderr.read() if self.cage_proc.stderr else b""
            msg = f"cage exited with code {self.cage_proc.returncode}"
            if stderr:
                msg += f". stderr:\n{stderr.decode(errors='replace')}"
            raise AssertionError(msg)

    def screenshot(self, name):
        FAILED_DIR.mkdir(parents=True, exist_ok=True)
        path = FAILED_DIR / f"{name}.png"
        subprocess.run(
            ["grim", str(path)],
            env=self.env,
            capture_output=True,
            timeout=10,
        )
        return path

    def assert_screenshot(self, name, threshold=0.01):
        captured = self.screenshot(name)
        reference = REFERENCE_DIR / f"{name}.png"

        if not reference.exists():
            raise AssertionError(
                f"No reference screenshot at {reference}. "
                f"Captured output saved to {captured}. "
                f"Run `make update-screenshots` to promote it."
            )

        if not compare_images(captured, reference, captured, threshold):
            diff = captured.with_name(f"{captured.stem}.diff.png")
            raise AssertionError(
                f"Screenshot mismatch for '{name}'. "
                f"See {captured} for actual output, "
                f"{diff} for diff overlay, "
                f"{reference} for expected."
            )

    def close(self):
        if self.cage_proc.poll() is None:
            self.cage_proc.terminate()
            try:
                self.cage_proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.cage_proc.kill()
                self.cage_proc.wait()


def start_st(xdg_runtime, st_path, cmd_args=None):
    check_deps()
    st_path = Path(st_path)
    if not st_path.is_file():
        raise RuntimeError(f"st binary not found at {st_path.resolve()}")
    if not os.access(st_path, os.X_OK):
        raise RuntimeError(f"st binary at {st_path.resolve()} is not executable")
    FAILED_DIR.mkdir(parents=True, exist_ok=True)
    env = {
        **os.environ,
        "WAYLAND_DISPLAY": "wayland-0",
        "XDG_RUNTIME_DIR": xdg_runtime,
        "WLR_BACKENDS": "headless",
    }
    cage_args = ["cage", "--", str(st_path.resolve()), "-f", TEST_FONT]
    if cmd_args:
        cage_args.extend(cmd_args)
    proc = subprocess.Popen(
        cage_args,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )
    return proc


def wait_for_st(proc, xdg_runtime, timeout=5):
    socket_path = Path(xdg_runtime) / "wayland-0"
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return False
        if socket_path.is_socket():
            time.sleep(1)
            return True
        time.sleep(0.1)
    return False


def _read_png(path):
    with open(path, "rb") as f:
        data = f.read()
        if data[:8] != b"\x89PNG\r\n\x1a\n":
            raise ValueError("not a PNG file")
        pos = 8
        idat = bytearray()
        header = None
        while pos < len(data):
            length = struct.unpack(">I", data[pos : pos + 4])[0]
            chunk_type = data[pos + 4 : pos + 8]
            chunk_data = data[pos + 8 : pos + 8 + length]
            if chunk_type == b"IHDR":
                header = chunk_data
            elif chunk_type == b"IDAT":
                idat.extend(chunk_data)
            pos += 12 + length
        if header is None:
            raise ValueError("no IHDR chunk")
        width = struct.unpack(">I", header[:4])[0]
        height = struct.unpack(">I", header[4:8])[0]
        bit_depth = header[8]
        color_type = header[9]
        raw = zlib.decompress(bytes(idat))
        raw = _unfilter_png(raw, width, height, bit_depth, color_type)
        return width, height, bit_depth, color_type, raw


def _paeth(a, b, c):
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _unfilter_png(raw, width, height, bit_depth, color_type):
    if color_type == 2:
        channels = 3
    elif color_type == 6:
        channels = 4
    else:
        channels = 1

    bpp = channels * (bit_depth // 8)
    stride = width * bpp
    previous = bytearray(stride)
    out = bytearray()

    for y in range(height):
        row_start = y * (stride + 1)
        filter_type = raw[row_start]
        row = bytearray(raw[row_start + 1 : row_start + 1 + stride])

        for i in range(stride):
            left = row[i - bpp] if i >= bpp else 0
            up = previous[i]
            up_left = previous[i - bpp] if i >= bpp else 0

            if filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + up) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
            elif filter_type == 4:
                row[i] = (row[i] + _paeth(left, up, up_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter: {filter_type}")

        out.extend(b"\x00" + row)
        previous = row

    return bytes(out)


def _pixel_rms(a_raw, b_raw, a_bit_depth, a_color_type, width, height):
    if a_color_type == 2:
        channels = 3
    elif a_color_type == 6:
        channels = 4
    else:
        channels = 1

    bpp = channels * (a_bit_depth // 8)
    stride = 1 + width * bpp
    total = 0
    count = 0
    for y in range(height):
        row_off = y * stride + 1
        a_row = a_raw[row_off : row_off + width * bpp]
        b_row = b_raw[row_off : row_off + width * bpp]
        for i in range(0, len(a_row), bpp):
            for c in range(channels):
                a_val = a_row[i + c]
                b_val = b_row[i + c]
                d = a_val - b_val
                total += d * d
                count += 1
    if count == 0:
        return 0.0
    return ((total / count) ** 0.5) / 255


def _pixel_channels(raw, color_type, bit_depth, width, height):
    if color_type == 2:
        channels = 3
    elif color_type == 6:
        channels = 4
    else:
        channels = 1
    bpp = channels * (bit_depth // 8)
    stride = 1 + width * bpp
    pixels = []
    for y in range(height):
        off = y * stride + 1
        row = raw[off : off + width * bpp]
        for i in range(0, len(row), bpp):
            pixels.append(list(row[i : i + channels]))
    return pixels


def _png_chunk(chunk_type, data):
    return (
        struct.pack(">I", len(data))
        + chunk_type
        + data
        + struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
    )


def _write_rgb_png(path, width, height, pixels):
    rows = []
    for y in range(height):
        start = y * width
        row = pixels[start : start + width]
        rows.append(b"\x00" + b"".join(bytes(pixel) for pixel in row))

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(_png_chunk(b"IHDR", header))
        f.write(_png_chunk(b"IDAT", zlib.compress(b"".join(rows))))
        f.write(_png_chunk(b"IEND", b""))


def compare_images(captured, reference, diff_path, threshold=0.01):
    w_a, h_a, bd_a, ct_a, raw_a = _read_png(captured)
    w_b, h_b, bd_b, ct_b, raw_b = _read_png(reference)

    if (w_a, h_a) != (w_b, h_b):
        return False

    rms = _pixel_rms(raw_a, raw_b, bd_a, ct_a, w_a, h_a)
    if rms > threshold:
        diff_path = Path(diff_path)
        diff_png = diff_path.with_name(f"{diff_path.stem}.diff.png")
        pix_a = _pixel_channels(raw_a, ct_a, bd_a, w_a, h_a)
        pix_b = _pixel_channels(raw_b, ct_b, bd_b, w_b, h_b)
        diff_pixels = []
        for pa, pb in zip(pix_a, pix_b):
            c = min(3, len(pa), len(pb))
            r = min(abs(pa[0] - pb[0]) * 10, 255) if c > 0 else 0
            g = min(abs(pa[1 % c] - pb[1 % c]) * 10, 255) if c > 1 else 0
            b = min(abs(pa[2 % c] - pb[2 % c]) * 10, 255) if c > 2 else 0
            diff_pixels.append([r, g, b])
        _write_rgb_png(diff_png, w_a, h_a, diff_pixels)
        return False
    return True
