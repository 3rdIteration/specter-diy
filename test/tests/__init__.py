import sys

from .test_keystore import *
from .test_wallets import *
from .test_sign import *
from .test_revault import *
from .test_compatibility import *

if sys.implementation.name != 'micropython':
    from .test_wallet_manager_parsing import *
