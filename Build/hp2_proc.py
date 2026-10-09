"""Start and terminate subprocesses together with their child processes."""

from __future__ import annotations

import os
import signal
import subprocess


def popen_group(argv, **kw):
    """Launch *argv* in a new process group/session."""
    if os.name == "nt":
        kw["creationflags"] = kw.get("creationflags", 0) | subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kw["start_new_session"] = True
    return subprocess.Popen(argv, **kw)


def kill_group(proc) -> None:
    """Forcefully terminate *proc* and any descendants it started."""
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/T", "/F", "/PID", str(proc.pid)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
    else:
        os.killpg(proc.pid, signal.SIGKILL)
