import sys

# Ensure our local modules shadow similarly named stdlib modules (e.g. platform)
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

from tests import util

util.clear_testdir()
unittest.main(test_module)
