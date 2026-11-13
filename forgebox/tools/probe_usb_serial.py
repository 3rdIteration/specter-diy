"""Test the exact async-hwi SpecterDIY discovery exchange on a serial port."""
import argparse
import re
from time import monotonic

import serial
from serial.tools.list_ports import comports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="serial port, e.g. COM5")
    args = parser.parse_args()
    ports = [p.device for p in comports() if (p.vid, p.pid) == (0xf055, 0x9802)]
    if args.port is None and len(ports) != 1:
        parser.error(f"expected one Specter serial port, found {ports}; specify a port")
    port = args.port or ports[0]
    with serial.Serial(port, 9600, timeout=2, write_timeout=2) as device:
        device.reset_input_buffer()
        for attempt in (1, 2):
            start = monotonic()
            device.write(b"\r\n\r\nfingerprint\r\n")
            ack, reply = device.readline(), device.readline()
            elapsed = (monotonic() - start) * 1000
            print(f"{port} request {attempt}: {elapsed:.0f} ms, ACK={ack!r}, reply={reply!r}")
            if ack.rstrip(b"\r\n") != b"ACK" or not re.fullmatch(rb"[0-9a-fA-F]{8}\r?\n", reply):
                raise SystemExit("Specter discovery exchange failed")
            if attempt == 1 and elapsed >= 500:
                print("First response exceeded the 500 ms deadline in current Liana source")
    print("Both Specter discovery exchanges passed")


if __name__ == "__main__":
    main()
