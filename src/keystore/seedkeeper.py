"""
SeedKeeper KeyStore for Specter-DIY.

Uses a SeedKeeper (or Satochip) smartcard to store multiple BIP39 mnemonics.
The card acts purely as encrypted storage — all BIP32 key derivation and
transaction signing happens on the MCU (same security model as MemoryCard).

Key features:
- Multiple seeds can be stored on a single card, each with a label
- PIN-protected access via the card's secure element
- Secure channel (ECDH + AES) for all communication with the card
- Compatible with the Satochip SeedKeeper ecosystem
"""
from .core import KeyStoreError, PinError
from .ram import RAMKeyStore
from .javacard.util import get_connection
from .satochip.applet import SeedKeeperApplet, SeedKeeperError
from platform import CriticalErrorWipeImmediately
import platform
from embit import bip39
from helpers import tagged_hash
import hmac
from gui.screens import Alert, Progress, Menu, Prompt
import asyncio
from binascii import hexlify
import lvgl as lv


class SeedKeeperKeyStore(RAMKeyStore):
    """
    KeyStore that stores multiple BIP39 mnemonics on a SeedKeeper smartcard.

    The SeedKeeper card provides:
    - PIN-protected secure element storage
    - Multiple secret slots with labels
    - Secure channel for APDU communication

    The MCU provides:
    - BIP32 key derivation from mnemonic
    - Transaction signing (PSBT, messages)
    - Wallet management

    This separation means the card never sees private keys in derived form,
    only the root mnemonic/entropy. The card cannot sign transactions.
    """

    NAME = "SeedKeeper"
    COLOR = "00CAF1"
    NOTE = """Stores multiple recovery phrases on a PIN-protected SeedKeeper smartcard.
Keys are loaded into device memory for signing when needed."""
    storage_button = "SeedKeeper storage"
    load_button = "Load key from SeedKeeper"
    # Reuse the smartcard connection from javacard/util.py.
    # Only one keystore is active at a time (selected during boot),
    # so there's no concurrent access. The SELECT command switches
    # the active applet on the card for whichever keystore is in use.
    connection = get_connection()

    def __init__(self):
        super().__init__()
        self.applet = SeedKeeperApplet(self.connection)
        self._is_key_saved = False
        self.connected = False
        # Currently loaded secret ID (to track which key is active)
        self._active_sid = None
        # Cached list of secrets on card
        self._secret_headers = None
        # Is PIN verified for current session
        self._pin_verified = False

    # ─── Availability detection ──────────────────────────────────

    @classmethod
    def is_available(cls):
        """Check if a SeedKeeper card is inserted and responsive."""
        if not cls.connection.isCardInserted():
            return False
        try:
            cls.connection.connect(cls.connection.T1_protocol)
            applet = SeedKeeperApplet(cls.connection)
            applet.select()
            # If select succeeds, we have a compatible card
            cls.connection.disconnect()
            return True
        except Exception as e:
            print(e)
            return False

    # ─── Anti-phishing words ─────────────────────────────────────

    def get_auth_word(self, pin_part):
        """
        Generate anti-phishing word using internal secret and card identity.
        """
        key = tagged_hash("auth", self.secret)
        h = hmac.new(key, pin_part, digestmod="sha256").digest()
        word_number = int.from_bytes(h[:2], "big") % len(bip39.WORDLIST)
        return bip39.WORDLIST[word_number]

    # ─── PIN properties ──────────────────────────────────────────

    @property
    def is_pin_set(self):
        return self.applet.is_pin_set

    @property
    def pin_attempts_left(self):
        return self.applet.pin_attempts_left

    @property
    def pin_attempts_max(self):
        return self.applet.pin_attempts_max

    @property
    def is_locked(self):
        return not self._pin_verified

    @property
    def is_ready(self):
        return (
            self.connected
            and self._pin_verified
            and (self.fingerprint is not None)
        )

    # ─── PIN operations ──────────────────────────────────────────

    def _unlock(self, pin):
        """Verify PIN with the SeedKeeper card."""
        try:
            self.applet.verify_pin(pin)
            self._pin_verified = True
        except SeedKeeperError as e:
            msg = str(e)
            if "Wrong PIN" in msg:
                raise PinError(msg)
            elif "blocked" in msg.lower():
                raise CriticalErrorWipeImmediately(
                    "Card PIN blocked!\nToo many failed attempts."
                )
            else:
                raise PinError(msg)
        # After unlock, refresh the list of stored secrets
        self._refresh_secret_list()

    def _change_pin(self, old_pin, new_pin):
        """Change the card's PIN."""
        self.lock()
        self._unlock(old_pin)
        try:
            self.applet.change_pin(old_pin, new_pin)
            self._pin_verified = True
        except SeedKeeperError as e:
            raise PinError(str(e))

    def _set_pin(self, pin):
        """Set initial PIN (during first-time setup)."""
        if self.is_pin_set:
            raise KeyStoreError("PIN is already set")
        try:
            self.applet.setup(pin)
            self.applet.verify_pin(pin)
            self._pin_verified = True
        except SeedKeeperError as e:
            raise KeyStoreError("Failed to set PIN: %s" % str(e))

    def lock(self):
        """Lock the keystore, requiring PIN to unlock."""
        self._pin_verified = False
        return True

    @property
    def userkey(self):
        """Unique key per card for user isolation."""
        if self._userkey is None:
            self._userkey = tagged_hash("userkey", self.secret)
        return self._userkey

    # ─── Card connection management ──────────────────────────────

    async def check_card(self, check_pin=False):
        """Ensure card is inserted, connected, and optionally PIN-verified."""
        if not self.connection.isCardInserted():
            scr = Progress(
                "SeedKeeper not inserted",
                "Please insert the SeedKeeper card...",
                button_text=None,
            )
            asyncio.create_task(self.wait_for_card(scr))
            await self.show(scr)

        if not self.connected:
            self.show_loader(title="Connecting to SeedKeeper...")
            try:
                self.connection.connect(self.connection.T1_protocol)
            except Exception:
                raise KeyStoreError("Failed to communicate with card")
            try:
                self.applet.select()
            except Exception:
                raise KeyStoreError("No SeedKeeper applet found on card")
            # Get card status
            status = self.applet.get_status()
            # Set up secure channel if required
            if self.applet.needs_secure_channel:
                self.applet.initiate_secure_channel()
            self.connected = True

        if check_pin and self.is_locked:
            pin = await self.get_pin()
            self._unlock(pin)

    async def wait_for_card(self, scr):
        """Wait for card insertion."""
        while not self.connection.isCardInserted():
            await asyncio.sleep_ms(30)
            scr.tick(5)
        if scr.waiting:
            scr.waiting = False

    # ─── Secret list management ──────────────────────────────────

    def _refresh_secret_list(self):
        """Refresh the cached list of secrets on the card."""
        try:
            self._secret_headers = self.applet.list_secret_headers()
            self._is_key_saved = any(
                h["type"] in (SeedKeeperApplet.TYPE_BIP39_MNEMONIC,
                              SeedKeeperApplet.TYPE_MASTERSEED)
                for h in self._secret_headers
            )
        except Exception:
            self._secret_headers = []
            self._is_key_saved = False

    def _get_mnemonic_headers(self):
        """Get only the BIP39 mnemonic / masterseed headers."""
        if self._secret_headers is None:
            self._refresh_secret_list()
        return [
            h for h in self._secret_headers
            if h["type"] in (SeedKeeperApplet.TYPE_BIP39_MNEMONIC,
                             SeedKeeperApplet.TYPE_MASTERSEED)
        ]

    # ─── Save / Load / Delete operations ─────────────────────────

    async def save_mnemonic(self):
        """Save the current mnemonic to the SeedKeeper card."""
        await self.check_card(check_pin=True)

        if self.mnemonic is None:
            raise KeyStoreError("No mnemonic loaded")

        # Ask user for a label
        from gui.screens import InputScreen
        scr = InputScreen(
            "Name this key",
            "Give this seed a unique name to identify it on the card.",
            suggestion=self.mnemonic.split()[0],
            min_length=1,
            strip=True,
        )
        await self.show(scr)
        label = scr.get_value()
        if label is None:
            return

        # Check for duplicate labels
        existing = self._get_mnemonic_headers()
        for h in existing:
            if h["label"] == label:
                if not await self.show(Prompt(
                    "Label exists",
                    "A key named '%s' already exists on the card.\n\n"
                    "Save anyway with the same name?" % label
                )):
                    return

        # Choose export rights
        allow_plaintext = await self.show(Prompt(
            "Export policy",
            "Allow plaintext export of this key?\n\n"
            "If yes, the key can be read from the card on any compatible device.\n\n"
            "If no, the key will be locked to encrypted export only.",
            confirm_text="Allow plaintext",
            cancel_text="Encrypted only",
        ))
        export_rights = (SeedKeeperApplet.EXPORT_PLAINTEXT
                         if allow_plaintext
                         else SeedKeeperApplet.EXPORT_ENCRYPTED)

        # Store the mnemonic string as the secret
        self.show_loader("Saving key to SeedKeeper...")
        secret_bytes = self.mnemonic.encode("utf-8")
        sid, fingerprint = self.applet.import_secret(
            SeedKeeperApplet.TYPE_BIP39_MNEMONIC,
            export_rights,
            label,
            secret_bytes,
        )
        self._is_key_saved = True
        self._active_sid = sid
        self._refresh_secret_list()

        await self.show(Alert(
            "Success!",
            "Key saved to SeedKeeper.\n\nLabel: %s\nID: %d" % (label, sid),
            button_text="OK",
        ))

    async def load_mnemonic(self, sid=None):
        """Load a mnemonic from the SeedKeeper card."""
        await self.check_card(check_pin=True)

        if sid is None:
            sid = await self._select_secret()
            if sid is None:
                return False

        self.show_loader("Loading key from SeedKeeper...")
        try:
            result = self.applet.export_secret(sid)
        except SeedKeeperError as e:
            raise KeyStoreError("Failed to load key: %s" % str(e))

        secret_bytes = result["secret_bytes"]
        secret_type = result["type"]

        if secret_type == SeedKeeperApplet.TYPE_BIP39_MNEMONIC:
            # Mnemonic stored as UTF-8 string
            mnemonic = secret_bytes.decode("utf-8").strip()
        elif secret_type == SeedKeeperApplet.TYPE_MASTERSEED:
            # Raw entropy bytes — convert to mnemonic
            mnemonic = bip39.mnemonic_from_bytes(secret_bytes)
        else:
            raise KeyStoreError(
                "Unsupported secret type: %s" % result.get("type_name", "unknown")
            )

        self.set_mnemonic(mnemonic, "")
        self._active_sid = sid
        return True

    async def delete_mnemonic(self):
        """Delete a secret from the SeedKeeper card."""
        await self.check_card(check_pin=True)

        sid = await self._select_secret()
        if sid is None:
            return

        # Get the header for the selected secret
        headers = self._get_mnemonic_headers()
        label = "unknown"
        for h in headers:
            if h["id"] == sid:
                label = h["label"]
                break

        if not await self.show(Prompt(
            "Delete key?",
            "Delete '%s' (ID: %d) from the SeedKeeper?\n\n"
            "This cannot be undone!" % (label, sid),
        )):
            return

        self.show_loader("Deleting key from SeedKeeper...")
        try:
            self.applet.reset_secret(sid)
        except SeedKeeperError as e:
            raise KeyStoreError("Failed to delete: %s" % str(e))

        self._refresh_secret_list()

        # If we deleted the active key, clear it
        if self._active_sid == sid:
            self._active_sid = None

        await self.show(Alert(
            "Deleted",
            "Key '%s' has been removed from the card." % label,
            button_text="OK",
        ))

    async def _select_secret(self):
        """Show a menu to select one of the stored secrets. Returns sid or None."""
        headers = self._get_mnemonic_headers()
        if not headers:
            await self.show(Alert(
                "No keys",
                "No keys found on the SeedKeeper card.",
                button_text="OK",
            ))
            return None

        buttons = [(None, "Select a key")]
        for h in headers:
            type_label = h.get("type_name", "Key")
            fp = h.get("fingerprint", "")
            text = "%s" % h["label"]
            if fp and fp != "00000000":
                text += " [%s]" % fp[:8]
            buttons.append((h["id"], text))

        sid = await self.show(Menu(buttons, last=(None, "Cancel")))
        return sid

    # ─── Properties ──────────────────────────────────────────────

    @property
    def is_key_saved(self):
        return self._is_key_saved

    # ─── Initialization ──────────────────────────────────────────

    async def init(self, show_fn, show_loader):
        """Initialize the keystore: connect to card, load secrets."""
        self.show_loader = show_loader
        self.show = show_fn
        platform.maybe_mkdir(self.path)
        self.load_secret(self.path)
        await self.check_card()
        await super().init(show_fn, show_loader)

    # ─── Card info ───────────────────────────────────────────────

    @property
    def hexid(self):
        """Short hex identifier for this card."""
        return hexlify(tagged_hash("seedkeeper", self.secret)[:4]).decode()

    async def show_card_info(self):
        """Display card information."""
        try:
            sk_status = self.applet.seedkeeper_get_status()
        except Exception:
            sk_status = {}

        props = [
            "\n#7f8fa4 CARD: #",
            "Type: %s" % (self.applet.card_type or "Unknown"),
            "Version: %s" % self.applet.version,
        ]

        if sk_status:
            props.append("\n#7f8fa4 STORAGE: #")
            props.append("Secrets stored: %d" % sk_status.get("nb_secrets", 0))
            total = sk_status.get("total_memory", 0)
            free = sk_status.get("free_memory", 0)
            if total > 0:
                props.append("Memory: %d / %d bytes free" % (free, total))

        headers = self._get_mnemonic_headers()
        if headers:
            props.append("\n#7f8fa4 KEYS: #")
            for h in headers:
                sid_marker = " *" if h["id"] == self._active_sid else ""
                props.append("%s (ID:%d)%s" % (h["label"], h["id"], sid_marker))

        note = "Card ID: %s" % self.hexid
        scr = Alert("SeedKeeper Info", "\n\n".join(props), note=note)
        scr.message.set_recolor(True)
        await self.show(scr)

    # ─── Storage menu ────────────────────────────────────────────

    async def storage_menu(self):
        """
        Manage SeedKeeper storage. Returns True if a new key was loaded.

        Provides options to:
        - Save current mnemonic to card
        - Load a mnemonic from card
        - Delete a mnemonic from card
        - View card info
        - Switch to a different card
        """
        enabled = self.connection.isCardInserted()
        has_keys = enabled and self._is_key_saved

        buttons = [
            (None, "SeedKeeper storage"),
            (0, "Save key to card", enabled),
            (1, "Load key from card", has_keys),
            (2, "Delete key from card", has_keys),
            (3, "Use a different card", enabled),
            (4, lv.SYMBOL.SETTINGS + " Card info", enabled),
        ]

        while True:
            # Refresh button states
            has_keys = enabled and self._is_key_saved
            buttons[2] = (1, "Load key from card", has_keys)
            buttons[3] = (2, "Delete key from card", has_keys)
            note = "Card ID: %s" % self.hexid

            menuitem = await self.show(Menu(buttons, note=note, last=(255, None)))

            if menuitem == 255:
                return False
            elif menuitem == 0:
                await self.save_mnemonic()
            elif menuitem == 1:
                if await self.load_mnemonic():
                    await self.show(Alert(
                        "Success!",
                        "Key loaded from SeedKeeper.",
                        button_text="OK",
                    ))
                    return True
            elif menuitem == 2:
                await self.delete_mnemonic()
            elif menuitem == 3:
                if await self.show(Prompt(
                    "Switch card",
                    "To use a different SeedKeeper card, "
                    "you need to verify your PIN first.\n\n"
                    "Continue?"
                )):
                    self.lock()
                    await self.unlock()
                    self.lock()
                    self.connected = False
                    self._secret_headers = None
                    self._userkey = None
                    await self.show(Alert(
                        "Swap card",
                        "You can now insert another SeedKeeper card.",
                        button_text="Continue",
                    ))
                    await self.check_card(check_pin=True)
                    await self.unlock()
            elif menuitem == 4:
                await self.show_card_info()
