"""
Simple helper functions to get reader and connection
"""
import uscard as sc
from pyb import Pin

reader = None
conn = None


def get_reader():
    global reader
    if reader is not None:
        return reader

    try:
        from platform import is_esp32p4
    except ImportError:
        is_esp32p4 = False

    if is_esp32p4:
        # ESP32-P4 with SEC1210 CCID bridge over UART.
        # The SEC1210 handles ISO 7816 CLK, I/O, RST, and voltage internally.
        # ioPin/clkPin are repurposed as UART TX/RX to the bridge IC.
        # rstPin and pwrPin are None — managed by the SEC1210.
        reader = sc.Reader(
            name="Specter card reader",
            ifaceId=1,
            ioPin=None,
            clkPin=None,
            rstPin=None,
            presPin=None,
            pwrPin=None,
        )
    else:
        # STM32 direct ISO 7816 mode via USART smartcard peripheral
        reader = sc.Reader(
            name="Specter card reader",
            ifaceId=2,
            ioPin=Pin.cpu.A2,
            clkPin=Pin.cpu.A4,
            rstPin=Pin.cpu.G10,
            presPin=Pin.cpu.C2,
            pwrPin=Pin.cpu.C5,
        )
    return reader


def get_connection():
    global conn
    if conn is not None:
        return conn
    reader = get_reader()
    conn = reader.createConnection()
    return conn


def encode(data):
    return bytes([len(data)]) + data
