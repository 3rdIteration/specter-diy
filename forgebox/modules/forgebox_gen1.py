import forgebox
import hmac
import platform
from embit import bip39
from forgebox_pin import FAILURE_LIMIT, FlashPinKeyStore, derive_pin_key
from gui.screens import Alert, Prompt
from helpers import tagged_hash
from keystore.core import KeyStoreError, PinError
from rng import RNGError, get_random_bytes
from forgebox_version import PRODUCTION


class SecureElementKeyStore(FlashPinKeyStore):
    NAME = 'ForgeBox Gen 1 storage'
    salt = None
    seed_pin_context = None
    session_passphrase = ""

    def set_mnemonic(self, mnemonic=None, password=""):
        super().set_mnemonic(mnemonic, password)
        self.session_passphrase = password

    @property
    def is_pin_set(self):
        return self.salt is not None

    def set_pin_state(self, salt, remaining):
        self.salt = salt
        self.failures = FAILURE_LIMIT - remaining
        self.word_key = tagged_hash('ForgeBox Gen 1 PIN words', salt)

    async def init(self, show_fn, show_loader):
        self.show = show_fn
        self.show_loader = show_loader
        if self.initialized:
            return
        try:
            forgebox.wallet_flash_resume()
            if not PRODUCTION and any(any(byte != 0xff for byte in forgebox.pin_flash_read(slot))
                                      for slot in range(2)):
                await self.wipe_legacy_wallet()
            state = forgebox.gen1_state()
        except OSError:
            if PRODUCTION:
                raise KeyStoreError('Secure storage unavailable or occupied. If you used a development '
                                    'build, use its in-app wipe before starting fresh.')
            raise KeyStoreError('Gen 1 storage unavailable, occupied, or could not finish recovery')
        if state is not None:
            self.set_pin_state(*state)
        self.initialized = True

    async def wipe_legacy_wallet(self):
        while not await self.show(Prompt(
                'Existing flash wallet',
                'Wipe the old PIN and saved flash seed to start Gen 1 setup. '
                'Backups encrypted with the old device key will become unreadable.',
                confirm_text='Wipe & reset')):
            await self.show(Alert('Old wallet kept',
                                  'To recover the old wallet, power off and reinstall '
                                  'your previous Specter firmware.'))
        self.show_backup_status('Wiping old wallet', 'Erasing the flash PIN and saved seed.')
        try:
            forgebox.wallet_flash_wipe()
        except OSError:
            raise KeyStoreError('Could not verify the flash wipe. Restart to check storage.')

    async def unlock(self):
        if not self.is_locked:
            return
        if not self.is_pin_set:
            pin = await self.choose_pin()
            self.show_backup_status('Setting startup PIN', 'Creating secure-element account keys.')
            salt = get_random_bytes(22)
            pin_key = derive_pin_key(pin, salt)
            try:
                secret = forgebox.gen1_enroll(salt, pin_key)
            except OSError:
                raise KeyStoreError('Enrollment could not be verified. Restart to check storage.')
            self.set_pin_state(salt, FAILURE_LIMIT)
            await self.show_auth_words(pin)
        else:
            fingerprint = None
            if forgebox.fp_unlock_enabled():
                from forgebox_fp_unlock import FingerprintLogin
                fingerprint = FingerprintLogin(self)
                if await fingerprint.unlock():
                    return
            while True:
                await self.wait_before_attempt()
                pin = await self.enter_pin(
                    'Enter startup PIN',
                    'Attempts left: %d' % (FAILURE_LIMIT - self.failures), cancel=True)
                if pin is None:
                    if fingerprint is None:
                        await self.locked_options()
                    elif await fingerprint.unlock():
                        return
                    continue
                self.show_backup_status('Unlocking', 'Checking PIN with both secure elements.')
                pin_key = derive_pin_key(pin, self.salt)
                try:
                    result = forgebox.gen1_unlock(pin_key)
                except OSError:
                    raise KeyStoreError('Unlock could not be verified. Restart to check storage.')
                if isinstance(result, bytes):
                    secret = result
                    self.failures = 0
                    break
                if result == 0:
                    await self.show(Alert('Wallet storage wiped', 'The PIN attempt limit was reached.'))
                    platform.reboot()
                    return
                self.failures = FAILURE_LIMIT - result
                await self.show(Alert('Incorrect PIN', '%d attempts left.' % result))
        self.secret = secret
        self.enc_secret = tagged_hash('enc', secret)
        # Only retain a purpose-specific value keyed by the device secret.
        self.seed_pin_context = bytearray(hmac.new(
            secret, b'ForgeBox BIP39 PIN context v1\x00' + pin_key, digestmod='sha256').digest())

    def clear_loaded_seed(self):
        self.mnemonic = self.root = self.fingerprint = self.slip77_key = self.idkey = None
        self.session_passphrase = ""

    def lock(self):
        self.clear_loaded_seed()
        if self.seed_pin_context is not None:
            for i in range(len(self.seed_pin_context)):
                self.seed_pin_context[i] = 0
            self.seed_pin_context = None
        super().lock()

    def generate_mnemonic(self, num_words):
        if self.is_locked or self.seed_pin_context is None:
            raise RNGError('Unlock the device before generating a seed.')
        if num_words not in (12, 24):
            raise ValueError('Use 12 or 24 words')
        entropy = bytearray(32)
        selected = entropy
        try:
            failed = forgebox.gen1_seed_entropy(self.secret, self.seed_pin_context, entropy)
            if failed:
                sources = {1: 'MCU', 2: 'ATECC608B', 3: 'DS28S60'}
                raise RNGError('%s randomness check failed. Try again.' %
                               sources.get(failed, 'Hardware'))
            if num_words == 12:
                selected = entropy[:16]
            return bip39.mnemonic_from_bytes(selected)
        finally:
            for i in range(len(entropy)):
                entropy[i] = 0
            if selected is not entropy:
                for i in range(len(selected)):
                    selected[i] = 0

    async def change_pin(self):
        if not await self.show(Prompt('Change startup PIN',
                'Saved wallets will remain accessible. Fingerprint unlock will need to be enrolled again.')):
            return
        await self.wait_before_attempt()
        old = await self.enter_pin('Enter current PIN', cancel=True)
        if old is None:
            return
        new = await self.choose_pin()
        if new == old:
            raise PinError('Choose a different startup PIN')
        self.show_backup_status('Changing startup PIN', 'Checking and updating secure-element credentials.')
        old_key = bytearray(derive_pin_key(old, self.salt))
        new_key = bytearray(derive_pin_key(new, self.salt))
        try:
            status = forgebox.gen1_change_pin(old_key, new_key)
            if status == -2:
                self.failures = 0
                self.seed_pin_context[:] = hmac.new(self.secret,
                    b'ForgeBox BIP39 PIN context v1\x00' + bytes(new_key), digestmod='sha256').digest()
            elif status > 0:
                self.failures = FAILURE_LIMIT - status
                raise PinError('Incorrect PIN. %d attempts left.' % status)
            elif status == 0:
                await self.show(Alert('Wallet storage wiped', 'The PIN attempt limit was reached.'))
                platform.reboot()
                return
            else:
                await self.show(Alert('PIN change unconfirmed',
                    'The device will restart. Try the new PIN first. If rejected, try the previous PIN.'))
                platform.reboot()
                return
        finally:
            for key in (old_key, new_key):
                for i in range(len(key)):
                    key[i] = 0
        await self.show_auth_words(new)
