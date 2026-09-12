#!/usr/bin/env python3
"""Run the portable Vroom checks without Unreal or repository-local binaries.

From any working directory:
  python3 /path/to/vroom-unreal/Tools/test_portable.py
  python3 Tools/test_portable.py --log Saved/Validation/v022/portable-tests.log

Requires Python 3 and a C++17 compiler (clang++ by default, or CXX/--cxx).
"""

import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
CPP_TESTS = (
    "drive_model", "contact_model", "elevation_model", "track_preview",
    "frame_timing", "overtake",
)
PYTHON_TESTS = (
    "track_elevation", "nvidia_installer", "analyze_performance",
)
CPP_FLAGS = ("-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"),
                        help="C++ compiler command, optionally including flags")
    parser.add_argument("--log", type=Path,
                        default=Path("Saved/Validation/portable-tests.log"),
                        help="log path; relative paths resolve against the repository")
    args = parser.parse_args()
    compiler = shlex.split(args.cxx)
    if not compiler:
        parser.error("--cxx must name a compiler")
    log_path = args.log if args.log.is_absolute() else ROOT / args.log
    log_path.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    outcomes = []

    with log_path.open("w", encoding="utf-8") as log:
        def emit(message):
            print(message, flush=True)
            log.write(message + "\n")
            log.flush()

        def run(command):
            emit("$ " + shlex.join(str(part) for part in command))
            try:
                result = subprocess.run(command, cwd=ROOT, env=environment,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=120, check=False)
            except (OSError, subprocess.TimeoutExpired) as error:
                emit(str(error))
                return False
            if result.stdout:
                emit(result.stdout.rstrip())
            emit(f"Exit: {result.returncode}")
            return result.returncode == 0

        emit("Vroom portable checks")
        emit(f"UTC: {datetime.now(timezone.utc).isoformat()}")
        emit(f"Repository: {ROOT}")
        emit(f"Python: {sys.version.splitlines()[0]}")
        with tempfile.TemporaryDirectory(prefix="vroom-portable-") as temporary:
            for name in CPP_TESTS:
                binary = Path(temporary) / name
                compiled = run([*compiler, *CPP_FLAGS, "-I", str(ROOT / "Source/Vroom/Public"),
                                str(ROOT / f"Tests/{name}_test.cpp"), "-o", str(binary)])
                passed = compiled and run([str(binary)])
                outcomes.append((f"C++ {name}", passed))
            for name in PYTHON_TESTS:
                passed = run([sys.executable, str(ROOT / f"Tests/{name}_test.py"), "-v"])
                outcomes.append((f"Python {name}", passed))
            passed = run([sys.executable, str(ROOT / "Tools/bootstrap_car_assets.py"), "--audit"])
            outcomes.append(("Car geometry audit", passed))

        emit("\nResults:")
        for name, passed in outcomes:
            emit(f"{'PASS' if passed else 'FAIL'}: {name}")
        failed = sum(not passed for _, passed in outcomes)
        emit(f"{len(outcomes) - failed}/{len(outcomes)} checks passed.")
        emit(f"Log: {log_path}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
