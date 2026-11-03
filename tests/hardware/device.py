# GS-232B Open - rotator controller firmware.
# Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
# SPDX-License-Identifier: GPL-3.0-only

"""Bounded protocol clients for tests against an already running controller."""

import json
import time

import serial
from websockets.sync.client import connect


class Device:
    def __init__(self, transport, timeout=5.0, baud=None):
        self.transport = transport
        self.timeout = timeout
        self.baud = baud
        self.pending = bytearray()

    @classmethod
    def uart(cls, path, baud=9600, timeout=5.0):
        port = serial.Serial(baudrate=baud, timeout=0.05, write_timeout=timeout)
        # NOTE: 在 open 前设置信号；原生 USB 不是控制口，也不能用默认 DTR/RTS 复位设备。
        port.dtr = port.rts = False
        port.port = path
        port.exclusive = True
        port.open()
        return cls(port, timeout, baud)

    @classmethod
    def websocket(cls, address, path="/ws/gs232b", timeout=5.0):
        device = cls(connect(f"ws://{address}{path}", open_timeout=timeout,
                             close_timeout=1, max_size=16384, proxy=None), timeout)
        try:
            assert device.line() == b"Connect OK\r\n"
        except BaseException:
            device.close()
            raise
        return device

    def close(self):
        self.transport.close()

    def send(self, payload):
        if self.baud is None:
            self.transport.send(payload)
        else:
            assert self.transport.write(payload) == len(payload)
            self.transport.flush()

    def read(self, size=None, *, timeout=None):
        budget = self.timeout if timeout is None else timeout
        if self.baud is not None:
            budget += (size or 2048) * 10 / self.baud
        deadline = time.monotonic() + budget
        while True:
            end = size if size is not None else self.pending.find(b"\r\n") + 2
            if (size is not None and len(self.pending) >= size) or (size is None and end > 1):
                result = bytes(self.pending[:end])
                del self.pending[:end]
                return result
            if len(self.pending) > 16384:
                raise AssertionError("Device response exceeds protocol bound")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                # NOTE: 不把原始回复放进异常，避免配网凭据进入 pytest/JUnit 日志。
                raise TimeoutError("Incomplete device response; check baud and USB2 port")
            chunk = (self.transport.read(self.transport.in_waiting or 1) if self.baud else
                     self.transport.recv(timeout=remaining))
            assert isinstance(chunk, bytes), "Protocol responses must use Binary frames"
            self.pending.extend(chunk)

    def line(self, *, timeout=None):
        return self.read(timeout=timeout)

    def extension(self, command, arguments=None):
        line = "!" + command
        if arguments is not None:
            line += " " + json.dumps(arguments, ensure_ascii=True, separators=(",", ":"))
        self.send(line.encode("ascii") + b"\r")
        while True:
            response = self.line()
            if response != b"Connect OK\r\n":
                break
        assert response.startswith(b"!") and response.endswith(b"\r\n")
        result = json.loads(response[1:-2])
        assert result.get("cmd") == command.upper()
        return result

    def query(self, command):
        result = self.extension(command)
        assert result.get("ok") is True, f"{command}: {result.get('error')}"
        return result["data"]
