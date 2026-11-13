import asyncio
import forgebox
import hmac
import lvgl as lv
from embit import bip39
from hashlib import pbkdf2_hmac
from helpers import aead_decrypt, aead_encrypt, tagged_hash
from rng import get_random_bytes
from gui.screens import Alert, Menu, PinScreen, Prompt
import platform
from keystore.core import KeyStoreError, PinError
from errors import BaseError
from forgebox_keystore import VolatileKeyStore

MAGIC = b'SFP1'
HEADER_SIZE = 24
ENCRYPTED_RECORD_SIZE = 145
RECORD_SIZE = ENCRYPTED_RECORD_SIZE + 32
NO_WORD_KEY = b'\xff' * 32
WORD_PREFIX_LENGTH = 2
FAILURE_LIMIT = 8
PIN_ROUNDS = 600000


def derive_pin_key(pin, salt):
    return pbkdf2_hmac('sha256', pin.encode(), salt, PIN_ROUNDS, 32)


class AntiPhishingPinScreen(PinScreen):
    def cb(self, obj, event):
        if event != lv.EVENT.RELEASED:
            return
        digit = obj.get_active_btn_text()
        if digit == lv.SYMBOL.CLOSE:
            self.reset()
        elif digit and digit != ' ':
            self.pin.add_text(digit)
            if self.get_word is not None:
                self.words.set_text(self.get_word(self.pin.get_text()[:WORD_PREFIX_LENGTH]))


class FlashPinKeyStore(VolatileKeyStore):
    NAME = 'ForgeBox flash PIN'

    def __init__(self):
        super().__init__()
        self.slot = None
        self.generation = 0
        self.record = None
        self.failures = 0
        self.word_key = None

    @property
    def is_locked(self):
        return self.secret is None

    @property
    def is_pin_set(self):
        return self.slot is not None

    def device_backup_key(self):
        if self.is_locked:
            return super().device_backup_key()
        return tagged_hash('forgebox seed backup', self.enc_secret)

    async def init(self, show_fn, show_loader):
        self.show = show_fn
        self.show_loader = show_loader
        if self.initialized:
            return
        newest = None
        try:
            forgebox.wallet_flash_resume()
        except OSError:
            raise KeyStoreError('Wallet storage unavailable or pending wipe could not finish')
        # Check both slots before allowing first-time setup. A read error or
        # foreign sector must never look like an uninitialized PIN.
        for slot in range(2):
            try:
                data = forgebox.pin_flash_read(slot)
            except OSError:
                raise KeyStoreError('PIN flash unavailable or reserved space already in use')
            if data[RECORD_SIZE] == 0xff:
                continue
            generation = int.from_bytes(data[4:8], 'little')
            if data[:4] != MAGIC or data[RECORD_SIZE] != 0 or not generation or (
                    newest is not None and generation == newest[0]):
                raise KeyStoreError('PIN record is damaged')
            if newest is None or generation > newest[0]:
                newest = (generation, slot, data)
        if newest is not None:
            self.generation, self.slot, data = newest
            self.record = data[:RECORD_SIZE]
            word_key = data[ENCRYPTED_RECORD_SIZE:RECORD_SIZE]
            self.word_key = None if word_key == NO_WORD_KEY else word_key
            self.failures = sum(byte != 0xff for byte in data[RECORD_SIZE + 1:])
        self.initialized = True

    def save_pin(self, pin, secret):
        if not 6 <= len(pin) <= 16 or not all(digit in '0123456789' for digit in pin):
            raise PinError('Use a PIN of 6 to 16 digits')
        generation = self.generation + 1
        if generation > 0xffffffff:
            raise KeyStoreError('PIN record sequence is exhausted')
        slot = 1 - self.slot if self.slot is not None else 0
        salt = get_random_bytes(16)
        header = MAGIC + generation.to_bytes(4, 'little') + salt
        word_key = self.word_key or tagged_hash('forgebox PIN words', get_random_bytes(32))
        record = header + aead_encrypt(derive_pin_key(pin, salt), header, secret) + word_key
        try:
            forgebox.pin_flash_write(slot, record)
        except OSError:
            raise KeyStoreError('Could not save the PIN to flash')
        self.slot, self.generation, self.record, self.failures = slot, generation, record, 0
        self.word_key = word_key
        self.retire_other_pin()

    def retire_other_pin(self):
        try:
            forgebox.pin_flash_retire(1 - self.slot)
        except OSError:
            raise KeyStoreError('New PIN record is active, but the old flash record could not be cleared')

    def verify_pin(self, pin):
        header = self.record[:HEADER_SIZE]
        key = derive_pin_key(pin, header[8:])
        try:
            authenticated, secret = aead_decrypt(
                self.record[HEADER_SIZE:ENCRYPTED_RECORD_SIZE], key)
        except Exception as error:
            if str(error) != 'Invalid HMAC':
                raise KeyStoreError('PIN record is damaged')
            if self.failures < FAILURE_LIMIT:
                try:
                    forgebox.pin_flash_failure(self.slot, self.failures)
                except OSError:
                    raise KeyStoreError('Could not update the PIN attempt counter')
                self.failures += 1
            raise PinError('Incorrect PIN')
        if authenticated != header or len(secret) != 32:
            raise KeyStoreError('PIN record is damaged')
        if self.failures or self.word_key is None:
            self.save_pin(pin, secret)
        else:
            self.retire_other_pin()
        return secret

    async def enter_pin(self, title, note=None, cancel=False):
        get_word = None
        if self.word_key is not None:
            get_word = self.get_auth_words
        screen = AntiPhishingPinScreen(title=title, note=note, get_word=get_word,
                                       with_cancel=cancel)
        screen.pin.set_max_length(16)
        return await self.show(screen)

    def get_auth_words(self, prefix):
        def word(value):
            digest = hmac.new(self.word_key, value, digestmod='sha256').digest()
            return bip39.WORDLIST[int.from_bytes(digest[:2], 'big') % len(bip39.WORDLIST)]

        device_word = word(b'')
        if len(prefix) < WORD_PREFIX_LENGTH:
            return device_word
        return device_word + ' ' + word(prefix[:WORD_PREFIX_LENGTH].encode())

    async def show_auth_words(self, pin):
        await self.show(Alert('Your anti-phishing words',
                              self.get_auth_words(pin[:WORD_PREFIX_LENGTH]) +
                              '\n\nCheck both words after the first two PIN digits. '
                              'Stop if they differ.'))

    async def choose_pin(self):
        while True:
            pin = await self.enter_pin('Choose a startup PIN', 'Use at least six digits.')
            if len(pin) < 6:
                await self.show(Alert('PIN too short', 'Enter at least six digits.'))
                continue
            confirm = await self.enter_pin('Confirm startup PIN')
            if pin == confirm:
                return pin
            await self.show(Alert('PINs do not match', 'Enter the same PIN twice.'))

    async def wait_before_attempt(self):
        if self.failures:
            delay = min(30, 1 << self.failures)
            self.show_loader('Wait %d seconds before retrying.' % delay, 'PIN delay')
            await asyncio.sleep_ms(delay * 1000)

    async def unlock(self):
        if not self.is_pin_set:
            pin = await self.choose_pin()
            self.show_loader('Writing the encrypted PIN record.', 'Setting startup PIN')
            secret = get_random_bytes(32)
            self.save_pin(pin, secret)
            await self.show_auth_words(pin)
        elif self.is_locked:
            while True:
                await self.wait_before_attempt()
                learn_words = self.word_key is None
                note = 'Unlock once to set up your anti-phishing words.' if learn_words else None
                pin = await self.enter_pin('Enter startup PIN', note=note, cancel=True)
                if pin is None:
                    await self.locked_options()
                    continue
                self.show_loader('Checking the startup PIN.', 'Unlocking')
                try:
                    secret = self.verify_pin(pin)
                    break
                except PinError as error:
                    await self.show(Alert('PIN error', str(error)))
            if learn_words:
                await self.show_auth_words(pin)
        else:
            return
        self.secret = secret
        self.enc_secret = tagged_hash('enc', secret)

    async def locked_options(self):
        action = await self.show(Menu([(1, 'Wipe wallet storage'), (2, 'Power off')],
                                      title='Wallet locked', last=(0, 'Back to PIN')))
        if action == 1 and await self.show(Prompt(
                'Erase wallet storage?', 'This deletes the startup PIN, saved seeds, and wallet descriptors. '
                'Device-encrypted microSD backups will become unreadable.')):
            platform.wipe()
        elif action == 2:
            try:
                platform.power_off()
            except BaseError as error:
                await self.show(Alert('Power off', str(error)))

    def lock(self):
        self.mnemonic = self.root = self.fingerprint = self.slip77_key = None
        self.idkey = self.secret = self.enc_secret = self._userkey = None

    async def change_pin(self):
        await self.wait_before_attempt()
        old = await self.enter_pin('Enter current PIN', cancel=True)
        if old is None:
            return
        self.show_loader('Checking the current PIN.', 'Change PIN')
        secret = self.verify_pin(old)
        new = await self.choose_pin()
        self.show_loader('Writing the encrypted PIN record.', 'Change PIN')
        self.save_pin(new, secret)
        await self.show_auth_words(new)
