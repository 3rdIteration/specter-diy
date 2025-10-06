import os
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

sys.path.insert(0, os.path.abspath(os.path.join(BASE_DIR, '..', 'src')))
sys.path.insert(0, os.path.abspath(os.path.join(BASE_DIR, '..', 'f469-disco', 'libs', 'common')))
sys.path.insert(0, os.path.abspath(os.path.join(BASE_DIR, '..', 'f469-disco', 'libs', 'unix')))
sys.path.insert(0, os.path.abspath(os.path.join(BASE_DIR, '..', 'f469-disco', 'usermods', 'udisplay_f469', 'display_unixport')))
sys.path.insert(0, os.path.abspath(os.path.join(BASE_DIR, '..', 'f469-disco', 'tests')))

import unittest
from tests import util

util.clear_testdir()
unittest.main('tests')
