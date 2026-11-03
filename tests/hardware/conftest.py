# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
# SPDX-License-Identifier: GPL-3.0-only

"""Opt-in serial and LAN fixtures; never flash, reset, or configure the device."""

import time

import pytest

from device import Device


def pytest_addoption(parser):
    group = parser.getgroup("hardware")
    group.addoption("--device-port", help="USB2 / CH340 serial port; required to run hardware tests")
    group.addoption("--device-baud", type=int, default=9600, choices=(1200, 2400, 4800, 9600))
    group.addoption("--device-timeout", type=float, default=5.0)
    group.addoption("--device-network", action="store_true", help="Require ETH and STA DHCP and test both LAN interfaces")


@pytest.fixture(scope="session")
def uart(pytestconfig):
    path = pytestconfig.getoption("--device-port")
    if not path:
        pytest.skip("Hardware tests require --device-port")
    timeout = pytestconfig.getoption("--device-timeout")
    if timeout <= 0:
        raise pytest.UsageError("--device-timeout must be positive")
    device = Device.uart(path, pytestconfig.getoption("--device-baud"), timeout)
    try:
        # NOTE: 固件可能仍处于 15 秒匹配窗口；只发送完整只读请求，不改变已保存速率。
        deadline = time.monotonic() + 22
        while True:
            try:
                device.query("SYS INFO")
                break
            except TimeoutError:
                if time.monotonic() >= deadline:
                    pytest.exit("Device did not become ready on USB2", returncode=1)
        owner = device.query("OWNER GET")
        assert owner["owner"] is None and owner["output_stopped"] is True
        assert owner["sequence"]["point_count"] == 0
        yield device
    finally:
        device.close()


@pytest.fixture(scope="session")
def network(uart, pytestconfig):
    if not pytestconfig.getoption("--device-network"):
        pytest.skip("LAN checks require --device-network")
    state = uart.query("NET GET")["runtime"]
    addresses = {}
    for interface in ("eth", "wifi"):
        current = state[interface]
        assert current["state"] == "ready" and current["ipv4_valid"], f"{interface} has no running IPv4"
        addresses[interface] = current["address"]
    assert addresses["eth"] != addresses["wifi"]
    return addresses


@pytest.fixture(params=("uart", "eth", "wifi"))
def device(request, uart, pytestconfig):
    if request.param == "uart":
        yield uart
        return
    address = request.getfixturevalue("network")[request.param]
    connection = Device.websocket(address, timeout=pytestconfig.getoption("--device-timeout"))
    try:
        yield connection
    finally:
        connection.close()
