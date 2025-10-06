import shutil
import sys
import tempfile
import types
from io import BytesIO
from unittest import TestCase

# Provide a minimal stub for the MicroPython-specific `pyb` module so that
# platform-dependent imports used by the wallet manager succeed in tests.
if "pyb" not in sys.modules:
    pyb_module = types.ModuleType("pyb")

    class _DummySDCard:
        def present(self):
            return True

        def power(self, *args, **kwargs):
            pass

    class _DummyLED:
        def __init__(self, *args, **kwargs):
            pass

        def on(self):
            pass

        def off(self):
            pass

    pyb_module.SDCard = _DummySDCard
    pyb_module.LED = _DummyLED
    sys.modules["pyb"] = pyb_module

# Provide minimal stubs for GUI modules referenced during import.
if "gui" not in sys.modules:
    gui_module = types.ModuleType("gui")
    sys.modules["gui"] = gui_module

if "gui.screens" not in sys.modules:
    gui_screens_module = types.ModuleType("gui.screens")

    class _DummyScreen:
        pass

    gui_screens_module.Menu = _DummyScreen
    gui_screens_module.InputScreen = _DummyScreen
    gui_screens_module.Prompt = _DummyScreen
    gui_screens_module.TransactionScreen = _DummyScreen
    gui_screens_module.Alert = _DummyScreen
    gui_screens_module.QRAlert = _DummyScreen
    sys.modules["gui.screens"] = gui_screens_module

if "gui.common" not in sys.modules:
    gui_common_module = types.ModuleType("gui.common")

    def _noop(*args, **kwargs):
        return None

    gui_common_module.add_label = _noop
    gui_common_module.add_button = _noop
    gui_common_module.format_addr = lambda addr, *args, **kwargs: addr
    gui_common_module.HOR_RES = 0
    sys.modules["gui.common"] = gui_common_module

if "lvgl" not in sys.modules:
    sys.modules["lvgl"] = types.ModuleType("lvgl")

# Stub out wallet screens to avoid importing GUI-heavy dependencies.
if "apps.wallets.screens" not in sys.modules:
    wallet_screens_module = types.ModuleType("apps.wallets.screens")

    class _DummyWalletScreen:
        pass

    wallet_screens_module.WalletScreen = _DummyWalletScreen
    wallet_screens_module.ConfirmWalletScreen = _DummyWalletScreen
    wallet_screens_module.WalletInfoScreen = _DummyWalletScreen
    sys.modules["apps.wallets.screens"] = wallet_screens_module

# Stub BCUR dependency which is only used for UR decoding.
if "bcur" not in sys.modules:
    bcur_module = types.ModuleType("bcur")

    def _noop_decode(stream):
        return stream

    bcur_module.bcur_decode_stream = _noop_decode
    sys.modules["bcur"] = bcur_module

# Provide a stub for MicroPython's ucryptolib module.
if "ucryptolib" not in sys.modules:
    ucryptolib_module = types.ModuleType("ucryptolib")

    class _DummyAES:
        def __init__(self, *args, **kwargs):
            pass

        def encrypt(self, data):
            return data

        def decrypt(self, data):
            return data

    def _aes_factory(*args, **kwargs):
        return _DummyAES()

    ucryptolib_module.aes = _aes_factory
    sys.modules["ucryptolib"] = ucryptolib_module

# Stub Liquid wallet manager to avoid pulling heavy Liquid dependencies.
if "apps.wallets.liquid.manager" not in sys.modules:
    liquid_manager_module = types.ModuleType("apps.wallets.liquid.manager")

    class _DummyLiquidWalletManager:
        pass

    liquid_manager_module.LWalletManager = _DummyLiquidWalletManager
    sys.modules["apps.wallets.liquid.manager"] = liquid_manager_module

from apps.wallets.manager import (
    WalletError,
    WalletManager,
    ADD_WALLET,
    SIGN_PSBT,
    VERIFY_ADDRESS,
)

DOC_MULTISIG_DESCRIPTOR = """wsh(sortedmulti(2,[b317ec86/48h/1h/0h/2h]tpubDEToKMGFhyuP6kfwvjtYaf56khzS1cUcwc47C6aMH6bQ8sNVLMcCK6jr21YDCkU2QhTK5CAnddhfgZ8dD4EL1wGCaAKZaGFeVVdXHaJMTMn,[f04828fe/48h/1h/0h/2h]tpubDFekS5zvPSdW6WWjH2p7vPRkxmeeNGnirmj36AUyoAYbJvfKBj6UARWR5gQ6FRrr98dzT1XFTi6rfGo9AAAeutY1S6SoWijQ8BKxDhYQzDR,[d3c05b2e/48h/1h/0h/2h]tpubDFnAczXQTHxuBh7FxrpLDHBidkC1Di54pTPSPMu4AQjKziFQQTTEFXEVugqm8ucKQhJfLGesBjRZWtLpqAkAmecoXtvaPwCzf4teqrY7Uu5))"""

DOC_BASE64_PSBT = (
    "cHNidP8BAHECAAAAAQWaIPxj7qSA0cbaKKz5Lk43V/8/FZeQw+IQ2tV6eJnbAAAAAAD9////AgfVTQUAAAAAFgAUUzfXvW1SC/+493dPMkR+9Ua1+7mAlpgAAAAAABYAFCwSoUTerJLG437IpfbWF8DgWx6kAAAAAAABAR8Kl+YFAAAAABYAFC80qhzwClOwVaKRoDp9RfCmmItSIgYDXUnszVTQCZ5DZ2J3x6bUYl1hHaiKXfSb+VF6d5Gnd6UYc8XaClQAAIABAACAAAAAgAEAAAAAAAAAACICAzra7/AYOHv1KHXP0Kgv8paA8ELhUBDLW3FrKXZzZpg2GHPF2gpUAACAAQAAgAAAAIABAAAAAAA"
)

DOC_ADDRESS_VERIFICATION = (
    b"bitcoin:bcrt1qd3mtrhysk3k4w6fmu7ayjvwk6q98c2dpf0p4x87zauu8rcgq5dzq73tyrx?index=2"
)


class WalletManagerParseStreamTest(TestCase):
    def setUp(self):
        self.tempdir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.tempdir, ignore_errors=True))
        self.manager = WalletManager(self.tempdir)

    def test_parse_stream_detects_prefixed_wallet_import(self):
        qr_payload = f"addwallet My multisig&{DOC_MULTISIG_DESCRIPTOR}".encode()
        stream = BytesIO(qr_payload)

        command, parsed_stream = self.manager.parse_stream(stream)

        self.assertEqual(command, ADD_WALLET)
        self.assertIs(parsed_stream, stream)
        self.assertEqual(parsed_stream.tell(), len(b"addwallet "))
        self.assertEqual(parsed_stream.read(), qr_payload[len(b"addwallet "):])

    def test_parse_stream_detects_base64_psbt_transaction(self):
        stream = BytesIO(DOC_BASE64_PSBT.encode())

        command, parsed_stream = self.manager.parse_stream(stream)

        self.assertEqual(command, SIGN_PSBT)
        self.assertIs(parsed_stream, stream)
        self.assertEqual(parsed_stream.tell(), 0)
        self.assertEqual(parsed_stream.read(), DOC_BASE64_PSBT.encode())

    def test_parse_stream_detects_address_verification(self):
        stream = BytesIO(DOC_ADDRESS_VERIFICATION)

        command, parsed_stream = self.manager.parse_stream(stream)

        self.assertEqual(command, VERIFY_ADDRESS)
        self.assertIs(parsed_stream, stream)
        self.assertEqual(parsed_stream.tell(), len(b"bitcoin:"))
        self.assertEqual(parsed_stream.read(), DOC_ADDRESS_VERIFICATION[len(b"bitcoin:"):])

    def test_parse_stream_detects_index_only_address_payload(self):
        payload = b"bc1qtestspecter?index=2"
        stream = BytesIO(payload)

        command, parsed_stream = self.manager.parse_stream(stream)

        self.assertEqual(command, VERIFY_ADDRESS)
        self.assertIs(parsed_stream, stream)
        self.assertEqual(parsed_stream.tell(), 0)
        self.assertEqual(parsed_stream.read(), payload)


class WalletManagerParseWalletTest(TestCase):
    def setUp(self):
        self.tempdir = tempfile.mkdtemp()
        self.addCleanup(lambda: shutil.rmtree(self.tempdir, ignore_errors=True))
        self.manager = WalletManager(self.tempdir)
        self.manager.network = "test"
        self.manager.wallets = []

    def test_parse_wallet_returns_named_wallet(self):
        wallet = self.manager.parse_wallet(f"My multisig&{DOC_MULTISIG_DESCRIPTOR}")

        self.assertEqual(wallet.name, "My multisig")
        self.assertIn("sortedmulti", str(wallet.descriptor))

    def test_parse_wallet_rejects_duplicate_descriptors(self):
        wallet = self.manager.parse_wallet(f"My multisig&{DOC_MULTISIG_DESCRIPTOR}")
        self.manager.wallets = [wallet]

        with self.assertRaises(WalletError):
            self.manager.parse_wallet(f"Another name&{DOC_MULTISIG_DESCRIPTOR}")

    def test_parse_wallet_detects_network_mismatch(self):
        self.manager.network = "main"

        with self.assertRaises(WalletError):
            self.manager.parse_wallet(f"My multisig&{DOC_MULTISIG_DESCRIPTOR}")

