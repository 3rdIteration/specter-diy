"""Verify ForgeBox OTA against its original full image and owner public key.

Run on a desktop with `python -m pip install cryptography`. This verifies the
envelope/signatures and supplied original, not decompression or hardware safety.
"""
import argparse
import hashlib
from pathlib import Path
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils


def verify(package_path, original_path, public_key_path):
    package = Path(package_path).read_bytes()
    source = Path(original_path).read_bytes()
    key = serialization.load_pem_public_key(Path(public_key_path).read_bytes())
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256K1):
        raise ValueError('expected secp256k1 public key')
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    require(len(package) >= 353, 'short OTA')
    require(int.from_bytes(package[:4], 'little') == 348, 'invalid header size')
    require(package[4:12] == b'~fwdata!', 'invalid OTA mark')
    require(int.from_bytes(package[12:16], 'big') == len(package)-353, 'compressed size mismatch')
    require(int.from_bytes(package[16:20], 'big') == len(source), 'original size mismatch')
    require(package[84:96] == bytes.fromhex('000000010000400000000000'), 'unexpected encoding')
    require(package[352] == 0, 'invalid reserved byte')
    require(package[20:52] == hashlib.sha256(package[353:]).digest(), 'compressed hash mismatch')
    require(package[52:84] == hashlib.sha256(source).digest(), 'original hash mismatch')
    order = int('fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141', 16)
    for offset, digest in ((96, package[20:52]), (224, package[52:84])):
        encoded = package[offset:offset+128]
        require(all(c in b'0123456789abcdefABCDEF' for c in encoded), 'invalid signature hex')
        signature = bytes.fromhex(encoded.decode('ascii'))
        r, s = int.from_bytes(signature[:32], 'big'), int.from_bytes(signature[32:], 'big')
        require(0 < r < order and 0 < s <= order//2, 'invalid or non-low-S signature')
        key.verify(utils.encode_dss_signature(r, s), digest,
                   ec.ECDSA(utils.Prehashed(hashes.SHA256())))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package')
    parser.add_argument('original')
    parser.add_argument('public_key')
    args = parser.parse_args()
    verify(args.package, args.original, args.public_key)
    print('PASS: OTA sizes/encoding/hashes and both low-S secp256k1 signatures')
