"""Test stub for the secp256k1 module."""

EC_UNCOMPRESSED = 0


def ecdsa_sign_recoverable(*args, **kwargs):
    return b"" * 65


def ec_pubkey_parse(*args, **kwargs):
    return None


def ec_pubkey_create(*args, **kwargs):
    return None


def ec_pubkey_serialize(*args, **kwargs):
    return b""


def ec_pubkey_tweak_mul(*args, **kwargs):
    return None


def ecdsa_signature_parse_der(*args, **kwargs):
    return None


def ecdsa_signature_normalize(sig):
    return sig


def ecdsa_verify(*args, **kwargs):
    return False


def ecdsa_recoverable_signature_convert(*args, **kwargs):
    return None
