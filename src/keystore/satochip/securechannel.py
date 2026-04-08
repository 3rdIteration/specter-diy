"""
Satochip/SeedKeeper secure channel implementation.

Uses ECDH on secp256k1 for key agreement, AES-128-CBC for encryption,
and HMAC-SHA1 for key derivation and message authentication.

This is the Satochip-style secure channel, which is different from the
Specter-DIY MemoryCard secure channel protocol.
"""
import secp256k1
import hmac
import hashlib
from rng import get_random_bytes
from ucryptolib import aes

AES_BLOCK = 16
AES_CBC = 2


class SatochipSecureChannelError(Exception):
    pass


class SatochipSecureChannel:
    """
    Implements the Satochip secure channel for encrypted APDU communication.

    Protocol:
    1. Host generates ephemeral EC key pair
    2. Host sends pubkey to card (INS=0x81)
    3. Card responds with its ephemeral pubkey
    4. Both sides do ECDH to derive shared secret
    5. Session keys derived via HMAC-SHA1
    6. All subsequent APDUs encrypted with AES-CBC + HMAC-SHA1 MAC
    """

    def __init__(self):
        self.initialized = False
        self.iv_counter = 1
        self.aes_key = None   # 16 bytes, for AES-128
        self.mac_key = None   # 20 bytes, for HMAC-SHA1
        self._privkey = None
        self._pubkey = None
        self._pubkey_bytes = None

    def generate_keypair(self):
        """Generate ephemeral EC key pair for ECDH handshake."""
        self._privkey = get_random_bytes(32)
        self._pubkey = secp256k1.ec_pubkey_create(self._privkey)
        self._pubkey_bytes = secp256k1.ec_pubkey_serialize(
            self._pubkey, secp256k1.EC_UNCOMPRESSED
        )
        return self._pubkey_bytes

    @property
    def pubkey_bytes(self):
        """Return the host's uncompressed public key (65 bytes)."""
        if self._pubkey_bytes is None:
            self.generate_keypair()
        return self._pubkey_bytes

    def initiate(self, peer_pubkey_bytes):
        """
        Complete the ECDH key exchange and derive session keys.

        Args:
            peer_pubkey_bytes: The card's public key (65 bytes uncompressed,
                               33 bytes compressed, or 32 bytes x-only).
        """
        # Parse the peer's public key
        if len(peer_pubkey_bytes) == 32:
            # x-only: prepend 0x02 to create a valid compressed key.
            # Either parity gives the same shared x-coordinate after ECDH,
            # which is all we use as the shared secret.
            peer_pubkey_bytes = b"\x02" + peer_pubkey_bytes
        peer_pub = secp256k1.ec_pubkey_parse(peer_pubkey_bytes)

        # ECDH: multiply peer's pubkey by our private key
        # Need to copy since tweak_mul modifies in place
        shared_point = secp256k1.ec_pubkey_parse(
            secp256k1.ec_pubkey_serialize(peer_pub)
        )
        secp256k1.ec_pubkey_tweak_mul(shared_point, self._privkey)
        shared_secret = secp256k1.ec_pubkey_serialize(shared_point)[1:33]

        # Derive session keys using HMAC-SHA1
        # AES key: first 16 bytes of HMAC-SHA1(shared_secret, "sc_key")
        h = hmac.new(shared_secret, b"sc_key", digestmod="sha1")
        self.aes_key = h.digest()[:16]

        # MAC key: HMAC-SHA1(shared_secret, "sc_mac")
        h = hmac.new(shared_secret, b"sc_mac", digestmod="sha1")
        self.mac_key = h.digest()

        self.iv_counter = 1
        self.initialized = True

    def encrypt(self, data):
        """
        Encrypt data for sending to the card.

        Args:
            data: plaintext bytes (the full APDU to encrypt)

        Returns:
            tuple: (iv, ciphertext, mac) as bytes
        """
        if not self.initialized:
            raise SatochipSecureChannelError("Secure channel not initialized")

        # PKCS#7 padding
        padded = _pkcs7_pad(data)

        # IV: 12 random bytes + 4-byte counter (big-endian)
        iv = get_random_bytes(12) + self.iv_counter.to_bytes(4, "big")

        # AES-CBC encrypt
        cipher = aes(self.aes_key, AES_CBC, iv)
        ciphertext = cipher.encrypt(padded)

        # Increment counter by 2 (matching pysatochip behavior)
        self.iv_counter += 2

        # HMAC-SHA1 over (iv + ciphertext_size + ciphertext)
        ct_size = len(ciphertext).to_bytes(2, "big")
        mac_data = iv + ct_size + ciphertext
        mac = hmac.new(self.mac_key, mac_data, digestmod="sha1").digest()

        return (iv, ciphertext, mac)

    def decrypt(self, iv, ciphertext):
        """
        Decrypt data received from the card.

        Args:
            iv: 16-byte initialization vector
            ciphertext: encrypted data

        Returns:
            Decrypted plaintext bytes (padding removed)
        """
        if not self.initialized:
            raise SatochipSecureChannelError("Secure channel not initialized")

        # AES-CBC decrypt
        cipher = aes(self.aes_key, AES_CBC, iv)
        padded = cipher.decrypt(ciphertext)

        # Remove PKCS#7 padding
        return _pkcs7_unpad(padded)


def _pkcs7_pad(data, block_size=AES_BLOCK):
    """Apply PKCS#7 padding."""
    pad_size = block_size - (len(data) % block_size)
    return data + bytes([pad_size] * pad_size)


def _pkcs7_unpad(data):
    """Remove PKCS#7 padding."""
    if len(data) == 0:
        return data
    pad_size = data[-1]
    if pad_size < 1 or pad_size > AES_BLOCK:
        raise SatochipSecureChannelError("Invalid PKCS#7 padding")
    # Verify all padding bytes
    for i in range(pad_size):
        if data[-(i + 1)] != pad_size:
            raise SatochipSecureChannelError("Invalid PKCS#7 padding")
    return data[:-pad_size]
