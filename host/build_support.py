"""Shared local build process and artifact checksums."""

import hashlib
import os
from pathlib import Path
import signal
import subprocess

from .errors import HostError

MIN_BUILD_FREE = 64 * 1024 * 1024


def _sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _run_build(command, env, log, cwd, *, stage="compile", timeout=None):
    """No shell interpolation; cancellation terminates and joins the compiler."""
    with log.open("wb") as output:
        process_options = ({"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP}
                           if os.name == "nt" else {"start_new_session": True})
        process = subprocess.Popen(command, env=env, cwd=cwd, stdout=output,
                                   stderr=subprocess.STDOUT, **process_options)
        try:
            result = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                try:
                    if os.name == "nt":
                        process.send_signal(signal.CTRL_BREAK_EVENT)
                    else:
                        os.killpg(process.pid, signal.SIGTERM)
                except (OSError, ProcessLookupError):
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    if os.name == "nt":
                        subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                                       capture_output=True, check=False)
                    else:
                        os.killpg(process.pid, signal.SIGKILL)
                    process.kill()
                    process.wait()
            raise
    if result:
        code = "compile_failed" if stage == "compile" else "guest_execution_failed"
        raise HostError(code, "Compilation/execution failed; inspect the retained log", stage=stage,
                        details={"exit_code": result, "log": str(log)})
