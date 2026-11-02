#!/usr/bin/env python3
"""Build the complete firmware and run native module and startup integration tests."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[1]
ENVIRONMENT = "esp32-c3-wroom-02u-n4"
CAPTURE_ENVIRONMENT = "uart-capture-api-check"
ETHERNET_ENVIRONMENT = "ethernet-api-check"
WIFI_STA_ENVIRONMENT = "wifi-sta-api-check"
WEBSOCKET_ENVIRONMENT = "websocket-api-check"


def run(*command: str, env: dict[str, str] | None = None) -> None:
    print("+ " + " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True, env=env)


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


def check_application_link() -> None:
    """Require the real composition entry and every platform path in the default ELF."""
    build = ROOT / ".pio" / "build" / ENVIRONMENT
    description = json.loads((build / "project_description.json").read_text())
    compiler = Path(description["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols = subprocess.check_output(
        [str(nm), "--defined-only", str(build / "firmware.elf")], text=True
    )
    defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "app_main", "gs232b_app_start", "gs232b_app_step", "gs232b_rotor_config",
        "gs232b_rotor_load", "gs232b_rotor_save",
        "gs232b_dispatcher_init", "gs232b_motion_init", "gs232b_esp_shared_spi_open",
        "gs232b_esp_store_worker_open", "gs232b_esp_store_worker_port", "nvs_commit",
        "gs232b_esp_adc_capture", "esp_timer_start_periodic", "gs232b_esp_uart_service",
        "gs232b_esp_ethernet_open_after_reset", "gs232b_esp_wifi_sta_service",
        "gs232b_esp_ws_open", "gs232b_esp_ws_service",
    }
    if missing := required - defined:
        raise RuntimeError(f"Application integration link missing symbols: {sorted(missing)}")
    print("Verified default firmware composition + tasks + all platform paths", flush=True)


def check_ethernet_link(environment: str = ETHERNET_ENVIRONMENT) -> None:
    """Verify real W5500, esp-netif and lwIP are retained in the target ELF."""
    build = ROOT / ".pio" / "build" / environment
    description = json.loads((build / "project_description.json").read_text())
    if description["target"] != "esp32c3" or description["git_revision"] != "6.1.0":
        raise RuntimeError("Revalidate Ethernet events for a different target or ESP-IDF version")
    settings = json.loads((build / "config/sdkconfig.json").read_text())
    if settings.get("ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF") is not True:
        raise RuntimeError("Ethernet requires DNS per interface; regenerate stale sdkconfig")
    compiler = Path(description["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols = subprocess.check_output(
        [str(nm), "--defined-only", str(build / "firmware.elf")], text=True
    )
    defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "gs232b_esp_ethernet_open", "gs232b_esp_ethernet_recover",
        "gs232b_esp_ethernet_configure", "gs232b_esp_ethernet_service",
        "gs232b_esp_ethernet_snapshot", "gs232b_esp_ethernet_close",
        "gs232b_esp_w5500_create", "gs232b_w5500_reset_tick",
        "gs232b_output_manager_set_w5500_reset", "gs232b_shared_spi_w5500_transfer",
        "esp_eth_mac_new_w5500", "esp_eth_phy_new_w5500", "esp_eth_driver_install",
        "esp_eth_new_netif_glue", "esp_netif_attach", "esp_netif_dhcpc_start",
        "esp_netif_dhcpc_stop", "esp_netif_set_ip_info", "esp_netif_set_dns_info",
        "esp_read_mac", "gpio_install_isr_service", "netif_add", "dhcp_start", "ethernet_input",
    }
    if missing := required - defined:
        raise RuntimeError(f"Ethernet API link check missing symbols: {sorted(missing)}")
    print("Verified ESP32-C3 / ESP-IDF 6.1.0 W5500 + esp-netif + lwIP API link", flush=True)


def check_wifi_sta_link() -> None:
    """Require real STA and Ethernet paths together in the locked target ELF."""
    check_ethernet_link(WIFI_STA_ENVIRONMENT)
    build = ROOT / ".pio" / "build" / WIFI_STA_ENVIRONMENT
    description = json.loads((build / "project_description.json").read_text())
    compiler = Path(description["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols = subprocess.check_output(
        [str(nm), "--defined-only", str(build / "firmware.elf")], text=True
    )
    defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "gs232b_esp_wifi_sta_open", "gs232b_esp_wifi_sta_configure",
        "gs232b_esp_wifi_sta_set_enabled", "gs232b_esp_wifi_sta_service",
        "gs232b_esp_wifi_sta_snapshot", "gs232b_esp_wifi_sta_close",
        "gs232b_esp_wifi_sta_ap_start", "gs232b_esp_wifi_sta_ap_stop",
        "gs232b_esp_wifi_sta_ap_scan",
        "esp_wifi_init", "esp_wifi_deinit", "esp_wifi_set_storage", "esp_wifi_set_mode",
        "esp_wifi_set_config", "esp_wifi_get_mac", "esp_wifi_start", "esp_wifi_connect",
        "esp_wifi_stop", "esp_netif_attach_wifi_station", "esp_wifi_set_default_wifi_sta_handlers",
        "esp_netif_attach_wifi_ap", "esp_wifi_set_default_wifi_ap_handlers",
        "esp_wifi_scan_start", "esp_wifi_scan_get_ap_num", "esp_wifi_scan_get_ap_record",
        "esp_wifi_clear_ap_list",
        "esp_wifi_clear_default_wifi_driver_and_handlers", "netif_add", "dhcp_start",
    }
    if missing := required - defined:
        raise RuntimeError(f"Wi-Fi STA API link check missing symbols: {sorted(missing)}")
    print("Verified ESP32-C3 / ESP-IDF 6.1.0 Wi-Fi STA + Ethernet API link", flush=True)


def check_websocket_link() -> None:
    """Verify locked HTTPD framing and handshake capabilities in the real target ELF."""
    check_ethernet_link(WEBSOCKET_ENVIRONMENT)
    build = ROOT / ".pio" / "build" / WEBSOCKET_ENVIRONMENT
    settings = json.loads((build / "config/sdkconfig.json").read_text())
    required_settings = (
        "HTTPD_WS_SUPPORT", "HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT",
        "HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT", "COMPILER_OPTIMIZATION_SIZE",
    )
    if not all(settings.get(key) is True for key in required_settings) or settings.get(
        "HTTPD_QUEUE_WORK_BLOCKING"
    ):
        raise RuntimeError("WebSocket requires WS pre/post hooks, size optimization and a nonblocking work queue; regenerate stale sdkconfig")
    description = json.loads((build / "project_description.json").read_text())
    compiler = Path(description["c_compiler"])
    nm = compiler.with_name(compiler.name.removesuffix("gcc") + "nm")
    symbols = subprocess.check_output(
        [str(nm), "--defined-only", str(build / "firmware.elf")], text=True
    )
    defined = {line.split()[-1] for line in symbols.splitlines() if line.split()}
    required = {
        "gs232b_esp_ws_open", "gs232b_esp_ws_io", "gs232b_esp_ws_interface",
        "gs232b_esp_ws_service", "gs232b_esp_ws_close", "gs232b_ws_pre_handshake",
        "gs232b_ws_post_handshake", "gs232b_ws_receive", "gs232b_ws_pump",
        "gs232b_dispatcher_management_session_open", "gs232b_management_register",
        "gs232b_management_status", "gs232b_admin_register", "gs232b_management_digest",
        "psa_generate_random", "psa_hash_compute",
        "gs232b_provisioning_page", "_binary_provisioning_html_gz_start",
        "_binary_provisioning_html_gz_end", "httpd_resp_send",
        "gs232b_provisioning_scan_page",
        "httpd_start", "httpd_stop", "httpd_register_uri_handler", "httpd_queue_work",
        "httpd_ws_recv_frame_part", "httpd_ws_send_frame_async",
        "httpd_sess_set_send_override", "lwip_shutdown",
        "lwip_setsockopt",
        "gs232b_esp_wifi_sta_open", "gs232b_esp_ethernet_open",
    }
    if missing := required - defined:
        raise RuntimeError(f"WebSocket API link check missing symbols: {sorted(missing)}")
    print("Verified ESP32-C3 / ESP-IDF 6.1.0 WebSocket + STA + Ethernet API link", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--host-only", action="store_true", help="run without PlatformIO or ESP-IDF"
    )
    parser.add_argument(
        "--sanitizers", action="store_true", help="run host tests with ASan, LSan and UBSan"
    )
    args = parser.parse_args()

    try:
        if not args.host_only:
            run("pio", "run", "-e", ENVIRONMENT, "-e", CAPTURE_ENVIRONMENT,
                "-e", ETHERNET_ENVIRONMENT, "-e", WIFI_STA_ENVIRONMENT,
                "-e", WEBSOCKET_ENVIRONMENT)
            check_target_console(ENVIRONMENT)
            check_target_console(CAPTURE_ENVIRONMENT)
            check_target_console(ETHERNET_ENVIRONMENT)
            check_target_console(WIFI_STA_ENVIRONMENT)
            check_target_console(WEBSOCKET_ENVIRONMENT)
            check_application_link()
            check_capture_link()
            check_ethernet_link()
            check_wifi_sta_link()
            check_websocket_link()
            run("python3", "scripts/resource_budget.py")
        build = "build/host-sanitized" if args.sanitizers else "build/host"
        configure = ["cmake", "-S", "tests/host", "-B", build,
                     f"-DGS232B_SANITIZERS={'ON' if args.sanitizers else 'OFF'}"]
        if args.sanitizers and shutil.which("clang"):
            configure.append(f"-DCMAKE_C_COMPILER={shutil.which('clang')}")
        run(*configure)
        run("cmake", "--build", build, "--parallel", str(min(os.cpu_count() or 1, 8)))
        test_env = os.environ.copy()
        if args.sanitizers:
            test_env["ASAN_OPTIONS"] = "detect_leaks=1:halt_on_error=1"
            test_env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        run("ctest", "--test-dir", build, "--output-on-failure", "--no-tests=error",
            "--output-junit", "results.xml", env=test_env)
    except FileNotFoundError as error:
        parser.exit(1, f"Missing build tool: {error.filename}\n")
    except subprocess.CalledProcessError as error:
        return error.returncode
    except (RuntimeError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
