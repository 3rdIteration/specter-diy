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
unittest.main(test_module)
