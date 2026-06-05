import os
import shlex
import subprocess
import sys
import time
from pathlib import Path
from urllib.parse import quote

import pytest

from .helpers import STInstance, start_st, wait_for_st


def _children(pid):
    children_path = Path(f"/proc/{pid}/task/{pid}/children")
    if not children_path.exists():
        return []
    return [int(p) for p in children_path.read_text().split()]


def _comm(pid):
    comm_path = Path(f"/proc/{pid}/comm")
    if not comm_path.exists():
        return ""
    return comm_path.read_text().strip()


def _wait_for_st_pid(cage_proc, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if cage_proc.poll() is not None:
            break
        queue = _children(cage_proc.pid)
        while queue:
            pid = queue.pop(0)
            if _comm(pid) == "st":
                return pid
            queue.extend(_children(pid))
        time.sleep(0.05)
    raise AssertionError("timed out waiting for st child process")


def _wait_for_cwd(pid, expected, timeout=5):
    deadline = time.monotonic() + timeout
    cwd = None
    while time.monotonic() < deadline:
        cwd = Path(f"/proc/{pid}/cwd").resolve()
        if cwd == expected:
            return
        time.sleep(0.05)
    raise AssertionError(f"st cwd did not become {expected}; last cwd was {cwd}")


def _start_st_with_cmd(request, tmp_path, cmd):
    st_path = request.config.getoption("--st-path")
    xdg_runtime = str(tmp_path / "xdg")
    os.makedirs(xdg_runtime, mode=0o700, exist_ok=True)

    proc = start_st(xdg_runtime, st_path, ["-e", "sh", "-c", cmd])
    if not wait_for_st(proc, xdg_runtime):
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        stderr = proc.stderr.read() if proc.stderr else b""
        msg = f"cage exited with code {proc.returncode}"
        if stderr:
            msg += f". stderr:\n{stderr.decode(errors='replace')}"
        pytest.fail(msg)

    return STInstance(proc, xdg_runtime)


def _python_write_osc(payload):
    code = (
        "import sys,time; "
        f"sys.stdout.write({payload!r}); "
        "sys.stdout.flush(); "
        "time.sleep(10)"
    )
    return f"{shlex.quote(sys.executable)} -c {shlex.quote(code)}"


def test_osc7_updates_process_cwd(request, tmp_path):
    target = tmp_path / "cwd with spaces"
    target.mkdir()
    uri_path = quote(str(target))
    cmd = _python_write_osc(f"\033]7;file://localhost{uri_path}\007")
    instance = _start_st_with_cmd(request, tmp_path, cmd)

    try:
        st_pid = _wait_for_st_pid(instance.cage_proc)
        _wait_for_cwd(st_pid, target.resolve())
    finally:
        instance.close()


def test_osc7_ignores_remote_hosts(request, tmp_path):
    target = tmp_path / "remote-cwd"
    target.mkdir()
    uri_path = quote(str(target))
    cmd = _python_write_osc(f"\033]7;file://example.invalid{uri_path}\007")
    initial_cwd = Path.cwd().resolve()
    instance = _start_st_with_cmd(request, tmp_path, cmd)

    try:
        st_pid = _wait_for_st_pid(instance.cage_proc)
        time.sleep(0.5)
        assert Path(f"/proc/{st_pid}/cwd").resolve() == initial_cwd
    finally:
        instance.close()


def test_osc7_rejects_invalid_percent_escape(request, tmp_path):
    cmd = _python_write_osc("\033]7;file://localhost/tmp/%zz\007")
    initial_cwd = Path.cwd().resolve()
    instance = _start_st_with_cmd(request, tmp_path, cmd)

    try:
        st_pid = _wait_for_st_pid(instance.cage_proc)
        time.sleep(0.5)
        assert Path(f"/proc/{st_pid}/cwd").resolve() == initial_cwd
    finally:
        instance.close()
