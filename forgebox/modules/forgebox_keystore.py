import os
import platform
import forgebox
import lvgl as lv
from binascii import hexlify
from hashlib import pbkdf2_hmac
from embit import bip39
from gui.screens import Alert, InputScreen, Menu, MnemonicScreen, Prompt
from keystore.core import KeyStoreError
from keystore.ram import RAMKeyStore
from errors import BaseError
from rng import get_random_bytes
from helpers import aead_decrypt, aead_encrypt, tagged_hash

from forgebox_version import PRODUCTION

LEGACY_FORMAT = b'FBS1'
SD_FORMAT = b'FBS2'
FLASH_FORMAT = b'SFS1'
CATALOG_FORMAT = b'FS01' if PRODUCTION else b'SFS2'
FLASH_SEED_LIMIT = 16
CATALOG_ENTRY_SIZE = 57
LEGACY_FLASH_NAME = 'Existing-flash-seed'
SALT_SIZE = 16
KDF_ROUNDS = 600000
MAX_BACKUP_SIZE = 512
SEED_SUFFIX = '.fbs'


def valid_label(label):
    chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_'
    return 0 < len(label) <= 24 and all(char in chars for char in label)


def backup_key(password, salt):
    return pbkdf2_hmac('sha256', password.encode(), salt, KDF_ROUNDS, 32)

class VolatileKeyStore(RAMKeyStore):
    NAME = 'RAM only'
    NOTE = 'Optional device-encrypted recovery phrase backups.'
    storage_button = 'Seed storage'
    load_button = 'Load saved seed'

    async def init(self, show_fn, show_loader):
        self.show = show_fn
        self.show_loader = show_loader
        if self.secret is None:
            self.secret = get_random_bytes(32)
            self.enc_secret = tagged_hash('enc', self.secret)
        self.initialized = True

    async def unlock(self):
        pass  # RAM-only session has no PIN and advertises no PIN protection.

    def lock(self):
        import platform
        platform.reboot()  # Discard VM heap, scratch and RAM filesystem together.

    async def change_pin(self):
        raise BaseError('This build has no device PIN')

    def saved_seed_names(self):
        if not platform.sdcard.is_present:
            return []
        with platform.sdcard:
            return sorted(name for name, kind, *_ in os.ilistdir('/sd')
                          if kind == 0x8000 and name.lower().endswith(SEED_SUFFIX)
                          and valid_label(name[:-len(SEED_SUFFIX)]))

    @property
    def is_key_saved(self):
        try:
            if self.flash_seed_record() is not None:
                return True
        except KeyStoreError:
            pass
        try:
            return bool(self.saved_seed_names())
        except OSError:
            return False

    async def backup_password(self, title, note):
        screen = InputScreen(title, note, min_length=1, max_length=128)
        screen.ta.set_pwd_mode(True)
        screen.ta.set_pwd_show_time(0)
        return await self.show(screen)

    def show_backup_status(self, title, message):
        self.show_loader(message, title)
        # PBKDF2 and flash/SD writes block the display loop. Flush the modal now.
        lv.refr_now(lv.disp_get_default())

    def device_backup_key(self):
        raise KeyStoreError('Unlock the device before using its backup key')

    def seal_seed(self, header):
        if not self.mnemonic:
            raise KeyStoreError('Load a recovery phrase first')
        entropy = bip39.mnemonic_to_bytes(self.mnemonic)
        padded = bytes([len(entropy)]) + entropy + bytes(32 - len(entropy))
        return header + aead_encrypt(self.device_backup_key(), header, padded)

    def open_device_seed(self, encrypted, header_size):
        header = encrypted[:header_size]
        key = self.device_backup_key()
        try:
            authenticated, padded = aead_decrypt(encrypted[header_size:], key)
        except Exception:
            raise KeyStoreError('Backup belongs to a different device key or is damaged')
        if authenticated != header or len(padded) != 33 or padded[0] not in (16, 20, 24, 28, 32):
            raise KeyStoreError('Invalid recovery phrase in backup')
        return bip39.mnemonic_from_bytes(padded[1:1 + padded[0]])

    def write_seed_backup(self, label):
        if not valid_label(label):
            raise KeyStoreError('Use 1–24 letters, digits, hyphens or underscores')
        encrypted = self.seal_seed(SD_FORMAT)
        filename = label + SEED_SUFFIX
        destination = '/sd/' + filename
        temporary = '/sd/fbs-' + hexlify(get_random_bytes(8)).decode() + '.tmp'
        with platform.sdcard:
            if platform.file_exists(destination):
                raise KeyStoreError('That backup name already exists')
            if platform.file_exists(temporary):
                raise KeyStoreError('Could not reserve a temporary backup file')
            try:
                with open(temporary, 'wb') as file:
                    if file.write(encrypted) != len(encrypted):
                        raise OSError('Incomplete backup write')
                with open(temporary, 'rb') as file:
                    if file.read(MAX_BACKUP_SIZE + 1) != encrypted:
                        raise OSError('Backup readback failed')
                os.rename(temporary, destination)
                platform.sync()
            finally:
                if platform.file_exists(temporary):
                    os.remove(temporary)
        return filename

    def read_seed_file(self, filename):
        if not filename.lower().endswith(SEED_SUFFIX) or not valid_label(filename[:-len(SEED_SUFFIX)]):
            raise KeyStoreError('Invalid backup filename')
        with platform.sdcard:
            with open('/sd/' + filename, 'rb') as file:
                encrypted = file.read(MAX_BACKUP_SIZE + 1)
        if len(encrypted) > MAX_BACKUP_SIZE:
            raise KeyStoreError('Invalid ForgeBox seed backup')
        return encrypted

    def open_legacy_seed(self, encrypted, password):
        header_size = len(LEGACY_FORMAT) + SALT_SIZE
        header = encrypted[:header_size]
        if len(encrypted) < header_size + 65 or not header.startswith(LEGACY_FORMAT):
            raise KeyStoreError('Invalid ForgeBox seed backup')
        key = backup_key(password, header[len(LEGACY_FORMAT):])
        try:
            adata, plaintext = aead_decrypt(encrypted[header_size:], key)
            mnemonic = plaintext.decode()
        except Exception:
            raise KeyStoreError('Wrong backup password or damaged backup')
        if adata != header or not bip39.mnemonic_is_valid(mnemonic):
            raise KeyStoreError('Invalid recovery phrase in backup')
        return mnemonic

    def flash_seed_record(self):
        latest = None
        for slot in range(2):
            try:
                data = forgebox.seed_backup_read(slot)
            except OSError:
                raise KeyStoreError('Seed flash unavailable or reserved space already in use')
            if data is None:
                continue
            generation = int.from_bytes(data[4:8], 'little')
            if data[:4] not in ((CATALOG_FORMAT,) if PRODUCTION else (FLASH_FORMAT, CATALOG_FORMAT)) or not generation or (
                    latest is not None and latest[0] == generation):
                raise KeyStoreError('Flash seed record is damaged')
            if latest is None or generation > latest[0]:
                latest = (generation, slot, data)
        return latest

    def flash_seed_entries(self, record):
        if record is None:
            return {}
        data = record[2]
        if data.startswith(FLASH_FORMAT):
            return {LEGACY_FLASH_NAME: bip39.mnemonic_to_bytes(self.open_device_seed(data, 8))}
        try:
            header, plaintext = aead_decrypt(data[8:], self.device_backup_key())
            if header != data[:8] or len(plaintext) != FLASH_SEED_LIMIT * CATALOG_ENTRY_SIZE:
                raise ValueError('Invalid catalog size or header')
            entries = {}
            for offset in range(0, len(plaintext), CATALOG_ENTRY_SIZE):
                entry = plaintext[offset:offset + CATALOG_ENTRY_SIZE]
                if not any(entry):
                    continue
                label = entry[:24].rstrip(b'\x00').decode()
                length = entry[24]
                if not valid_label(label) or label in entries or length not in (16, 20, 24, 28, 32):
                    raise ValueError('Invalid catalog entry')
                entries[label] = entry[25:25 + length]
            return entries
        except Exception:
            raise KeyStoreError('Flash seed catalog belongs to a different device key or is damaged')

    def flash_seed_space(self):
        return FLASH_SEED_LIMIT - len(self.flash_seed_entries(self.flash_seed_record()))

    async def save_to_flash(self):
        if not self.mnemonic:
            raise KeyStoreError('Load a recovery phrase first')
        label = await self.show(InputScreen('Name this flash backup',
                                            'Use letters, digits, hyphens or underscores.',
                                            min_length=1, max_length=24))
        if label is None:
            return None
        if not valid_label(label):
            raise KeyStoreError('Use letters, digits, hyphens or underscores')
        current = self.flash_seed_record()
        entries = self.flash_seed_entries(current)
        if label in entries:
            if not await self.show(Prompt('Replace flash backup?', 'Replace %s?' % label)):
                return None
        elif len(entries) >= FLASH_SEED_LIMIT:
            raise KeyStoreError('Flash seed storage is full. Enter an existing name to replace it.')
        if current is not None:
            generation, slot, _ = current
            if generation == 0xffffffff:
                raise KeyStoreError('Flash seed record sequence is exhausted')
            generation, slot = generation + 1, 1 - slot
        else:
            generation, slot = 1, 0
        self.show_backup_status('Creating encrypted backup...',
                                'Encrypting and saving the recovery phrase to device flash.')
        entries[label] = bip39.mnemonic_to_bytes(self.mnemonic)
        plaintext = bytearray(FLASH_SEED_LIMIT * CATALOG_ENTRY_SIZE)
        for i, (name, entropy) in enumerate(entries.items()):
            offset = i * CATALOG_ENTRY_SIZE
            plaintext[offset:offset + len(name)] = name.encode()
            plaintext[offset + 24] = len(entropy)
            plaintext[offset + 25:offset + 25 + len(entropy)] = entropy
        header = CATALOG_FORMAT + generation.to_bytes(4, 'little')
        try:
            record = header + aead_encrypt(self.device_backup_key(), header, bytes(plaintext))
            forgebox.seed_catalog_write(slot, record)
        except OSError:
            raise KeyStoreError('Could not save the encrypted seed to flash')
        finally:
            for i in range(len(plaintext)):
                plaintext[i] = 0
        return label

    async def save_mnemonic(self):
        if not self.mnemonic or not platform.sdcard.is_present:
            raise KeyStoreError('Load a recovery phrase and insert a microSD card')
        label = await self.show(InputScreen('Name this backup', 'Use letters, digits, hyphens or underscores.',
                                            min_length=1, max_length=24))
        if label is None:
            return None
        if not valid_label(label):
            raise KeyStoreError('Use letters, digits, hyphens or underscores')
        self.show_backup_status('Creating encrypted backup...',
                                'Encrypting and saving the recovery phrase to microSD.')
        try:
            return self.write_seed_backup(label)
        except OSError:
            raise KeyStoreError('Could not write the seed backup to microSD')

    async def load_mnemonic(self):
        buttons = []
        flash = None
        flash_error = sd_error = None
        try:
            flash = self.flash_seed_entries(self.flash_seed_record())
            buttons.extend((i, '%s (flash)' % name) for i, name in enumerate(sorted(flash)))
        except KeyStoreError as error:
            flash_error = error
        try:
            buttons.extend((name, name) for name in self.saved_seed_names())
        except OSError:
            sd_error = KeyStoreError('Could not read the microSD card')
        if not buttons:
            raise flash_error or sd_error or KeyStoreError('No ForgeBox seed backups found')
        filename = await self.show(Menu(buttons, title='Load saved seed', last=(None, 'Cancel')))
        if filename is None:
            return False
        if isinstance(filename, int):
            self.show_backup_status('Decrypting wallet backup...',
                                    'Opening the recovery phrase saved in device flash.')
            mnemonic = bip39.mnemonic_from_bytes(flash[sorted(flash)[filename]])
        else:
            self.show_backup_status('Opening wallet backup...', 'Reading the backup file from microSD.')
            try:
                encrypted = self.read_seed_file(filename)
            except OSError:
                raise KeyStoreError('Could not read the seed backup from microSD')
            if encrypted.startswith(SD_FORMAT):
                self.show_backup_status('Decrypting wallet backup...',
                                        'Opening the backup with the device key.')
                mnemonic = self.open_device_seed(encrypted, len(SD_FORMAT))
            elif not PRODUCTION and encrypted.startswith(LEGACY_FORMAT):
                password = await self.backup_password('Enter backup password', 'Unlock this older microSD backup.')
                if password is None:
                    return False
                self.show_backup_status('Decrypting wallet backup...',
                                        'Checking the backup password. This can take a while.')
                mnemonic = self.open_legacy_seed(encrypted, password)
            else:
                raise KeyStoreError('Invalid ForgeBox seed backup')
        self.set_mnemonic(mnemonic)
        return True

    async def storage_menu(self):
        choice = await self.show(Menu([(1, 'Save seed to microSD'), (2, 'Save seed to device flash'),
                                       (3, 'Load saved seed')],
                                      title='Seed storage', last=(None, 'Back')))
        if choice == 1:
            filename = await self.save_mnemonic()
            if filename:
                await self.show(Alert('Seed backup saved', filename))
        if choice == 2:
            label = await self.save_to_flash()
            if label:
                await self.show(Alert('Seed backup saved', '%s (device flash)' % label))
        if choice == 3:
            return await self.load_mnemonic()
        return False

    async def show_mnemonic(self):
        await self.show(MnemonicScreen(self.mnemonic))
