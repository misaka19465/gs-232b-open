#!/usr/bin/env python3
"""Build the firmware and run the independent native smoke test."""

import argparse
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "esp32-c3-wroom-02u-n4"


def run(*command: str) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host-only", action="store_true", help="run without PlatformIO or ESP-IDF"
    )
    args = parser.parse_args()

    try:
        if not args.host_only:
            run("pio", "run", "-e", ENVIRONMENT)
        run("cmake", "-S", "tests/host", "-B", "build/host")
        run("cmake", "--build", "build/host")
        run("ctest", "--test-dir", "build/host", "--output-on-failure", "--no-tests=error")
    except FileNotFoundError as error:
        parser.exit(1, f"Missing build tool: {error.filename}\n")
    except subprocess.CalledProcessError as error:
        return error.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
