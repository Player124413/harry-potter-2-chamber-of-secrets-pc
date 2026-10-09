#!/usr/bin/env python3
"""Exercise the real SDL launcher window and verify its rendered smoke frame."""
from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: LauncherSmoke.py <hp2_game>")
    artifact_dir = Path(os.environ["HP2_ARTIFACT_DIR"])
    artifact_dir.mkdir(parents=True, exist_ok=True)
    log_path = artifact_dir / "launcher-smoke.log"
    image_path = artifact_dir / "launcher.bmp"
    environment = os.environ.copy()
    environment["HP2_LAUNCHER_SMOKE_FRAMES"] = "30"
    result = subprocess.run(
        [sys.argv[1]],
        cwd=Path(__file__).resolve().parents[1],
        env=environment,
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
    )
    log = result.stdout + result.stderr
    log_path.write_text(log, encoding="utf-8")
    failures: list[str] = []
    if result.returncode != 0:
        failures.append(f"launcher exited with code {result.returncode}")
    if "HP2Launcher: smoke complete" not in log:
        failures.append("launcher did not log its smoke completion marker")
    try:
        with image_path.open("rb") as image:
            header = image.read(26)
        if len(header) < 26 or header[:2] != b"BM":
            failures.append("launcher.bmp is missing or is not a BMP")
        else:
            width, height = struct.unpack_from("<ii", header, 18)
            if width < 960 or height < 640:
                failures.append(f"launcher.bmp dimensions are {width}x{height}, expected at least 960x640")
    except OSError:
        failures.append("launcher.bmp was not created")
    if failures:
        print("; ".join(failures), file=sys.stderr)
        print(f"launcher log: {log_path}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
