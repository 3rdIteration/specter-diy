"""Read camera, hardware, or secure-element pairing diagnostics over USB."""
import argparse
import json
import struct
import sys
import time

VID = 0x1209
PID = 0x0001
PRODUCT = "ForgeBox QR Dbg"
REPORT_ID = 1
REPORT_SIZE = 32
CAMERA_PHASES = (
    "idle", "allocating buffers", "vendor decoder initialization",
    "camera initialized", "waiting for a frame", "decoding a frame",
    "decode returned",
)
UI_PHASES = ("idle", "opening scanner", "opening preview", "scan loop")
BOARD_VERSIONS = ("EVT0", "EVT1", "DVT1", "DVT2", "v3.0", "v3.1", "unknown")
DS_BLOCKS = tuple(range(23)) + (31, 32, 33, 34)
PAIRING_RESULTS = (
    "not run", "authenticated", "no device reply", "configuration read failed",
    "unexpected chip policy", "OTP material unavailable", "random source failed",
    "identity read failed", "encrypted read failed", "challenge command failed",
    "invalid authentication reply", "authentication did not match",
    "transport key GenDig failed", "independent reads disagree",
)
BLANK_STATES = (None, "all zero", "all ff", "contains data")
PAGE5_TEST_RESULTS = (
    "not run", "blank; awaiting write confirmation", "test marker present; awaiting restore confirmation",
    "other data; writes disabled", "marker write verified", "zero restore verified",
    "block policy unavailable or unexpected", "authenticated page read failed",
    "page unchanged after attempted write", "authenticated readback failed",
    "unexpected page contents; writes disabled until inspected", "random source failed",
)
PIN_TEST_RESULTS = (
    "not run", "page 5 blank; awaiting test confirmation",
    "exhausted counter persisted; awaiting verify and clear", "interrupted test; awaiting cleanup",
    "PIN-only decryption failed, valid unlock passed, and SPI replay did not bypass three-attempt lockout",
    "lockout verified after restart and page 5 restored to all zero",
    "interrupted test cleared and page 5 restored to all zero",
    "other page 5 data; writes disabled", "test failed or page policy/readback could not be verified",
)
RESET_TEST_RESULTS = (
    "not run", "pages 5 and 11 blank; awaiting confirmation",
    "automatic wipe and re-enrollment passed; controlled reboot pending",
    "automatic wipe, re-enrollment, old-record rejection and reboot recovery verified; pages 5 and 11 all zero",
    "incomplete test data cleared; pages 5 and 11 all zero",
    "unexpected data; writes disabled", "reset test or authenticated verification failed",
)
KDF_TEST_RESULTS = ("not run", "encrypted slot 8 KDF verified against host HMAC",
                    "encrypted slot 8 KDF command or prerequisite failed", "KDF result mismatch")
ACCOUNT_TEST_STAGES = (
    "not started", "preflight", "write PIN authorization to slot 10", "rotate slot 11",
    "write random key to slot 12", "check PIN against slot 10", "derive with slot 11",
    "reauthorize slot 10", "derive with slot 12", "combined tests passed; reboot pending",
    "combined tests and reboot cleanup verified",
    "replace fixture authorization using factory transport credentials",
    "restore fixture authorization after rewrite test",
)


def decode_report(data):
    data = bytes(data)
    if len(data) != REPORT_SIZE or data[:5] != b"\x01FBD1":
        raise ValueError("Unexpected diagnostic report")
    attempt, phase, status, frames, dcmi_errors, ui = struct.unpack_from("<IIiIII", data, 5)
    camera = CAMERA_PHASES[phase] if phase < len(CAMERA_PHASES) else str(phase)
    interface = UI_PHASES[ui] if ui < len(UI_PHASES) else str(ui)
    return (
        f"attempt={attempt} | UI={interface} | camera={camera} | "
        f"status={status} | frames={frames} | DCMI errors={dcmi_errors}"
    )


def read_hardware_report(get_report):
    for _ in range(3):
        chunks = []
        for report_id in range(2, 9):
            report = bytes(get_report(report_id, REPORT_SIZE))
            if len(report) != REPORT_SIZE or report[0] != report_id:
                raise ValueError(f"Invalid hardware report chunk {report_id}")
            chunks.append(report[1:])
        if bytes(get_report(2, REPORT_SIZE))[1:5] == chunks[0][:4]:
            return b"".join(chunks)
    raise ValueError("Hardware report changed during the query. Retry after the scan finishes.")


def decode_boot_probe(data):
    generation, = struct.unpack_from("<I", data)
    revision, rom, lifecycle, signature = data[8:12]
    ro, rol, jtag, test_rom, _, cpuid, vtor = struct.unpack_from("<7I", data, 12)
    known_rom = rom in (1, 5)
    fp = data[80:]
    counts = struct.unpack_from("<4H", fp, 10)
    labels = {1: "valid reply", 2: "no UART bytes", 3: "no valid frame",
              4: "UART transmit timeout", 5: "module reported RNG error"}

    def reply(status, count):
        return {"status": labels.get(status, "unknown"), "uart_bytes_received": count}

    count_status = {1: "valid encrypted reply", 2: "pairing key read failed",
                    3: "pairing key blank or invalid", 4: "RNG unavailable",
                    5: "UART transmit timeout", 6: "no UART bytes",
                    7: "no valid encrypted reply", 8: "module rejected the command",
                    9: "AES operation failed"}
    report = {
        "snapshot": generation,
        "board_adc_revision": BOARD_VERSIONS[revision] if revision < len(BOARD_VERSIONS) else "unknown",
        "boot_rom": {
            "identity": {1: "AIR105 reference matched", 2: "different ROM fingerprint",
                         3: "no ROM vectors at address zero", 4: "ROM read fault",
                         5: "MH1903 reference matched"}.get(rom, "unknown"),
            "sha256_first_128_kib_at_zero": data[40:72].hex() if known_rom or rom == 2 else None,
            "production_marker": {1: "reads all ones", 2: "programmed",
                                  3: "unreadable or reads zero"}.get(lifecycle, "unverified"),
            "production_signature_selector": {1: "RSA-2048", 2: "SM2"}.get(signature),
            "random_delay_and_repeated_check_code_identified": True if known_rom else None,
            "reference_boot_attempt_counter": data[72] if known_rom and data[72] != 0xff else None,
        },
        "runtime_registers": {
            "otp_ro": f"0x{ro:08x}" if data[73] & 1 else None,
            "otp_rol": f"0x{rol:08x}" if data[73] & 2 else None,
            "otp_pairing_page_write_protected": bool(ro & (1 << 21)) if data[73] & 1 else None,
            "otp_pairing_page_write_lock_set": bool(rol & (1 << 21)) if data[73] & 2 else None,
            "jtag_control": f"0x{jtag:08x}",
            "test_rom_control": f"0x{test_rom:08x}",
            "core_halting_debug_enabled": bool(data[28]),
            "cpuid": f"0x{cpuid:08x}",
            "vector_table": f"0x{vtor:08x}",
        },
        "fingerprint_module": {
            "version_query": reply(fp[0], counts[0]),
            "firmware_version": ".".join(str(byte) for byte in fp[4:8]) if fp[0] == 1 else None,
            "pairing_query": reply(fp[1], counts[1]),
            "aes_pairing_flag": fp[8] if fp[1] == 1 and fp[8] in (0, 1) else None,
            "initialization_query": reply(fp[18], struct.unpack_from("<H", fp, 20)[0]),
            "initialization_status": fp[19] if fp[18] == 1 else None,
            "rng_first": reply(fp[2], counts[2]),
            "rng_second": reply(fp[3], counts[3]),
            "rng_replies_distinct": {1: True, 2: False}.get(fp[9]),
        },
    }
    if fp[22]:
        result, count, mode, tried, key_status, module_status, received = struct.unpack_from("<6BH", fp, 22)
        count_result = {
            "status": count_status.get(result, "unknown"),
            "pairing_key_read_verified": key_status == 1,
            "pairing_key_read_status": key_status,
            "template_count": count if result == 1 else None,
            "aes_mode": {1: "ECB", 2: "CBC"}.get(mode),
            "aes_modes_tried": [name for bit, name in ((2, "CBC"), (1, "ECB")) if tried & bit],
            "module_status": f"0x{module_status:02x}" if result == 8 else None,
            "uart_bytes_received": received,
        }
        attempts = {}
        for name, offset in (("ECB", 30), ("CBC", 40)):
            code, frames, acks, length, flags, plain_size, decrypted_size, _, rx = struct.unpack_from(
                "<8BH", fp, offset)
            if code:
                attempts[name] = {
                    "status": count_status.get(code, "unknown"),
                    "uart_bytes_received": rx,
                    "crc_valid_frames": frames,
                    "ack_sized_frames": acks,
                    "last_non_ack_frame_bytes": length or None,
                    "plaintext_a500_ack_seen": bool(flags & 1),
                    "plaintext_a500_payload_bytes": plain_size if flags & 2 else None,
                    "decrypted_a500_command_seen": bool(flags & 4),
                    "decrypted_a500_payload_bytes": decrypted_size if flags & 8 else None,
                }
        if attempts:
            count_result["attempts"] = attempts
        report["fingerprint_module"]["encrypted_template_count"] = count_result
    touch, = struct.unpack_from("<I", data, 140)
    if touch & 1:
        report["fingerprint_module"]["live_touch"] = {
            "pf14_active_low_asserted": bool(touch & 2),
            "transitions": touch >> 2,
        }
    fields = ("enrollment", "enrolled_finger_scan", "different_finger_scan", "delete", "template_count")
    live_test = {}
    for name, word in zip(fields, struct.unpack_from("<5I", data, 156)):
        if not word & 0x10000:
            continue
        value = (word & 0xffff) - (0x10000 if word & 0x8000 else 0)
        if value < 0:
            live_test[name] = {"transport_status": count_status.get(-value, "unknown"), "code": value}
        elif name == "template_count":
            live_test[name] = value
        else:
            live_test[name] = {"module_status": f"0x{value:02x}"}
    if live_test:
        live_test["sequence"], = struct.unpack_from("<I", data, 176)
        report["fingerprint_module"]["live_enrollment_test"] = live_test
    return report


def decode_hardware_report(data):
    if len(data) != 217:
        raise ValueError("Invalid hardware report size")
    generation, = struct.unpack_from("<I", data)
    if not generation:
        raise ValueError("The device is still collecting its startup report. Retry in a few seconds.")
    if data[4:8] == b"FBB1":
        return decode_boot_probe(data)
    gpio, config_flags, counter_mask, flags = data[4:8]
    config_mask = config_flags & 0x0f
    config = data[28:156]
    revision = data[214]
    counters = struct.unpack_from("<II", data, 20)
    valid_blocks, = struct.unpack_from("<I", data, 156)
    atecc = {
        "revision": data[8:12].hex() if flags & 1 else None,
        "config_blocks": [config[i * 32:(i + 1) * 32].hex() if config_mask & (1 << i) else None
                          for i in range(4)],
        "counter_0": counters[0] if counter_mask & 1 else None,
        "counter_1": counters[1] if counter_mask & 2 else None,
    }
    if config_mask == 15:
        slot_locked, = struct.unpack_from("<H", config, 88)
        slot_config = struct.unpack_from("<16H", config, 20)
        key_config = struct.unpack_from("<16H", config, 96)
        atecc.update({
            "count_match": f"0x{config[18]:02x}",
            "config_zone_lock_byte": f"0x{config[87]:02x}",
            "data_zone_lock_byte": f"0x{config[86]:02x}",
            "slots": [
                {"slot": i, "locked": not bool(slot_locked & (1 << i)),
                 "slot_config": f"0x{slot_config[i]:04x}", "key_config": f"0x{key_config[i]:04x}"}
                for i in range(16)
            ],
        })
    report = {
        "snapshot": generation,
        "board_adc_revision": BOARD_VERSIONS[revision] if revision < len(BOARD_VERSIONS) else "unknown",
        "tamper_pa2_high": bool(gpio & 1),
        "fingerprint": {
            "firmware_version": ".".join(str(b) for b in data[12:16]) if flags & 2 else None,
            "aes_pairing_flag": bool(data[16]) if flags & 4 else None,
            "irq_pf14_high": bool(gpio & 2),
            "pe11_high": bool(gpio & 4),
            "pe12_high": bool(gpio & 8),
        },
        "atecc": atecc,
        "ds28s60_block_protection": {
            str(block): data[160 + 2 * i:162 + 2 * i].hex() if valid_blocks & (1 << i) else None
            for i, block in enumerate(DS_BLOCKS)
        },
    }
    if data[19] in (1, 2, 3, 4, 5, 6, 7):
        def result_name(code):
            return PAIRING_RESULTS[code] if code < len(PAIRING_RESULTS) else f"unknown ({code})"

        def pairing_result(code, status):
            return {
                "result": result_name(code),
                "device_status": None if status == 0xff else f"0x{status:02x}",
            }

        report["factory_pairing"] = {
            "atecc608b_slot_1_checkmac": pairing_result(data[215], 0xff if data[19] == 7 else data[17]),
            "ds28s60_secret_a_authenticated_read": pairing_result(data[216], 0xff if data[19] == 7 else data[18]),
        }
        if data[19] >= 2:
            def probe_result(code, state):
                return {
                    "result": "read verified" if code == 1 else result_name(code),
                    "contents": BLANK_STATES[state] if state < len(BLANK_STATES) else None,
                }

            report["storage_probe"] = {
                "atecc608b_slot_8_encrypted_read": probe_result(data[12], data[14]),
                "ds28s60_page_5_authenticated_read": probe_result(data[13], data[15]),
                "atecc_count_match_enabled": bool(config[18] & 0x80) if config_mask & 1 else None,
                "atecc_counters_unchanged": [
                    bool(data[16] & (1 << i)) if data[16] & (1 << (i + 2)) else None
                    for i in range(2)
                ],
            }
            if data[19] >= 3:
                report["storage_probe"]["atecc608b_slot_13_encrypted_read"] = probe_result(
                    (flags >> 3) & 0x0f, config_flags >> 4)
        if data[19] == 4:
            code = counter_mask >> 4
            report["page_5_write_test"] = PAGE5_TEST_RESULTS[code] if code < len(PAGE5_TEST_RESULTS) else f"unknown ({code})"
        if data[19] == 5:
            code = counter_mask >> 4
            report["pin_retry_spi_test"] = {
                "result": PIN_TEST_RESULTS[code] if code < len(PIN_TEST_RESULTS) else f"unknown ({code})",
                "retry_limit": 3,
                "retry_enforcement": "firmware, using the authenticated DS28S60 page",
                "spi_record": "disposable fixture in RAM",
            }
        if data[19] in (6, 7):
            code = counter_mask >> 4
            report["wallet_reset_test"] = {
                "result": RESET_TEST_RESULTS[code] if code < len(RESET_TEST_RESULTS) else f"unknown ({code})",
                "atecc_kdf": KDF_TEST_RESULTS[(counter_mask >> 2) & 3],
                "spi_record": "disposable fixture in RAM",
            }
        if data[19] == 7:
            stage = data[17]
            report["combined_account_test"] = {
                "result": report["wallet_reset_test"]["result"],
                "atecc_stage": ACCOUNT_TEST_STAGES[stage] if stage < len(ACCOUNT_TEST_STAGES) else f"unknown ({stage})",
                "last_atecc_status": None if data[18] == 0xff else f"0x{data[18]:02x}",
                "factory_auth_rewrite_reproduced_original_kdf": bool(flags & 0x80),
                "atecc_counters_unchanged": [
                    bool(data[16] & (1 << (i + 4))) if data[16] & (1 << (i + 6)) else None
                    for i in range(2)
                ],
            }
    return report


class WindowsHID:
    """Read feature reports through the Windows HID API without pip."""
    def __init__(self):
        import ctypes
        from ctypes import wintypes as w
        import re
        import subprocess

        self.ctypes = ctypes
        self.handle = None
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.hid = ctypes.WinDLL("hid", use_last_error=True)
        self.kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID,
                                           w.DWORD, w.DWORD, w.HANDLE]
        self.kernel.CreateFileW.restype = w.HANDLE
        self.kernel.CloseHandle.argtypes = [w.HANDLE]
        for function in (self.hid.HidD_GetFeature, self.hid.HidD_GetProductString):
            function.argtypes = [w.HANDLE, w.LPVOID, w.ULONG]
            function.restype = ctypes.c_ubyte
        output = subprocess.check_output(["pnputil", "/enum-interfaces", "/enabled"],
                                         text=True, errors="replace")
        paths = re.findall(r"(\\\\\?\\hid#vid_1209&pid_0001[^\s]+)", output, re.I)
        for path in paths:
            handle = self.kernel.CreateFileW(path, 0x80000000, 3, None, 3, 0, None)
            if handle == ctypes.c_void_p(-1).value:
                continue
            product = ctypes.create_unicode_buffer(128)
            if self.hid.HidD_GetProductString(handle, product, ctypes.sizeof(product)) and product.value == PRODUCT:
                if self.handle is not None:
                    self.kernel.CloseHandle(handle)
                    self.close()
                    raise OSError(f"More than one '{PRODUCT}' device is connected")
                self.handle = handle
            else:
                self.kernel.CloseHandle(handle)
        if self.handle is None:
            raise OSError(f"'{PRODUCT}' was not found. Boot the diagnostic image and connect a USB data cable.")

    def get_feature_report(self, report_id, size):
        report = (self.ctypes.c_ubyte * size)(report_id)
        if not self.hid.HidD_GetFeature(self.handle, report, size):
            raise self.ctypes.WinError(self.ctypes.get_last_error())
        return bytes(report)

    def close(self):
        if self.handle is not None:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--watch", action="store_true", help="Watch camera or live fingerprint IRQ changes")
    mode.add_argument("--hardware", action="store_true", help="Print the secure-element and fingerprint report as JSON")
    args = parser.parse_args()
    device = None
    try:
        if sys.platform == "win32":
            device = WindowsHID()
        else:
            try:
                import hid
            except ImportError:
                parser.exit(1, "Install the host dependency with: python -m pip install hidapi\n")
            matches = [entry for entry in hid.enumerate(VID, PID) if entry.get("product_string") == PRODUCT]
            if len(matches) != 1:
                raise OSError(f"Expected one '{PRODUCT}' HID device, found {len(matches)}")
            device = hid.device()
            device.open_path(matches[0]["path"])
        initial = None if args.hardware else bytes(device.get_feature_report(REPORT_ID, REPORT_SIZE))
        if args.hardware or initial[:5] in (b"\x01FBP1", b"\x01FBB1"):
            report = read_hardware_report(device.get_feature_report)
            decoded = decode_hardware_report(report)
            print(json.dumps(decoded, indent=2), flush=True)
            if args.watch and report[4:8] == b"FBB1":
                live_fields = ("live_touch", "live_enrollment_test")
                previous_live = {key: decoded["fingerprint_module"].get(key) for key in live_fields}
                while True:
                    time.sleep(0.25)
                    report = read_hardware_report(device.get_feature_report)
                    current = decode_hardware_report(report)["fingerprint_module"]
                    live = {key: current.get(key) for key in live_fields}
                    if live != previous_live:
                        print(json.dumps({key: value for key, value in live.items() if value is not None}),
                              flush=True)
                        previous_live = live
            return
        previous = None
        while True:
            status = decode_report(initial)
            if status != previous:
                print(status, flush=True)
                previous = status
            if not args.watch:
                break
            time.sleep(0.25)
            initial = device.get_feature_report(REPORT_ID, REPORT_SIZE)
    except (OSError, ValueError) as error:
        parser.exit(1, f"USB diagnostic query failed: {error}\n")
    except KeyboardInterrupt:
        pass
    finally:
        if device is not None:
            device.close()


if __name__ == "__main__":
    main()
