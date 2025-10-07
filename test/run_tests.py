import sys

# Ensure our local modules shadow similarly named stdlib modules (e.g. platform)
sys.path.insert(0, '.')
sys.path.insert(0, '../src')
sys.path.insert(0, '../f469-disco/libs/common')
sys.path.insert(0, '../f469-disco/libs/unix')
sys.path.insert(0, '../f469-disco/usermods/udisplay_f469/display_unixport')
sys.path.insert(0, '../f469-disco/tests')

if sys.implementation.name != 'micropython':
    from native_support import setup_native_stubs

    setup_native_stubs()

import unittest

is_micropython = sys.implementation.name == 'micropython'

if is_micropython:
    test_module = 'tests'
    try:
        import bech32 as _bech32_module  # type: ignore

        def _ensure_pair_result(attr):
            func = getattr(_bech32_module, attr, None)
            if func is None:
                return

            def _decoder(*args, **kwargs):
                res = func(*args, **kwargs)
                if isinstance(res, tuple) and len(res) > 2:
                    return res[0], res[1]
                return res

            setattr(_bech32_module, attr, _decoder)

        _ensure_pair_result('decode')
        _ensure_pair_result('bech32_decode')
        _ensure_pair_result('bech32m_decode')
    except ImportError:  # pragma: no cover - MicroPython without bundled bech32
        pass
else:
    test_module = 'tests_native'

try:
    from tests.util import clear_testdir
except ImportError:  # pragma: no cover - MicroPython may not expose package-style imports
    try:
        import platform

        def clear_testdir():
            try:
                platform.delete_recursively('testdir', include_self=True)
            except Exception:
                pass
    except Exception:  # pragma: no cover - fallback for extremely constrained ports
        def clear_testdir():
            pass

clear_testdir()

kwargs = {}
if not is_micropython:
    kwargs['verbosity'] = 2

unittest.main(test_module, **kwargs)
