"""Minimal stub of the MicroPython `pyb` module for unit tests."""
from __future__ import annotations

import time


class SDCard:
    def __init__(self, *args, **kwargs):
        self._powered = False

    def present(self):
        return False

    def power(self, value):
        self._powered = bool(value)


class LED:
    def __init__(self, *args, **kwargs):
        self.state = False

    def on(self):
        self.state = True

    def off(self):
        self.state = False


class UART:
    def __init__(self, *args, **kwargs):
        self.buffer = bytearray()

    def read(self, *args, **kwargs):
        data = bytes(self.buffer)
        self.buffer.clear()
        return data

    def write(self, data):
        return len(data)


class USB_VCP:
    RTS = 0x01
    CTS = 0x02

    def __init__(self, *args, **kwargs):
        pass

    def init(self, *args, **kwargs):
        pass

    def any(self):
        return 0

    def read(self, *args, **kwargs):
        return b""

    def write(self, data):
        return len(data)

    def close(self):
        pass


def usb_mode(*args, **kwargs):
    pass


def hard_reset():
    pass


def millis():
    return int(time.time() * 1000)


def elapsed_millis(start):
    return millis() - start


def delay(ms):
    time.sleep(ms / 1000.0)


class _PinBoard:
    class _USBVBus:
        @staticmethod
        def value():
            return 1

    USB_VBUS = _USBVBus()


class Pin:
    OUT = 0
    IN = 1

    board = _PinBoard()

    def __init__(self, *args, **kwargs):
        pass


class Flash:
    def __init__(self, *args, **kwargs):
        pass

    def ioctl(self, *args, **kwargs):
        return 0

    def readblocks(self, *args, **kwargs):
        pass

    def writeblocks(self, *args, **kwargs):
        pass


class I2C:
    def __init__(self, *args, **kwargs):
        pass


__all__ = [
    "SDCard",
    "LED",
    "UART",
    "USB_VCP",
    "Pin",
    "Flash",
    "I2C",
    "usb_mode",
    "hard_reset",
    "millis",
    "elapsed_millis",
    "delay",
]
