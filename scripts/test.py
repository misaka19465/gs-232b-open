#!/usr/bin/env python3
"""Build the firmware and run the independent native smoke test."""

import argparse
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "esp32-c3-wroom-02u-n4"
CAPTURE_ENVIRONMENT = "uart-capture-api-check"


def run(*command: str) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def check_target_console(environment: str) -> None:
    """Verify application and bootloader logs use native USB exclusively."""
    build = ROOT / ".pio" / "build" / environment
    for config in (build / "config/sdkconfig.json", build / "bootloader/config/sdkconfig.json"):
        settings = json.loads(config.read_text())
        if not (
            settings.get("ESP_CONSOLE_USB_SERIAL_JTAG") is True
            and settings.get("ESP_CONSOLE_SECONDARY_NONE") is True
            and not settings.get("ESP_CONSOLE_UART")
            and not settings.get("ESP_CONSOLE_UART_DEFAULT")
            and not settings.get("ESP_CONSOLE_UART_CUSTOM")
            and settings.get("ESP_CONSOLE_UART_NUM") == -1
        ):
            raise RuntimeError(f"{config}: console must use USB Serial/JTAG without UART")


def check_capture_link() -> None:
    """Require the locked C3 SDK and real capture/UART/NVS symbols in the linked ELF."""
    build = ROOT / ".pio" / "build" / CAPTURE_ENVIRONMENT
    description = json.loads((build / "project_description.json").read_text())
    if description["target"] != "esp32c3" or description["git_revision"] != "6.1.0":
        raise RuntimeError("Revalidate RX capture for a different target or ESP-IDF version")
    compiler = Path(description["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols = subprocess.check_output(
        [str(nm), "--defined-only", str(build / "firmware.elf")], text=True
    )
    defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "gs232b_esp_uart_open", "gs232b_esp_uart_boot_service",
        "gs232b_uart_capture_open", "gs232b_uart_capture_service",
        "rmt_new_rx_channel", "rmt_rx_register_event_callbacks", "rmt_enable",
        "rmt_receive", "rmt_disable", "rmt_del_channel", "uart_set_baudrate", "gpio_get_level",
        "gs232b_uart_baud_commit", "gs232b_nvs_store_port", "nvs_commit",
    }
    if missing := required - defined:
        raise RuntimeError(f"Capture API link check missing symbols: {sorted(missing)}")
    print("Verified ESP32-C3 / ESP-IDF 6.1.0 UART1 RX capture API link", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host-only", action="store_true", help="run without PlatformIO or ESP-IDF"
    )
    args = parser.parse_args()

    try:
        if not args.host_only:
            run("pio", "run", "-e", ENVIRONMENT, "-e", CAPTURE_ENVIRONMENT)
            check_target_console(ENVIRONMENT)
            check_target_console(CAPTURE_ENVIRONMENT)
            check_capture_link()
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
