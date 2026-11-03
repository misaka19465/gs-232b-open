# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
# SPDX-License-Identifier: GPL-3.0-only

"""Read back the saved Personal policy after an externally controlled restart."""

import pytest


def test_saved_personal_policy_is_ready(uart, pytestconfig):
    expected = pytestconfig.getoption("--device-wifi-security")
    if expected is None:
        pytest.skip("Policy check requires --device-wifi-security")
    state = uart.query("NET GET")
    wifi = state["active"]["wifi"]
    assert uart.query("SYS INFO")["schema"]["network"] >= 4
    assert state["active_revision"] > 0 and state["trial"] is None
    assert wifi["security"] == expected and wifi["enabled"] and wifi["password_set"]
    current = state["runtime"]["wifi"]
    assert current["state"] == "ready" and current["ipv4_valid"]
    assert current["ssid"] == wifi["ssid"]
    assert "password" not in wifi
