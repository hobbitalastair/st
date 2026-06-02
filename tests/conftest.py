import os
import subprocess

import pytest

from .helpers import REFERENCE_DIR, FAILED_DIR, STInstance, start_st, wait_for_st


def pytest_addoption(parser):
    parser.addoption(
        "--screenshot-threshold",
        default=0.01,
        type=float,
        help="RMS threshold for screenshot comparison (default 0.01)",
    )
    parser.addoption(
        "--st-path",
        default="./st",
        type=str,
        help="Path to the st binary (default ./st)",
    )


def pytest_configure(config):
    REFERENCE_DIR.mkdir(parents=True, exist_ok=True)
    FAILED_DIR.mkdir(parents=True, exist_ok=True)
    config.addinivalue_line("markers", "st_cmd(cmd): command to run inside st (e.g. 'sleep 3')")


def _make_cmd(request, cmd_args):
    if cmd_args is None:
        return None
    if isinstance(cmd_args, str):
        cmd_args = [cmd_args]
    return ["-e", "sh", "-c", *cmd_args]


@pytest.fixture(scope="function")
def st_instance(request, tmp_path):
    st_path = request.config.getoption("--st-path")
    xdg_runtime = str(tmp_path / "xdg")
    os.makedirs(xdg_runtime, mode=0o700, exist_ok=True)

    marker = request.node.get_closest_marker("st_cmd")
    cmd_args = marker.args[0] if marker else "cat"

    extra_args = _make_cmd(request, cmd_args)
    proc = start_st(xdg_runtime, st_path, extra_args)
    ready = wait_for_st(proc, xdg_runtime)

    if not ready:
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

    instance = STInstance(proc, xdg_runtime)

    yield instance

    instance.close()
