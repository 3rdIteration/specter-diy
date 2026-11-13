import json
import forgebox
from binascii import hexlify, unhexlify
from helpers import aead_decrypt, aead_encrypt, tagged_hash
from keystore.core import KeyStoreError
from platform import maybe_mkdir

from forgebox_version import PRODUCTION

MAGIC = b'FW01' if PRODUCTION else b'SFW2'
FIRST_BANK = 2 if PRODUCTION else 0
MAX_RECORD = 32767
FILES = ('descriptor', 'meta')


class WalletFlash:
    def __init__(self, keystore):
        self.keystore = keystore

    def key(self):
        return tagged_hash('ForgeBox wallet flash v1', self.keystore.device_backup_key())

    def namespace(self, path):
        network = path.split('/')[-1]
        if self.keystore.idkey is None:
            raise KeyStoreError('Load a seed before accessing wallet storage')
        seed_id = tagged_hash('ForgeBox wallet namespace v1', self.keystore.idkey)
        return hexlify(seed_id).decode() + '/' + network

    def read(self):
        latest = None
        for bank in range(FIRST_BANK, 4):
            try:
                record = forgebox.descriptor_flash_read(bank)
            except OSError:
                raise KeyStoreError('Wallet flash unavailable or reserved space already in use')
            if record is None:
                continue
            if PRODUCTION and not record.startswith(MAGIC):
                raise KeyStoreError('Unknown descriptor format')
            generation = int.from_bytes(record[4:8], 'little')
            if not generation or (latest is not None and generation == latest[0]):
                raise KeyStoreError('Wallet flash record sequence is invalid')
            if latest is None or generation > latest[0]:
                latest = (generation, bank, record)
        if latest is None:
            return {}, 0, 1, 0
        generation, bank, record = latest
        try:
            header, payload = aead_decrypt(record[10:], self.key())
            if header != record[:10]:
                raise ValueError('Invalid wallet flash header')
            wallets = json.loads(payload.decode())
            if not isinstance(wallets, dict):
                raise ValueError('Invalid wallet flash contents')
        except Exception:
            raise KeyStoreError('Wallet flash belongs to a different device key or is damaged')
        return wallets, generation, bank, len(record)

    def commit(self, wallets, generation, bank):
        if generation == 0xffffffff:
            raise KeyStoreError('Wallet flash record sequence is exhausted')
        payload = json.dumps(wallets).encode()
        # 10-byte header, compact length, associated header, IV, padded AES
        # ciphertext and HMAC. Match the upstream Specter AEAD framing.
        size = 69 + ((len(payload) + 16) // 16) * 16
        if size > MAX_RECORD:
            raise KeyStoreError('Wallet descriptor storage is full')
        header = MAGIC + (generation + 1).to_bytes(4, 'little') + size.to_bytes(2, 'little')
        record = header + aead_encrypt(self.key(), header, payload)
        try:
            forgebox.descriptor_flash_write(3 if bank == 2 else 2, record)
        except OSError:
            raise KeyStoreError('Could not save wallet descriptors to flash')

    def restore(self, directory):
        wallets, _, _, _ = self.read()
        prefix = self.namespace(directory) + '/'
        for name, files in wallets.items():
            if not name.startswith(prefix):
                continue
            wallet_id = name[len(prefix):]
            if not wallet_id.isdigit() or not isinstance(files, list) or len(files) != 2:
                raise KeyStoreError('Invalid wallet flash entry')
            path = directory + '/' + wallet_id
            maybe_mkdir(path)
            for filename, encoded in zip(FILES, files):
                with open(path + '/' + filename, 'wb') as output:
                    output.write(unhexlify(encoded))

    def update(self, path, remove=False):
        directory, wallet_id = path.rsplit('/', 1)
        name = self.namespace(directory) + '/' + wallet_id
        wallets, generation, bank, _ = self.read()
        if remove:
            if name not in wallets:
                return
            del wallets[name]
        else:
            files = []
            for filename in FILES:
                with open(path + '/' + filename, 'rb') as source:
                    files.append(hexlify(source.read()).decode())
            wallets[name] = files
        self.commit(wallets, generation, bank)

    def space(self):
        _, _, _, used = self.read()
        return MAX_RECORD - used, MAX_RECORD

    def load_device_setting(self, name, default=None):
        wallets, _, _, _ = self.read()
        return wallets.get('@device/' + name, default)

    def save_device_setting(self, name, value):
        wallets, generation, bank, _ = self.read()
        key = '@device/' + name
        if key not in wallets or wallets[key] != value:
            wallets[key] = value
            self.commit(wallets, generation, bank)
