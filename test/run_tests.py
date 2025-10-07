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
import importlib.util

if sys.implementation.name == 'micropython':
    test_module = 'tests'
else:
    test_module = 'tests_native'

util_spec = importlib.util.spec_from_file_location('tests.util', 'tests/util.py')
util = importlib.util.module_from_spec(util_spec)
util_spec.loader.exec_module(util)

util.clear_testdir()
unittest.main(test_module)
