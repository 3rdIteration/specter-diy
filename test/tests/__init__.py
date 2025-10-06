import warnings

try:
    from .test_keystore import *  # noqa: F401,F403
except ImportError as exc:
    warnings.warn(f"Skipping keystore tests: {exc}")

try:
    from .test_wallets import *  # noqa: F401,F403
except ImportError as exc:
    warnings.warn(f"Skipping wallet tests: {exc}")

try:
    from .test_sign import *  # noqa: F401,F403
except ImportError as exc:
    warnings.warn(f"Skipping signing tests: {exc}")

try:
    from .test_revault import *  # noqa: F401,F403
except ImportError as exc:
    warnings.warn(f"Skipping Revault tests: {exc}")

try:
    from .test_compatibility import *  # noqa: F401,F403
except ImportError as exc:
    warnings.warn(f"Skipping compatibility tests: {exc}")

from .test_wallet_manager import *  # noqa: F401,F403
