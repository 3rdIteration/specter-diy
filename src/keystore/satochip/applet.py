"""
SeedKeeper/Satochip applet interface for Specter-DIY.

Implements the APDU protocol for communicating with a SeedKeeper smartcard.
Handles applet selection, secure channel, PIN management, and secret CRUD.

Only storage-related operations are implemented (no on-card signing).
All BIP32 derivation and signing happens on the MCU.
"""
from binascii import hexlify
from io import BytesIO


class SeedKeeperError(Exception):
    """Error returned by the SeedKeeper card."""
    pass


class SeedKeeperApplet:
    """
    Interface to SeedKeeper/Satochip JavaCard applet.

    Provides methods for:
    - Applet selection and status
    - Secure channel management
    - PIN verification and management
    - Secret storage (import, export, list, delete, generate)
    """

    # Class-level APDU constants
    CLA = 0xB0

    # Applet AIDs
    SEEDKEEPER_AID = b"\x53\x65\x65\x64\x4b\x65\x65\x70\x65\x72"  # "SeedKeeper"
    SATOCHIP_AID = b"\x53\x61\x74\x6f\x43\x68\x69\x70"  # "SatoChip"

    # Instruction codes
    INS_SELECT = 0xA4
    INS_SETUP = 0x2A
    INS_GET_STATUS = 0x3C
    INS_VERIFY_PIN = 0x42
    INS_CREATE_PIN = 0x40
    INS_CHANGE_PIN = 0x44
    INS_UNBLOCK_PIN = 0x46
    INS_INIT_SECURE_CHANNEL = 0x81
    INS_PROCESS_SECURE_CHANNEL = 0x82
    INS_GENERATE_MASTERSEED = 0xA0
    INS_IMPORT_SECRET = 0xA1
    INS_EXPORT_SECRET = 0xA2
    INS_GENERATE_RANDOM = 0xA3
    INS_RESET_SECRET = 0xA5
    INS_LIST_SECRET_HEADERS = 0xA6
    INS_SEEDKEEPER_STATUS = 0xA7
    INS_RESET_FACTORY = 0xFF
    INS_BIP32_GET_AUTHENTIKEY = 0x73

    # Instructions exempt from secure channel encryption
    PLAINTEXT_INS = {INS_SELECT, INS_INIT_SECURE_CHANNEL,
                     INS_PROCESS_SECURE_CHANNEL, INS_RESET_FACTORY,
                     INS_GET_STATUS}

    # Secret types
    TYPE_MASTERSEED = 0x10
    TYPE_BIP39_MNEMONIC = 0x30
    TYPE_PASSWORD = 0x90
    TYPE_DATA = 0xC0

    # Export rights
    EXPORT_FORBIDDEN = 0x00
    EXPORT_PLAINTEXT = 0x01
    EXPORT_ENCRYPTED = 0x02
    EXPORT_AUTHENTICATED = 0x03

    # OP codes for multi-step operations
    OP_INIT = 0x01
    OP_PROCESS = 0x02
    OP_FINAL = 0x03

    # Secret type labels for UI
    TYPE_LABELS = {
        0x10: "Masterseed",
        0x30: "BIP39 mnemonic",
        0x40: "Electrum mnemonic",
        0x60: "Private Key",
        0x70: "Public Key",
        0x80: "Secret Key",
        0x90: "Password",
        0xA0: "Certificate",
        0xB0: "2FA secret",
        0xC0: "Data",
    }

    NAME = "SeedKeeper"

    def __init__(self, connection):
        from .securechannel import SatochipSecureChannel
        self.conn = connection
        self.sc = SatochipSecureChannel()
        self.needs_secure_channel = False
        self.setup_done = False
        self.card_type = None
        self._pin_attempts_left = None
        self._pin_attempts_max = None
        self._pin_set = None
        self.protocol_version = 0

    # ─── Low-level APDU transport ────────────────────────────────

    def _raw_transmit(self, apdu):
        """
        Send raw APDU bytes and return (response_data, sw1, sw2).
        Uses the uscard connection directly.
        """
        if not self.conn.isCardInserted():
            raise SeedKeeperError("Card is not present")
        data = self.conn.transmit(apdu)
        sw1 = data[-2]
        sw2 = data[-1]
        response = data[:-2]
        if isinstance(response, memoryview):
            response = bytes(response)
        return (response, sw1, sw2)

    def transmit(self, apdu):
        """
        Send an APDU, handling secure channel encryption/decryption.

        Args:
            apdu: the plaintext APDU as bytes or bytearray

        Returns:
            (response, sw1, sw2) tuple
        """
        ins = apdu[1] if len(apdu) > 1 else 0

        # Encrypt via secure channel if needed
        if self.needs_secure_channel and ins not in self.PLAINTEXT_INS:
            enc_apdu = self._encrypt_apdu(apdu)
            response, sw1, sw2 = self._raw_transmit(enc_apdu)
            # Decrypt response
            if sw1 == 0x90 and sw2 == 0x00 and len(response) > 0:
                response = self._decrypt_response(response)
            return (response, sw1, sw2)
        else:
            return self._raw_transmit(apdu)

    def _encrypt_apdu(self, plain_apdu):
        """Encrypt a plaintext APDU for secure channel transmission."""
        iv, ciphertext, mac = self.sc.encrypt(bytes(plain_apdu))
        data = (
            iv
            + len(ciphertext).to_bytes(2, "big")
            + ciphertext
            + len(mac).to_bytes(2, "big")
            + mac
        )
        return bytes([self.CLA, self.INS_PROCESS_SECURE_CHANNEL, 0x00, 0x00,
                       len(data)]) + data

    def _decrypt_response(self, response):
        """Decrypt a response from the secure channel."""
        if len(response) < 18:
            raise SeedKeeperError("Encrypted response too short")
        iv = bytes(response[0:16])
        ct_size = (response[16] << 8) + response[17]
        ciphertext = bytes(response[18:18 + ct_size])
        if len(ciphertext) != ct_size:
            raise SeedKeeperError("Ciphertext length mismatch")
        return self.sc.decrypt(iv, ciphertext)

    def _check_sw(self, sw1, sw2, context=""):
        """Check status word and raise on error."""
        if sw1 == 0x90 and sw2 == 0x00:
            return
        sw = (sw1 << 8) | sw2
        msg = "Card error 0x%04X" % sw
        if context:
            msg = "%s: %s" % (context, msg)
        raise SeedKeeperError(msg)

    # ─── Applet selection & status ────────────────────────────────

    def select(self):
        """Select the SeedKeeper applet (falls back to Satochip)."""
        # Try SeedKeeper first
        for aid, name in [(self.SEEDKEEPER_AID, "SeedKeeper"),
                          (self.SATOCHIP_AID, "Satochip")]:
            try:
                apdu = bytes([0x00, self.INS_SELECT, 0x04, 0x00,
                              len(aid)]) + aid
                response, sw1, sw2 = self._raw_transmit(apdu)
                if sw1 == 0x90 and sw2 == 0x00:
                    self.card_type = name
                    return response
            except Exception:
                continue
        raise SeedKeeperError("No SeedKeeper or Satochip applet found")

    def get_status(self):
        """
        Get card status including version, PIN state, setup state.

        Returns:
            dict with status fields
        """
        apdu = bytes([self.CLA, self.INS_GET_STATUS, 0x00, 0x00])
        response, sw1, sw2 = self.transmit(apdu)

        status = {}
        if sw1 == 0x90 and sw2 == 0x00:
            r = response
            if len(r) >= 4:
                status["protocol_major"] = r[0]
                status["protocol_minor"] = r[1]
                status["applet_major"] = r[2]
                status["applet_minor"] = r[3]
                self.protocol_version = (r[0] << 8) + r[1]
            if len(r) >= 8:
                self._pin_attempts_left = r[4]
                status["pin0_tries"] = r[4]
                status["puk0_tries"] = r[5]
                self._pin_attempts_max = 5  # default
            if len(r) >= 10:
                status["is_seeded"] = r[9] != 0
            if len(r) >= 11:
                self.setup_done = r[10] != 0
                status["setup_done"] = self.setup_done
            if len(r) >= 12:
                self.needs_secure_channel = r[11] != 0
                status["needs_secure_channel"] = self.needs_secure_channel
        elif sw1 == 0x9C and sw2 == 0x04:
            self.setup_done = False
            status["setup_done"] = False
        else:
            self._check_sw(sw1, sw2, "get_status")

        return status

    @property
    def version(self):
        """Return version string from protocol version."""
        major = (self.protocol_version >> 8) & 0xFF
        minor = self.protocol_version & 0xFF
        return "%d.%d" % (major, minor)

    @property
    def platform(self):
        return "JavaCard OS"

    def seedkeeper_get_status(self):
        """
        Get SeedKeeper-specific status.

        Returns:
            dict with nb_secrets, total_memory, free_memory
        """
        apdu = bytes([self.CLA, self.INS_SEEDKEEPER_STATUS, 0x00, 0x00])
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "seedkeeper_get_status")

        status = {}
        if len(response) >= 6:
            status["nb_secrets"] = (response[0] << 8) + response[1]
            status["total_memory"] = (response[2] << 8) + response[3]
            status["free_memory"] = (response[4] << 8) + response[5]
        return status

    # ─── Secure channel ──────────────────────────────────────────

    def initiate_secure_channel(self):
        """
        Establish a secure channel with the card using ECDH.
        Must be called after select() and before any PIN/secret operations.
        """
        pubkey = self.sc.generate_keypair()
        apdu = bytes([self.CLA, self.INS_INIT_SECURE_CHANNEL, 0x00, 0x00,
                       len(pubkey)]) + pubkey
        response, sw1, sw2 = self._raw_transmit(apdu)
        self._check_sw(sw1, sw2, "initiate_secure_channel")

        # Parse the card's pubkey from response
        # Format: [coordx_size(2) | coordx | sig_size(2) | sig_der]
        peer_pubkey_bytes = self._parse_peer_pubkey(response)
        self.sc.initiate(peer_pubkey_bytes)

    def _parse_peer_pubkey(self, response):
        """
        Parse the card's public key from the secure channel init response.

        Response format: [coordx_size(2) | coordx(32) | sig_size(2) | sig]
        Returns 33-byte compressed pubkey (0x02 + x).
        """
        if len(response) < 4:
            raise SeedKeeperError("Invalid secure channel response")
        coordx_size = (response[0] << 8) + response[1]
        if len(response) < 2 + coordx_size:
            raise SeedKeeperError("Invalid coordx in secure channel response")
        coordx = bytes(response[2:2 + coordx_size])
        # Return compressed key with 0x02 prefix
        # For ECDH shared secret, only x-coordinate matters
        return b"\x02" + coordx

    # ─── PIN management ──────────────────────────────────────────

    def verify_pin(self, pin):
        """
        Verify the user's PIN.

        Args:
            pin: PIN string

        Returns:
            True if PIN is correct

        Raises:
            SeedKeeperError on wrong PIN or blocked card
        """
        pin_bytes = list(pin.encode("utf-8"))
        apdu = bytes([self.CLA, self.INS_VERIFY_PIN, 0x00, 0x00,
                       len(pin_bytes)]) + bytes(pin_bytes)

        # PIN verification must go through secure channel manually
        # to avoid the auto-PIN-verify loop in transmit()
        if self.needs_secure_channel:
            enc_apdu = self._encrypt_apdu(apdu)
            response, sw1, sw2 = self._raw_transmit(enc_apdu)
        else:
            response, sw1, sw2 = self._raw_transmit(apdu)

        if sw1 == 0x90 and sw2 == 0x00:
            self._pin_set = True
            return True
        elif sw1 == 0x63 and (sw2 & 0xC0) == 0xC0:
            # Wrong PIN, remaining tries in lower bits
            self._pin_attempts_left = sw2 & ~0xC0
            raise SeedKeeperError(
                "Wrong PIN! %d tries remaining" % self._pin_attempts_left
            )
        elif sw1 == 0x9C and sw2 == 0x02:
            # Wrong PIN (legacy)
            raise SeedKeeperError("Wrong PIN!")
        elif sw1 == 0x9C and sw2 == 0x0C:
            # PIN blocked
            raise SeedKeeperError("PIN blocked! Use PUK to unblock.")
        else:
            self._check_sw(sw1, sw2, "verify_pin")
        return False

    def create_pin(self, pin_tries, pin, puk):
        """Create a new PIN (during card setup)."""
        pin_bytes = list(pin.encode("utf-8"))
        puk_bytes = list(puk.encode("utf-8"))
        data = [len(pin_bytes)] + pin_bytes + [len(puk_bytes)] + puk_bytes
        apdu = bytes([self.CLA, self.INS_CREATE_PIN, 0x00, pin_tries,
                       len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "create_pin")

    def change_pin(self, old_pin, new_pin):
        """Change the card's PIN."""
        old_bytes = list(old_pin.encode("utf-8"))
        new_bytes = list(new_pin.encode("utf-8"))
        data = [len(old_bytes)] + old_bytes + [len(new_bytes)] + new_bytes
        apdu = bytes([self.CLA, self.INS_CHANGE_PIN, 0x00, 0x00,
                       len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)

        if sw1 == 0x63 and (sw2 & 0xC0) == 0xC0:
            self._pin_attempts_left = sw2 & ~0xC0
            raise SeedKeeperError(
                "Wrong PIN! %d tries remaining" % self._pin_attempts_left
            )
        self._check_sw(sw1, sw2, "change_pin")

    def setup(self, pin, puk="00000000"):
        """
        Initial card setup. Sets PIN, PUK, and memory configuration.

        Args:
            pin: user's chosen PIN string
            puk: PUK string for PIN unblocking (default: "00000000")
        """
        if self.setup_done:
            return

        default_pin = [0x4D, 0x75, 0x73, 0x63, 0x6C, 0x65, 0x30, 0x30]
        pin0 = list(pin.encode("utf-8"))
        ublk0 = list(puk.encode("utf-8"))
        pin1 = list(pin.encode("utf-8"))
        ublk1 = list(puk.encode("utf-8"))

        pin_tries0 = 5
        ublk_tries0 = 5
        pin_tries1 = 5
        ublk_tries1 = 5
        memsize = 0x0000
        memsize2 = 0x0000
        create_object_ACL = 0x01
        create_key_ACL = 0x01
        create_pin_ACL = 0x01

        data = (
            [len(default_pin)] + default_pin
            + [pin_tries0, ublk_tries0, len(pin0)] + pin0
            + [len(ublk0)] + ublk0
            + [pin_tries1, ublk_tries1, len(pin1)] + pin1
            + [len(ublk1)] + ublk1
            + [memsize >> 8, memsize & 0xFF,
               memsize2 >> 8, memsize2 & 0xFF]
            + [create_object_ACL, create_key_ACL, create_pin_ACL]
        )
        apdu = bytes([self.CLA, self.INS_SETUP, 0x00, 0x00,
                       len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "setup")
        self.setup_done = True

    @property
    def is_pin_set(self):
        """Check if a PIN has been set on the card."""
        if self._pin_set is None:
            try:
                status = self.get_status()
                self._pin_set = status.get("setup_done", False)
            except Exception:
                self._pin_set = False
        return self._pin_set

    @property
    def pin_attempts_left(self):
        if self._pin_attempts_left is None:
            self.get_status()
        return self._pin_attempts_left if self._pin_attempts_left is not None else 5

    @property
    def pin_attempts_max(self):
        return self._pin_attempts_max if self._pin_attempts_max is not None else 5

    # ─── Secret management ────────────────────────────────────────

    def _make_header(self, secret_type, export_rights, label, subtype=0x00):
        """
        Build a SeedKeeper secret header.

        Header format:
        [id(2) | type(1) | origin(1) | export_rights(1) |
         export_counters(3) | fingerprint(4) | rfu(2) |
         label_size(1) | label_bytes]
        """
        sid = [0x00, 0x00]
        origin = 0x01  # plaintext import
        export_counters = [0x00, 0x00, 0x00]
        fingerprint = [0x00, 0x00, 0x00, 0x00]
        rfu = [subtype, 0x00]
        label_bytes = list(label.encode("utf-8"))
        label_size = len(label_bytes)

        header = (
            sid
            + [secret_type, origin, export_rights]
            + export_counters
            + fingerprint
            + rfu
            + [label_size]
            + label_bytes
        )
        return bytes(header)

    def _parse_header(self, response):
        """
        Parse a SeedKeeper secret header from response bytes.

        Returns:
            dict with header fields
        """
        if len(response) < 14:
            raise SeedKeeperError("Header too short")

        s = BytesIO(bytes(response))
        sid = (s.read(1)[0] << 8) + s.read(1)[0]
        secret_type = s.read(1)[0]
        origin = s.read(1)[0]
        export_rights = s.read(1)[0]
        export_counters = s.read(3)
        fingerprint = hexlify(s.read(4)).decode()
        rfu = s.read(2)
        label_size = s.read(1)[0]
        label_bytes = s.read(label_size)
        try:
            label = label_bytes.decode("utf-8")
        except Exception:
            label = hexlify(label_bytes).decode()

        type_name = self.TYPE_LABELS.get(secret_type, "Unknown (0x%02X)" % secret_type)

        return {
            "id": sid,
            "type": secret_type,
            "type_name": type_name,
            "origin": origin,
            "export_rights": export_rights,
            "fingerprint": fingerprint,
            "label": label,
            "subtype": rfu[0] if len(rfu) > 0 else 0,
            "header_list": list(response[:14 + label_size]),
        }

    def import_secret(self, secret_type, export_rights, label, secret):
        """
        Import a secret into the SeedKeeper card (plaintext).

        Args:
            secret_type: type code (e.g., TYPE_BIP39_MNEMONIC)
            export_rights: export policy (e.g., EXPORT_PLAINTEXT)
            label: human-readable label string
            secret: secret bytes to store

        Returns:
            (sid, fingerprint) tuple
        """
        secret_list = list(secret)
        secret_size = len(secret_list)
        pad_size = 16 - (secret_size % 16)
        padded_secret_size = secret_size + pad_size

        # OP_INIT: send header + padded_secret_size
        header = self._make_header(secret_type, export_rights, label)
        # Strip the 2-byte id prefix (card assigns its own id)
        header_data = list(header[2:])
        data = header_data + [(padded_secret_size >> 8) & 0xFF,
                               padded_secret_size & 0xFF]
        apdu = bytes([self.CLA, self.INS_IMPORT_SECRET, 0x01,
                       self.OP_INIT, len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "import_secret INIT")

        # OP_PROCESS: send secret in chunks
        chunk_size = 128
        offset = 0
        remaining = len(secret_list)

        while remaining > chunk_size:
            chunk_data = ([(chunk_size >> 8) & 0xFF, chunk_size & 0xFF]
                          + secret_list[offset:offset + chunk_size])
            apdu = bytes([self.CLA, self.INS_IMPORT_SECRET, 0x01,
                           self.OP_PROCESS, len(chunk_data)]) + bytes(chunk_data)
            response, sw1, sw2 = self.transmit(apdu)
            self._check_sw(sw1, sw2, "import_secret PROCESS")
            offset += chunk_size
            remaining -= chunk_size

        # OP_FINAL: send last chunk
        last_chunk = secret_list[offset:]
        last_size = len(last_chunk)
        final_data = [(last_size >> 8) & 0xFF, last_size & 0xFF] + last_chunk
        apdu = bytes([self.CLA, self.INS_IMPORT_SECRET, 0x01,
                       self.OP_FINAL, len(final_data)]) + bytes(final_data)
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "import_secret FINAL")

        # Parse response: [id(2) | fingerprint(4)]
        sid = (response[0] << 8) + response[1]
        fingerprint = hexlify(bytes(response[2:6])).decode()
        return (sid, fingerprint)

    def export_secret(self, sid):
        """
        Export a secret from the SeedKeeper card (plaintext).

        Args:
            sid: secret ID to export

        Returns:
            dict with secret data including 'secret_bytes', 'header', etc.
        """
        data = [(sid >> 8) & 0xFF, sid & 0xFF]

        # OP_INIT: request export
        apdu = bytes([self.CLA, self.INS_EXPORT_SECRET, 0x01,
                       self.OP_INIT, len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)

        if sw1 == 0x9C and sw2 == 0x31:
            raise SeedKeeperError("Export not allowed by card policy")
        if sw1 == 0x9C and sw2 == 0x08:
            raise SeedKeeperError("Secret not found")
        self._check_sw(sw1, sw2, "export_secret INIT")

        # Parse header from INIT response
        header = self._parse_header(response)

        # OP_PROCESS: read secret data in chunks
        secret = bytearray()
        while True:
            apdu = bytes([self.CLA, self.INS_EXPORT_SECRET, 0x01,
                           self.OP_PROCESS, len(data)]) + bytes(data)
            response, sw1, sw2 = self.transmit(apdu)
            self._check_sw(sw1, sw2, "export_secret PROCESS")

            # Parse chunk: [chunk_size(2) | chunk_data | optional: sig_size(2) | sig]
            chunk_size = (response[0] << 8) + response[1]
            chunk = response[2:2 + chunk_size]
            secret.extend(chunk)

            # Check if this is the last chunk (signature follows)
            if chunk_size + 2 < len(response):
                # Signature present = last chunk
                break

        header["secret_bytes"] = bytes(secret)
        return header

    def list_secret_headers(self):
        """
        List all secrets stored on the card.

        Returns:
            list of header dicts with id, type, label, etc.
        """
        headers = []

        # OP_INIT: start listing
        apdu = bytes([self.CLA, self.INS_LIST_SECRET_HEADERS, 0x00,
                       self.OP_INIT])
        response, sw1, sw2 = self.transmit(apdu)

        while sw1 == 0x90 and sw2 == 0x00:
            try:
                header = self._parse_header(response)
                headers.append(header)
            except Exception:
                pass

            # OP_PROCESS: get next header
            apdu = bytes([self.CLA, self.INS_LIST_SECRET_HEADERS, 0x00,
                           self.OP_PROCESS])
            response, sw1, sw2 = self.transmit(apdu)

        # 0x9C12 = no more objects (expected end condition)
        if not (sw1 == 0x9C and sw2 == 0x12):
            if sw1 != 0x90 or sw2 != 0x00:
                # Ignore non-fatal errors during listing
                pass

        return headers

    def reset_secret(self, sid):
        """
        Delete a secret from the card.

        Args:
            sid: secret ID to delete

        Returns:
            True if successful
        """
        data = [(sid >> 8) & 0xFF, sid & 0xFF]
        apdu = bytes([self.CLA, self.INS_RESET_SECRET, 0x00, 0x00,
                       len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)

        if sw1 == 0x9C and sw2 == 0x08:
            raise SeedKeeperError("Secret not found")
        self._check_sw(sw1, sw2, "reset_secret")
        return True

    def generate_masterseed(self, seed_size, export_rights, label):
        """
        Generate a random master seed on the card.

        Args:
            seed_size: size in bytes (e.g., 16 for 12 words, 32 for 24 words)
            export_rights: export policy
            label: human-readable label

        Returns:
            (sid, fingerprint) tuple
        """
        label_bytes = list(label.encode("utf-8"))
        data = [len(label_bytes)] + label_bytes
        apdu = bytes([self.CLA, self.INS_GENERATE_MASTERSEED,
                       seed_size, export_rights, len(data)]) + bytes(data)
        response, sw1, sw2 = self.transmit(apdu)
        self._check_sw(sw1, sw2, "generate_masterseed")

        sid = (response[0] << 8) + response[1]
        fingerprint = hexlify(bytes(response[2:6])).decode()
        return (sid, fingerprint)

    def ping(self):
        """Quick check that the card is responsive."""
        try:
            self.get_status()
        except Exception:
            raise SeedKeeperError("Card not responding")
