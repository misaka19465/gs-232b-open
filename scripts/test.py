#!/usr/bin/env python3
"""Build the firmware and run the independent native smoke test."""

import argparse
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "esp32-c3-wroom-02u-n4"


def run(*command: str) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def check_target_console() -> None:
    """Verify both generated images keep the UART console off GPIO20."""
    build = ROOT / ".pio" / "build" / ENVIRONMENT
    for config in (build / "config/sdkconfig.json", build / "bootloader/config/sdkconfig.json"):
        settings = json.loads(config.read_text())
        if not (
            settings.get("ESP_CONSOLE_USB_SERIAL_JTAG") is True
            and settings.get("ESP_CONSOLE_SECONDARY_NONE") is True
            and not settings.get("ESP_CONSOLE_UART")
            and settings.get("ESP_CONSOLE_UART_NUM") == -1
        ):
            raise RuntimeError(f"{config}: console must use USB Serial/JTAG without UART")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host-only", action="store_true", help="run without PlatformIO or ESP-IDF"
    )
    args = parser.parse_args()

    try:
        if not args.host_only:
            run("pio", "run", "-e", ENVIRONMENT)
            check_target_console()
        run("cmake", "-S", "tests/host", "-B", "build/host")
        run("cmake", "--build", "build/host")
        run("ctest", "--test-dir", "build/host", "--output-on-failure", "--no-tests=error")
    except FileNotFoundError as error:
        parser.exit(1, f"Missing build tool: {error.filename}\n")
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (RuntimeError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
