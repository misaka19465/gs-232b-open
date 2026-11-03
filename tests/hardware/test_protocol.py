# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
# SPDX-License-Identifier: GPL-3.0-only

"""Real UART / ETH / STA checks that never request movement or calibration."""

import time

import pytest


def test_identity_and_selected_model_agree(device):
    info = device.query("SYS INFO")
    rotor = device.query("ROTOR GET")
    assert info["board"] == "ESP32-C3-WROOM-02U-N4"
    assert isinstance(info["version"], str) and info["version"]
    assert 1 <= rotor["active_model_id"] <= 16
    for axis in ("azimuth", "elevation"):
        assert info["capabilities"][axis] == rotor["capabilities"][axis]


def test_unconnected_feedback_and_stopped_outputs_remain_explicit(device):
    before = device.query("MOTION STATUS")
    time.sleep(0.04)
    after = device.query("MOTION STATUS")
    assert after["captured_at_us"] > before["captured_at_us"]
    assert after["output_known"] is True and after["speed_permille"] == 0
    for old, axis in zip(before["axes"], after["axes"], strict=True):
        assert axis["direction"] == 0 and axis["target_valid"] is False
        assert axis["target_mdeg"] is None
        assert axis["sample"]["valid"] is True and axis["sample"]["error"] == "OK"
        assert axis["sample"]["acquired_at_us"] > old["sample"]["acquired_at_us"]
        assert 0 <= axis["sample"]["raw"] <= 4095
        assert 0 <= after["captured_at_us"] - axis["sample"]["acquired_at_us"] <= 100000
        if not axis["zero_valid"] or not axis["position_valid"]:
            assert axis["motion_ready"] is False
        if not axis["position_valid"]:
            assert axis["position_mdeg"] is None


def test_queries_do_not_take_ownership(device):
    device.query("SYS INFO")
    device.query("MOTION STATUS")
    owner = device.query("OWNER GET")
    assert owner["owner"] is None and owner["output_stopped"] is True


def test_split_command_and_crlf_recover_without_extra_reply(device):
    device.send(b" n")
    time.sleep(0.03)
    device.send(b" \r\nN\r")
    assert device.line() == b"=0000=0000\r\n"
    assert device.line() == b"=0000=0000\r\n"


@pytest.mark.parametrize("invalid", (b"N\x00N\r", b"N\t\r", b"N\n\r", b"@\r"))
def test_invalid_input_rejects_whole_line_and_next_query_succeeds(device, invalid):
    device.send(invalid + b"N\r")
    assert device.line() == b"?>\r\n"
    assert device.line() == b"=0000=0000\r\n"


def test_partial_input_timeout_discards_until_cr(device):
    device.send(b"N")
    assert device.line(timeout=3.5) == b"?>\r\n"
    device.send(b"N\rN\r")
    assert device.line() == b"=0000=0000\r\n"


def test_management_rejects_unknown_fields(device):
    result = device.extension("SYS INFO", {"unexpected": True})
    assert result["ok"] is False and result["error"] == "INVALID_ARGUMENT"
    assert device.query("SYS INFO")["entries"]["uart1"] is True


def test_network_query_never_discloses_passwords_or_tokens(device):
    state = device.query("NET GET")

    def check(value):
        if isinstance(value, dict):
            assert not ({"password", "token"} & value.keys())
            for child in value.values():
                check(child)
        elif isinstance(value, list):
            for child in value:
                check(child)

    check(state)


def test_repeated_queries_keep_adc_and_protocol_alive(device):
    initial = device.query("MOTION STATUS")["captured_at_us"]
    for _ in range(16):
        device.send(b"N\r")
        assert device.line() == b"=0000=0000\r\n"
    final = device.query("MOTION STATUS")
    assert final["captured_at_us"] > initial
    assert final["output_known"] is True and final["speed_permille"] == 0
