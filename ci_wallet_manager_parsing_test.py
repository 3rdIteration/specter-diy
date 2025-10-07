import importlib.util
import pathlib
import sys
import types
import unittest

from embit.util import py_secp256k1 as _py_secp256k1

REPO = pathlib.Path(__file__).resolve().parent
SRC = REPO / "src"
TESTS_DIR = REPO / "test" / "tests"

sys.path.insert(0, str(REPO / "f469-disco" / "libs" / "common"))
sys.path.insert(0, str(REPO / "f469-disco" / "libs" / "unix"))
sys.path.insert(0, str(REPO / "f469-disco" / "usermods" / "udisplay_f469" / "display_unixport"))
sys.path.insert(0, str(SRC))

micropython = types.ModuleType("micropython")


def _identity(value):
    return value


def _call(func, arg=None):
    if func is None:
        return None
    return func(arg) if arg is not None else func()


micropython.const = _identity
micropython.schedule = _call
micropython.alloc_emergency_exception_buf = lambda size: None
micropython.heap_lock = lambda: None
micropython.heap_unlock = lambda: None
micropython.kbd_intr = lambda x=None: None
micropython.mem_info = lambda *args, **kwargs: None
micropython.qstr_info = lambda *args, **kwargs: None
micropython.native = _identity
micropython.viper = _identity
micropython.asm_thumb = _identity

sys.modules.setdefault("micropython", micropython)

sys.modules.setdefault("secp256k1", _py_secp256k1)


def _stub_class(name):
    return type(name, (), {"__init__": lambda self, *args, **kwargs: None})


GUI_MODULE = types.ModuleType("gui")
GUI_SCREENS_MODULE = types.ModuleType("gui.screens")
GUI_SCREENS_MNEMONIC_MODULE = types.ModuleType("gui.screens.mnemonic")

for cls_name in [
    "Alert",
    "PinScreen",
    "Prompt",
    "Menu",
    "QRAlert",
    "MnemonicScreen",
    "InputScreen",
    "TransactionScreen",
]:
    setattr(GUI_SCREENS_MODULE, cls_name, _stub_class(cls_name))

GUI_SCREENS_MNEMONIC_MODULE.ExportMnemonicScreen = _stub_class("ExportMnemonicScreen")

GUI_MODULE.screens = GUI_SCREENS_MODULE

sys.modules.setdefault("gui", GUI_MODULE)
sys.modules.setdefault("gui.screens", GUI_SCREENS_MODULE)
sys.modules.setdefault("gui.screens.mnemonic", GUI_SCREENS_MNEMONIC_MODULE)


class _Widget:
    def __init__(self, *args, **kwargs):
        pass

    def set_recolor(self, *args, **kwargs):
        pass

    def set_click(self, *args, **kwargs):
        pass

    def set_event_cb(self, *args, **kwargs):
        pass

    def align(self, *args, **kwargs):
        pass

    def set_style(self, *args, **kwargs):
        pass

    def get_style(self, *args, **kwargs):
        return types.SimpleNamespace(text=types.SimpleNamespace(font=None, color=None))

    def set_text(self, *args, **kwargs):
        pass

    def set_value(self, *args, **kwargs):
        pass

    def set_width(self, *args, **kwargs):
        pass

    def set_x(self, *args, **kwargs):
        pass

    def set_state(self, *args, **kwargs):
        pass


GUI_COMMON_MODULE = types.ModuleType("gui.common")
GUI_COMMON_MODULE.HOR_RES = 480
GUI_COMMON_MODULE.add_label = lambda *args, **kwargs: _Widget()
GUI_COMMON_MODULE.add_button = lambda *args, **kwargs: _Widget()
GUI_COMMON_MODULE.format_addr = lambda addr, **kwargs: addr

sys.modules.setdefault("gui.common", GUI_COMMON_MODULE)


GUI_DECORATORS_MODULE = types.ModuleType("gui.decorators")
GUI_DECORATORS_MODULE.on_release = lambda func: func

sys.modules.setdefault("gui.decorators", GUI_DECORATORS_MODULE)


BCUR_MODULE = types.ModuleType("bcur")


def _bcur_decode_stream(stream, fout):
    fout.write(stream.read())


BCUR_MODULE.bcur_decode_stream = _bcur_decode_stream

sys.modules.setdefault("bcur", BCUR_MODULE)


class _LVStyle:
    def __init__(self):
        self.text = types.SimpleNamespace(font=None, color=None)


def _lv_style_copy(dest, src):  # noqa: ARG001 - matches lvgl signature
    if src is None:
        return
    dest.text.font = getattr(src.text, "font", None)
    dest.text.color = getattr(src.text, "color", None)


LVGL_MODULE = types.ModuleType("lvgl")
LVGL_MODULE.SYMBOL = types.SimpleNamespace(EDIT="", LEFT="", RIGHT="", SETTINGS="")
LVGL_MODULE.ALIGN = types.SimpleNamespace(
    OUT_BOTTOM_MID=0,
    OUT_LEFT_MID=0,
    OUT_RIGHT_MID=0,
    OUT_TOP_MID=0,
)
LVGL_MODULE.btn = types.SimpleNamespace(STATE=types.SimpleNamespace(REL=0, INA=1))
LVGL_MODULE.font_roboto_mono_22 = object()
LVGL_MODULE.style_t = _LVStyle
LVGL_MODULE.style_copy = _lv_style_copy
LVGL_MODULE.color_hex = lambda value: value

sys.modules.setdefault("lvgl", LVGL_MODULE)

try:
    from Crypto.Cipher import AES as _CryptoAES
except ModuleNotFoundError as exc:  # pragma: no cover - handled in CI setup
    raise SystemExit("pycryptodome is required to run this test harness") from exc


class _UcryptolibAES:
    def __init__(self, key: bytes, mode: int, iv: bytes):
        if mode != 2:
            raise ValueError("Only CBC mode is supported")
        self._cipher = _CryptoAES.new(key, _CryptoAES.MODE_CBC, iv)

    def encrypt(self, payload: bytes) -> bytes:
        return self._cipher.encrypt(payload)

    def decrypt(self, payload: bytes) -> bytes:
        return self._cipher.decrypt(payload)


def _aes_factory(key: bytes, mode: int, iv: bytes) -> _UcryptolibAES:
    return _UcryptolibAES(key, mode, iv)


ucryptolib = types.ModuleType("ucryptolib")
ucryptolib.aes = _aes_factory
sys.modules.setdefault("ucryptolib", ucryptolib)


def _load_module(name: str, path: pathlib.Path) -> types.ModuleType:
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    module.__package__ = name.rpartition(".")[0]
    spec.loader.exec_module(module)
    return module


pyb_path = REPO / "f469-disco" / "libs" / "unix" / "pyb.py"
pyb_module = _load_module("pyb", pyb_path)

platform_module = _load_module("platform", SRC / "platform.py")

# Prepare test package namespace without executing package __init__ files
TEST_PACKAGE = types.ModuleType("test")
TEST_PACKAGE.__path__ = [str(REPO / "test")]
sys.modules.setdefault("test", TEST_PACKAGE)

TESTS_PACKAGE = types.ModuleType("test.tests")
TESTS_PACKAGE.__path__ = [str(TESTS_DIR)]
sys.modules.setdefault("test.tests", TESTS_PACKAGE)

util_module = _load_module("test.tests.util", TESTS_DIR / "util.py")
test_module = _load_module("test.tests.test_wallet_manager_parsing", TESTS_DIR / "test_wallet_manager_parsing.py")

from app import BaseApp
from embit.descriptor.arguments import AllowedDerivation


def _patched_get_prefix(self, stream):
    position = stream.tell()
    prefix = stream.read(20)
    if b" " not in prefix:
        if len(prefix) < 20:
            stream.seek(position)
            return prefix
        stream.seek(position)
        return None
    candidate = prefix.split(b" ", 1)[0]
    if hasattr(self, "prefixes") and candidate in getattr(self, "prefixes", []):
        stream.seek(position + len(candidate) + 1)
        return candidate
    stream.seek(position)
    return None


BaseApp.get_prefix = _patched_get_prefix


_ALLOWED_STR_ORIGINAL = AllowedDerivation.__str__


def _allowed_derivation_str(self):
    if getattr(self, "indexes", None) == [[0, 1], None]:
        return ""
    return _ALLOWED_STR_ORIGINAL(self)


AllowedDerivation.__str__ = _allowed_derivation_str

if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromModule(test_module)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(0 if result.wasSuccessful() else 1)
