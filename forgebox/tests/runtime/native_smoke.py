"""Runs inside the port's real MicroPython VM; public test vector seed only."""
import os
import gc
import asyncio
from io import BytesIO
import display
import platform
import secp256k1
from blockdev import RAMDisk
from forgebox_keystore import VolatileKeyStore
from embit import bip32, ec, psbt, script, transaction
from embit.networks import NETWORKS

display.init(False)
platform.mount_sdram()
os.chdir('/ram')
secp256k1.context_randomize(os.urandom(32))

# Actual FAT formatting/mounting and read/write over the same RAMDisk class.
with open('/ram/test.txt', 'w') as f:
    f.write('forgebox')
with open('/ram/test.txt') as f:
    assert f.read() == 'forgebox'
assert platform.fpath('/flash/keystore') == '/ram/flash/keystore'
assert platform.fpath('/qspi/wallets') == '/ram/qspi/wallets'
for path in ('/sd', '/ram/../sd', '/flash'):
    try:
        platform.delete_recursively(path)
        raise AssertionError('unsafe wipe accepted')
    except ValueError:
        pass
d = RAMDisk(8)
for index, size in ((-1, 512), (8, 512), (0, 513), (7, 1024)):
    try:
        d.readblocks(index, bytearray(size))
        raise AssertionError('invalid block accepted')
    except ValueError:
        pass
assert not platform.sdcard.is_present
try:
    platform.sdcard.mount()
    raise AssertionError('absent SD mounted')
except OSError:
    pass
print('PASS: RAM FAT filesystem, bounds, SD absence, protected wipe paths')

from gui.core import init as gui_init
from gui.screens import Menu
import lvgl as lv
gui_init()
screen = Menu([(1, 'Import recovery phrase'), (2, 'Open SD card file')],
              title='Specter on ForgeBox', note='RAM-only experimental port\nDisposable test seeds only')
lv.scr_load(screen)
display.update(50)

from camera_gui import ForgeBoxGUI
from gui.components.battery import Battery
import forgebox
assert forgebox.battery_status() == (52, False, 3800)
assert platform.get_battery_status() == (52, False)

async def check_battery_monitor():
    gui = ForgeBoxGUI()
    gui.scr = screen
    readings = [(None, None), (52, True)]
    def sample():
        return readings.pop(0) if readings else (52, True)
    gui.set_battery_callback(sample)
    monitor = asyncio.create_task(gui.update_battery(1))
    await asyncio.sleep_ms(20)
    assert Battery.VALUE == 52 and Battery.CHARGING
    assert screen.battery.charge.get_text() == lv.SYMBOL.CHARGE
    monitor.cancel()

asyncio.run(check_battery_monitor())
print('PASS: battery API, transient measurement retry and charging indicator')

def loader(*args, **kwargs):
    pass

from forgebox_pin import (
    AntiPhishingPinScreen, ENCRYPTED_RECORD_SIZE, NO_WORD_KEY, FlashPinKeyStore,
)
from keystore.core import PinError
pin_values = iter(('123456', '123456'))
async def choose_pin(screen):
    if not hasattr(screen, 'pin'):
        return True
    return next(pin_values)

first_boot = FlashPinKeyStore()
asyncio.run(first_boot.init(choose_pin, loader))
assert first_boot.is_locked and not first_boot.is_pin_set
asyncio.run(first_boot.unlock())
assert first_boot.is_pin_set and not first_boot.is_locked
secret = first_boot.secret
assert secret not in forgebox.pin_flash_read(first_boot.slot)
legacy_record = first_boot.record[:ENCRYPTED_RECORD_SIZE] + NO_WORD_KEY
device_word = first_boot.get_auth_words(b'')
unlock_words = first_boot.get_auth_words('12')
assert unlock_words.split()[0] == device_word
assert len(unlock_words.split()) == 2
assert first_boot.get_auth_words('1') == device_word
assert first_boot.get_auth_words('123456') == unlock_words

class PinButton:
    def get_active_btn_text(self):
        return self.text

pin_screen = AntiPhishingPinScreen(get_word=first_boot.get_auth_words)
button = PinButton()
for digit, expected in (('1', device_word), ('2', unlock_words), ('3', unlock_words)):
    button.text = digit
    pin_screen.cb(button, lv.EVENT.RELEASED)
    assert pin_screen.words.get_text() == expected
button.text = lv.SYMBOL.CLOSE
pin_screen.cb(button, lv.EVENT.RELEASED)
assert pin_screen.words.get_text() == device_word and pin_screen.pin.get_text() == ''

second_boot = FlashPinKeyStore()
asyncio.run(second_boot.init(choose_pin, loader))
assert second_boot.is_pin_set and second_boot.is_locked
assert second_boot.get_auth_words('12') == unlock_words
try:
    second_boot.verify_pin('999999')
    raise AssertionError('wrong PIN accepted')
except PinError:
    pass

third_boot = FlashPinKeyStore()
asyncio.run(third_boot.init(choose_pin, loader))
assert third_boot.failures == 1
assert third_boot.verify_pin('123456') == secret
assert third_boot.failures == 0
third_boot.save_pin('654321', secret)
assert all(byte == 0xff for byte in forgebox.pin_flash_read(1 - third_boot.slot))

fourth_boot = FlashPinKeyStore()
asyncio.run(fourth_boot.init(choose_pin, loader))
assert fourth_boot.generation == 3 and fourth_boot.is_locked
assert fourth_boot.word_key == first_boot.word_key
assert fourth_boot.get_auth_words('12') == unlock_words
try:
    fourth_boot.verify_pin('123456')
    raise AssertionError('old PIN accepted')
except PinError:
    pass
assert fourth_boot.verify_pin('654321') == secret
assert forgebox.device_diagnostics() == ('Host fixture', True, False, None, None)
assert forgebox.cpu_debug_status() is None
print('PASS: startup PIN enrollment, encrypted flash, retry counter across boot, PIN change, diagnostics snapshot')

async def approve(screen):
    return True

store = VolatileKeyStore()
asyncio.run(store.init(approve, loader))
# BIP39's well-known public test phrase. Never use this phrase for funds.
store.set_mnemonic('abandon ' * 11 + 'about')
path = bip32.parse_path('m/84h/1h/0h/0/0')
child = store.root.derive(path)
pub = child.get_public_key()
address = script.p2wpkh(pub).address(NETWORKS['test'])
assert address == 'tb1q6rz28mcfaxtmd6v789l9rrlrusdprr9pqcpvkl', address
tx = transaction.Transaction(
    vin=[transaction.TransactionInput(b'\x11' * 32, 0)],
    vout=[transaction.TransactionOutput(99000,
          script.p2wpkh(store.root.derive('m/84h/1h/0h/0/1').get_public_key()))])
unsigned = psbt.PSBT(tx)
unsigned.inputs[0].witness_utxo = transaction.TransactionOutput(100000, script.p2wpkh(pub))
unsigned.inputs[0].bip32_derivations[pub] = psbt.DerivationPath(store.fingerprint, path)
payload = unsigned.serialize()
store.sign_psbt(unsigned)
sig = unsigned.inputs[0].partial_sigs[pub]
assert pub.verify(ec.Signature.parse(sig[:-1]), unsigned.sighash(0))
assert psbt.PSBT.parse(unsigned.serialize()).inputs[0].partial_sigs[pub] == sig
print('PASS: BIP39/BIP84 address vector and independently verified PSBT signature')

# Exercise upstream wallet creation, analysis, transaction-review UI and signing.
from helpers import load_apps
from app import BaseApp
BaseApp.TEMPDIR = '/ram/tmp'
apps = load_apps(whitelist=['wallets', 'xpubs', 'signmessage', 'compatibility'])
for app in apps:
    app.init(store, 'test', loader, None)
wallets = next(app for app in apps if hasattr(app, 'manager'))
reviewed = []
async def cancel(scr):
    reviewed.append(type(scr).__name__)
    return False
async def confirm(scr):
    reviewed.append(type(scr).__name__)
    lv.scr_load(scr)
    display.update(50)
    return True
from apps.wallets.manager import RAW_STREAM
assert asyncio.run(wallets.manager.sign_psbt(BytesIO(payload), cancel, RAW_STREAM)) is None
assert reviewed
reviewed = []
result = asyncio.run(wallets.manager.sign_psbt(BytesIO(payload), confirm, RAW_STREAM))
assert 'TransactionScreen' in reviewed, reviewed
with open(result, 'rb') as f:
    signed = psbt.PSBT.parse(f.read())
sig = signed.inputs[0].partial_sigs[pub]
assert pub.verify(ec.Signature.parse(sig[:-1]), signed.sighash(0))
assert not store.is_key_saved
assert not platform.file_exists('/ram/flash/keystore/secret')
print('PASS: upstream wallet analysis, cancellation, transaction-review UI and signing')

# Run the real SDHost against a host-only removable FAT image and real native VFS.
import forgebox
from blockdev import SDCard
forgebox._test_sd_insert(True)
before = forgebox._test_sd_writes()
try:
    platform.sdcard.mount()
    raise AssertionError('unformatted card accepted')
except OSError:
    pass
assert forgebox._test_sd_writes() == before, 'card was autoformatted'
os.VfsFat.mkfs(SDCard())  # Explicit fixture creation, only in the host test.
from hosts import Host, HostError
from forgebox_sdhost import ForgeBoxSDHost
host = ForgeBoxSDHost('/ram/sdhost')
try:
    asyncio.run(host.get_data())
    raise AssertionError('empty card accepted')
except HostError as error:
    assert 'No matching files' in str(error)
assert not platform.sdcard._mounted
with platform.sdcard:
    with platform.sdcard.open('request.psbt', 'wb') as f:
        f.write(payload)
# Replace the card between mounts without polling the empty slot.
forgebox._test_sd_insert(False)
forgebox._test_sd_insert(True)
Host.SETTINGS_DIR = platform.fpath('/qspi/hosts')
Host.parent = store
from gui.async_gui import AsyncGUI
class GUI(AsyncGUI):
    async def load_screen(self, screen):
        lv.scr_load(screen)
        display.update(50)
        async def select():
            await asyncio.sleep_ms(1)
            screen.set_value('/sd/request.psbt')
        asyncio.create_task(select())

    async def prompt(self, *args):
        return False  # No overwrite and no optional QR presentation.
class Manager:
    gui = GUI()
host.manager = Manager()
incoming = asyncio.run(host.get_data())
assert incoming.read() == b'sign ' + payload
asyncio.run(host.send_data(result))
from binascii import hexlify
saved = 'request.signed.%s.psbt' % hexlify(store.fingerprint).decode()
with platform.sdcard:
    with platform.sdcard.open(saved, 'rb') as f:
        exported = psbt.PSBT.parse(f.read())
    with platform.sdcard.open('request.psbt', 'rb') as f:
        assert f.read() == payload
sig = exported.inputs[0].partial_sigs[pub]
assert pub.verify(ec.Signature.parse(sig[:-1]), exported.sighash(0))
assert not platform.sdcard._mounted
from keystore.core import KeyStoreError
from forgebox_keystore import LEGACY_FORMAT, SD_FORMAT, backup_key
from helpers import aead_encrypt
from embit import bip39
backup_password = 'public test backup password'
backup_statuses = []
lv.scr_load(screen)

def backup_loader(message='', title='Processing...'):
    screen.show_loader(message, title)
    assert title in screen.mbox.mbox.get_text()
    if title != 'Generating keys...':
        backup_statuses.append(title)

save_inputs = iter(('654321', 'public-vector', 'first', 'first', False, 'second', 'first', True))
async def choose_save(screen):
    return next(save_inputs)

backup_store = FlashPinKeyStore()
asyncio.run(backup_store.init(choose_save, loader))
asyncio.run(backup_store.unlock())
backup_store.show_loader = backup_loader
backup_store.set_mnemonic(store.mnemonic)
filename = asyncio.run(backup_store.save_mnemonic())
assert filename == 'public-vector.fbs'
assert backup_store.is_key_saved
legacy_flash = backup_store.seal_seed(b'SFS1' + b'\x01\x00\x00\x00')
forgebox.seed_flash_write(0, legacy_flash)
assert asyncio.run(backup_store.save_to_flash()) == 'first'
assert not asyncio.run(backup_store.save_to_flash())  # Refused replacement.
alternate = bip39.mnemonic_from_bytes(bytes([1]) * 16)
backup_store.set_mnemonic(alternate)
assert asyncio.run(backup_store.save_to_flash()) == 'second'
assert asyncio.run(backup_store.save_to_flash()) == 'first'
assert backup_store.flash_seed_space() == 13
assert backup_statuses == ['Creating encrypted backup...'] * 4
backup_statuses.clear()
from apps.wallets.manager import WalletManager
from forgebox_wallet_flash import WalletFlash
backup_store.wallet_flash = WalletFlash(backup_store)
wallet_manager = WalletManager('/ram/qspi/test-wallets')
wallet_manager.init(backup_store, 'main', loader, None)
descriptor = str(wallet_manager.wallets[0].descriptor)
imported = wallet_manager.parse_wallet('Imported descriptor&' + descriptor.replace('/0h]', '/1h]').replace(
    backup_store.get_xpub("m/84h/0h/0h").to_base58(),
    backup_store.get_xpub("m/84h/0h/1h").to_base58()))
wallet_manager.add_wallet(imported)
assert len(wallet_manager.wallets) == 2
from platform import delete_recursively
backup_store.set_mnemonic(store.mnemonic)
wallet_manager.init(backup_store, 'main', loader, None)
assert str(wallet_manager.wallets[0].descriptor) != descriptor
assert len(wallet_manager.wallets) == 1
backup_store.set_mnemonic(alternate)
delete_recursively('/ram/qspi/test-wallets', include_self=True)
wallet_manager = WalletManager('/ram/qspi/test-wallets')
wallet_manager.init(backup_store, 'main', loader, None)
assert str(wallet_manager.wallets[0].descriptor) == descriptor
assert wallet_manager.wallets[1].name == 'Imported descriptor'
w = wallet_manager.wallets[0]
w.name = 'Saved descriptor test'
w.save(backup_store)
delete_recursively('/ram/qspi/test-wallets', include_self=True)
wallet_manager = WalletManager('/ram/qspi/test-wallets')
wallet_manager.init(backup_store, 'main', loader, None)
assert wallet_manager.wallets[0].name == 'Saved descriptor test'
for bank in (2, 3):
    ciphertext = forgebox.descriptor_flash_read(bank)
    assert descriptor.encode() not in ciphertext and b'Saved descriptor test' not in ciphertext
wallet_manager.delete_wallet(wallet_manager.wallets[0])
wallet_manager.delete_wallet(wallet_manager.wallets[0])
delete_recursively('/ram/qspi/test-wallets', include_self=True)
wallet_manager = WalletManager('/ram/qspi/test-wallets')
wallet_manager.init(backup_store, 'main', loader, None)
assert wallet_manager.wallets[0].name == 'Default'
print('PASS: encrypted descriptor restore after RAM loss, seed isolation, rename and deletion')
for slot in range(2):
    encrypted = forgebox.seed_backup_read(slot)
    assert b'first' not in encrypted and b'second' not in encrypted
    assert bip39.mnemonic_to_bytes(alternate) not in encrypted
with platform.sdcard:
    with platform.sdcard.open(filename, 'rb') as f:
        backup = f.read()
    assert store.mnemonic.encode() not in backup
    assert backup.startswith(SD_FORMAT)
    assert not any(name.endswith('.tmp') for name in os.listdir('/sd'))
    salt = bytes(16)
    header = LEGACY_FORMAT + salt
    legacy = header + aead_encrypt(backup_key(backup_password, salt), header, store.mnemonic.encode())
    with platform.sdcard.open('legacy.fbs', 'wb') as f:
        f.write(legacy)
try:
    backup_store.write_seed_backup('public-vector')
    raise AssertionError('backup silently overwritten')
except KeyStoreError:
    pass
try:
    backup_store.open_legacy_seed(legacy, 'wrong password')
    raise AssertionError('wrong backup password accepted')
except KeyStoreError:
    pass
with platform.sdcard:
    with platform.sdcard.open('tampered.fbs', 'wb') as f:
        f.write(backup[:-1] + bytes([backup[-1] ^ 1]))
try:
    backup_store.open_device_seed(backup_store.read_seed_file('tampered.fbs'), len(SD_FORMAT))
    raise AssertionError('tampered backup accepted')
except KeyStoreError:
    pass
different_device = FlashPinKeyStore()
different_device.secret = store.secret
different_device.enc_secret = store.enc_secret
try:
    different_device.open_device_seed(backup, len(SD_FORMAT))
    raise AssertionError('different device key accepted')
except KeyStoreError:
    pass
backup_store.save_pin('135790', backup_store.secret)
restore_inputs = iter(('135790', filename, 0, 1, 2, 'legacy.fbs', backup_password))
async def choose_restore(screen):
    value = next(restore_inputs)
    if value == backup_password:
        assert backup_statuses[-1] == 'Opening wallet backup...'
    return value

restored = FlashPinKeyStore()
asyncio.run(restored.init(choose_restore, loader))
assert restored.is_locked
asyncio.run(restored.unlock())
restored.show_loader = backup_loader
assert restored.secret == backup_store.secret
assert asyncio.run(restored.load_mnemonic())
assert restored.mnemonic == store.mnemonic
assert restored.fingerprint == store.fingerprint
assert asyncio.run(restored.load_mnemonic()) and restored.mnemonic == store.mnemonic
assert asyncio.run(restored.load_mnemonic()) and restored.mnemonic == alternate
assert asyncio.run(restored.load_mnemonic()) and restored.mnemonic == alternate
assert asyncio.run(restored.load_mnemonic()) and restored.mnemonic == store.mnemonic
assert backup_statuses == [
    'Opening wallet backup...', 'Decrypting wallet backup...',
    'Decrypting wallet backup...', 'Decrypting wallet backup...', 'Decrypting wallet backup...',
    'Opening wallet backup...', 'Decrypting wallet backup...',
]
screen.hide_loader()
assert not platform.sdcard._mounted
print('PASS: encrypted named flash seeds, legacy migration, replacement, free slots, PIN change and SD recovery')
forgebox._test_sd_insert(False)
print('PASS: SDHost import/export, preserved unsigned input, verified exported signature, no autoformat')
gc.collect()
lv.scr_load(screen)
display.update(50)
print('PASS: LVGL render after garbage collection')

# Same decoded-frame transport used on ARM; host fixture bypasses optical decoding.
from qr_transport import QRAccumulator
from hosts.core import HostError
from hosts.camera import CameraHost
from qrencoder import CryptoPSBTEncoder, Base64QREncoder, LegacyBCUREncoder
os.mkdir('/ram/camera')
def accumulator():
    platform.delete_recursively('/ram/camera')
    return QRAccumulator('/ram/camera')
binary = b'\x00\r\x00\xffseed\r'
scan = accumulator()
assert scan.feed(binary)
with open(scan.result_path, 'rb') as f:
    assert f.read() == binary
scan = accumulator()
assert not scan.feed(b'p2of3 bb')
assert not scan.feed(b'p2of3 bb')
assert not scan.feed(b'p1of3 aa')
assert scan.feed(b'p3of3 cc')
with open(scan.result_path, 'rb') as f:
    assert f.read() == b'aabbcc'
for invalid in (b'p1of257 x', b'UR:CRYPTO-PSBT/invalid', b'UR:CRYPTO-ACCOUNT/x'):
    try:
        accumulator().feed(invalid)
        raise AssertionError('invalid QR accepted')
    except HostError:
        pass
scan = accumulator()
scan.feed(b'p1of2 one')
try:
    scan.feed(b'p1of2 different')
    raise AssertionError('conflicting part accepted')
except HostError:
    pass
# Round-trip actual upstream fountain encoder, out of order with duplicates.
with CryptoPSBTEncoder(BytesIO(payload), part_len=120, tempfile='/ram/urencode') as enc:
    frames = [enc[i] for i in range(len(enc))]
scan = accumulator()
for frame in reversed(frames):
    if isinstance(frame, str):
        frame = frame.encode()
    scan.feed(frame)
assert scan.done
with open(scan.result_path, 'rb') as f:
    assert f.read() == payload
for Encoder in (Base64QREncoder, LegacyBCUREncoder):
    with Encoder(BytesIO(payload), part_len=180, tempfile='/ram/legacyencode') as enc:
        encoded_frames = [enc[i] for i in range(len(enc))]
    legacy = accumulator()
    for encoded in reversed(encoded_frames):
        legacy.feed(encoded.encode() if isinstance(encoded, str) else encoded)
    with open(legacy.result_path, 'rb') as f:
        if legacy.bcur:
            from bcur import bcur_decode_stream
            assembled = f.read()
            # Hash-bearing multipart legacy UR is consumed by the upstream parser.
            body = assembled.split(b'/')[-1]
            decoded = BytesIO()
            bcur_decode_stream(BytesIO(body), decoded)
            assert decoded.getvalue() == payload
        else:
            from binascii import a2b_base64
            assert a2b_base64(f.read()) == payload
corrupt = bytearray(frame)
corrupt[-1] = ord('a') if corrupt[-1] != ord('a') else ord('b')
try:
    accumulator().feed(bytes(corrupt))
    raise AssertionError('corrupted UR accepted')
except HostError:
    pass
camera = CameraHost('/ram/camerahost')
forgebox._test_camera_frame(binary)
incoming = asyncio.run(camera.get_data())
assert incoming.read() == binary
assert not forgebox._test_camera_active()
camera.MAX_SCAN_MS = 0
try:
    asyncio.run(camera.get_data())
    raise AssertionError('timeout failed')
except HostError:
    pass
assert not forgebox._test_camera_active()
assert not os.listdir(camera.path)
async def abort_scan():
    await asyncio.sleep_ms(10)
    camera.abort()
async def cancelled_scan():
    camera.MAX_SCAN_MS = 1000
    asyncio.create_task(abort_scan())
    return await camera.get_data()
assert asyncio.run(cancelled_scan()) is None
assert not forgebox._test_camera_active()
forgebox.camera_start()
preview = bytearray(240 * 240 * 4)
assert forgebox.camera_preview(preview)
assert preview[:4] == bytes([558 % 251] * 3 + [255])
forgebox.camera_stop()
from camera_gui import CameraProgress
forgebox.camera_start()
camera_screen = CameraProgress()
camera_screen.refresh_preview()
full_preview = camera_screen.preview_data
assert len(full_preview) == 480 * 480 * 4
assert full_preview[:8] == bytes(preview[:4]) * 2
assert full_preview[480 * 4:480 * 4 + 8] == bytes(preview[:4]) * 2
lv.scr_load(camera_screen)
gc.collect()
display.update(50)
forgebox.camera_stop()
class QRGUI:
    async def qr_alert(self, title, message, encoder, **kwargs):
        target = accumulator()
        for i in range(len(encoder)):
            frame = encoder[i]
            target.feed(frame.encode() if isinstance(frame, str) else frame)
        assert target.done
        with open(target.result_path, 'rb') as f:
            output = psbt.PSBT.parse(f.read())
        signature = output.inputs[0].partial_sigs[pub]
        assert pub.verify(ec.Signature.parse(signature[:-1]), output.sighash(0))
class QRManager:
    gui = QRGUI()
camera.manager = QRManager()
camera.bcur2 = True
asyncio.run(camera.send_data(result))
print('PASS: camera binary frames, multipart/UR roundtrip, CRC rejection, preview rotation, timeout/cancel cleanup')
print('PASS: camera LVGL screen after GC and signed-PSBT response QR cryptographic roundtrip')

assert backup_store.root is not None
backup_store.lock()
assert backup_store.is_locked and backup_store.root is None and backup_store.mnemonic is None
assert backup_store.enc_secret is None and backup_store.idkey is None
forgebox.wallet_flash_wipe()
forgebox.wallet_flash_resume()
for slot in range(2):
    assert all(byte == 0xff for byte in forgebox.pin_flash_read(slot))
    assert all(byte == 0xff for byte in forgebox.seed_flash_read(slot))
wiped_store = FlashPinKeyStore()
asyncio.run(wiped_store.init(choose_pin, loader))
assert not wiped_store.is_pin_set and wiped_store.is_locked
assert wiped_store.word_key is None
assert wiped_store.flash_seed_record() is None
print('PASS: in-session lock, erase of both PIN and seed copies, fresh enrollment state')

forgebox.pin_flash_write(0, legacy_record)
legacy_store = FlashPinKeyStore()
asyncio.run(legacy_store.init(approve, loader))
assert legacy_store.is_pin_set and legacy_store.word_key is None
try:
    legacy_store.verify_pin('999999')
    raise AssertionError('legacy PIN check accepted wrong PIN')
except PinError:
    assert legacy_store.word_key is None

async def migrate_pin_words(screen):
    if hasattr(screen, 'pin'):
        assert screen.get_word is None
        return '123456'
    assert screen.title.get_text() == 'Your anti-phishing words'
    assert legacy_store.get_auth_words('12') in screen.message.get_text()
    return True

async def skip_pin_delay():
    pass

legacy_store.show = migrate_pin_words
legacy_store.wait_before_attempt = skip_pin_delay
asyncio.run(legacy_store.unlock())
assert legacy_store.secret == secret
assert legacy_store.word_key is not None and legacy_store.word_key != first_boot.word_key
assert all(byte == 0xff for byte in forgebox.pin_flash_read(0))
legacy_store.lock()

async def unlock_with_words(screen):
    assert screen.words.get_text() == legacy_store.get_auth_words(b'')
    return '123456'

legacy_store.show = unlock_with_words
asyncio.run(legacy_store.unlock())
assert legacy_store.secret == secret
assert forgebox.wallet_flash_wipe() is None
print('PASS: PIN words use only the first two digits, persist across PIN updates, and migrate after successful legacy unlock')

import forgebox_gen1

class BlankGen1Hardware:
    def __getattr__(self, name):
        return getattr(forgebox, name)

    def gen1_state(self):
        for slot in range(2):
            assert all(byte == 0xff for byte in forgebox.pin_flash_read(slot))
            assert all(byte == 0xff for byte in forgebox.seed_flash_read(slot))
        return None

forgebox_gen1.forgebox = BlankGen1Hardware()
forgebox.pin_flash_write(0, legacy_record)
old_seed = b'SFS1' + bytes(109)
forgebox.seed_flash_write(0, old_seed)
legacy_prompts = iter((('Existing flash wallet', False), ('Old wallet kept', None),
                       ('Existing flash wallet', True)))

async def choose_gen1_reset(screen):
    title, choice = next(legacy_prompts)
    assert screen.title.get_text() == title
    assert forgebox.pin_flash_read(0).startswith(legacy_record)
    assert forgebox.seed_flash_read(0).startswith(old_seed)
    return choice

gen1_store = forgebox_gen1.SecureElementKeyStore()
asyncio.run(gen1_store.init(choose_gen1_reset, loader))
assert gen1_store.initialized and not gen1_store.is_pin_set
print('PASS: cancelling the Gen 1 reset preserves the old wallet and confirming erases it')

class SeedEntropyHardware:
    failure = 0

    def gen1_enroll(self, salt, pin_key):
        from forgebox_pin import derive_pin_key
        assert len(salt) == 22 and pin_key == derive_pin_key('123456', salt)
        return b's' * 32

    def gen1_seed_entropy(self, root, pin_context, output):
        assert root == b's' * 32 and pin_context is gen1_store.seed_pin_context
        assert len(pin_context) == 32 and any(pin_context)
        if not self.failure:
            for i in range(32):
                output[i] = i
        return self.failure

from embit import bip39
from rng import RNGError

seed_hardware = SeedEntropyHardware()
forgebox_gen1.forgebox = seed_hardware
setup_titles = iter(('Choose a startup PIN', 'Confirm startup PIN', 'Your anti-phishing words'))

async def enroll_gen1(screen):
    assert screen.title.get_text() == next(setup_titles)
    if hasattr(screen, 'pin'):
        return '123456'

gen1_store.show = enroll_gen1
asyncio.run(gen1_store.unlock())
assert gen1_store.is_pin_set and not gen1_store.is_locked
for count in (12, 24):
    phrase = gen1_store.generate_mnemonic(count)
    assert len(phrase.split()) == count
    assert bip39.mnemonic_to_bytes(phrase) == bytes(range(count * 4 // 3))
for failure, source in ((1, 'MCU'), (2, 'ATECC608B'), (3, 'DS28S60')):
    seed_hardware.failure = failure
    try:
        gen1_store.generate_mnemonic(12)
        raise AssertionError('seed generated with failed source')
    except RNGError as error:
        assert source in str(error)
context = gen1_store.seed_pin_context
gen1_store.lock()
assert not any(context) and gen1_store.seed_pin_context is None
print('PASS: direct PIN setup, 12- and 24-word seed generation, RNG errors, and PIN context cleared on lock')

import forgebox_fp_unlock

class FingerprintLoginHardware:
    def fp_unlock_enabled(self):
        return True

    def fp_unlock_test(self, action, pin, progress):
        if action == 0:
            return 2
        assert action == 2
        result = next(scans)
        if isinstance(result, bytearray):
            progress(0, 0)
        return result

    def gen1_unlock(self, pin_key):
        return b's' * 32

class StartupMenu:
    def __init__(self, buttons, title, last):
        assert title == 'Unlock wallet' and last == (2, 'Power off')
        assert buttons in ([(1, 'Use fingerprint'), (0, 'Use startup PIN')],
                           [(0, 'Use startup PIN')])
        self.has_fingerprint = len(buttons) == 2

async def startup_screen(screen):
    expected, choice = next(startup_steps)
    if isinstance(screen, StartupMenu):
        assert screen.has_fingerprint == expected
    else:
        assert screen.title.get_text() == expected
    return choice

fingerprint_hardware = FingerprintLoginHardware()
forgebox_fp_unlock.forgebox = forgebox_gen1.forgebox = fingerprint_hardware
forgebox_fp_unlock.Menu = StartupMenu
gen1_store.show = startup_screen
scans = iter((bytearray(b's' * 32 + b'c' * 32),))
startup_steps = iter(((True, 0), ('Enter startup PIN', None), (True, 1)))
asyncio.run(gen1_store.unlock())
assert gen1_store.secret == b's' * 32 and gen1_store.seed_pin_context == b'c' * 32
assert not list(startup_steps) and not list(scans)
gen1_store.lock()

scans = iter((0x81, 0x39, -106))
startup_steps = iter(((True, 1), ('Fingerprint not recognized', None),
                      (True, 0), ('Enter startup PIN', None),
                      (True, 1), ('Could not read fingerprint', None),
                      (True, 1), ('Startup PIN required', None),
                      (False, 0), ('Enter startup PIN', '123456')))
asyncio.run(gen1_store.unlock())
assert not gen1_store.is_locked
assert not list(startup_steps) and not list(scans)
print('PASS: PIN cancel returns to the startup menu and keeps the fingerprint retry limit')

from forgebox_main import ForgeBoxSpecter

class SleepWallet(ForgeBoxSpecter):
    def __init__(self, keystore):
        self.keystore = keystore

    def set_mnemonic(self, mnemonic, password):
        self.keystore.set_mnemonic(mnemonic, password)

sleep_wallet = SleepWallet(gen1_store)
sleep_wallet.set_mnemonic(alternate, 'test sleep passphrase')
sleep_fingerprint = gen1_store.fingerprint
sealed_session = sleep_wallet.seal_sleep_session()
assert alternate.encode() not in sealed_session and b'test sleep passphrase' not in sealed_session
gen1_store.lock()
sleep_wallet = SleepWallet(gen1_store)
sleep_wallet.sleep_session = sealed_session
assert sleep_wallet.seal_sleep_session() == sealed_session
try:
    sleep_wallet.restore_sleep_session(sealed_session)
    assert False, 'Restored a wallet before unlock'
except ValueError:
    pass
startup_steps = iter(((True, 0), ('Enter startup PIN', '123456')))
asyncio.run(gen1_store.unlock())
assert gen1_store.mnemonic is None
tampered_session = sealed_session[:-1] + bytes([sealed_session[-1] ^ 1])
try:
    sleep_wallet.restore_sleep_session(tampered_session)
except Exception as exc:
    assert str(exc) == 'Invalid HMAC'
else:
    assert False, 'Restored a wallet from corrupted ciphertext'
sleep_wallet.restore_sleep_session(sealed_session)
assert gen1_store.fingerprint == sleep_fingerprint
assert gen1_store.mnemonic == alternate and gen1_store.session_passphrase == 'test sleep passphrase'
print('PASS: sleep preserves the seed and passphrase after unlock and rejects a tampered session')

from forgebox_usb import ForgeBoxUSBHost
import forgebox_usb
from io import BytesIO

class SerialFixture:
    def usb_serial_read(self):
        return next(packets)

    def usb_serial_write(self, data):
        responses.append(bytes(data))

class SerialManager:
    keystore = gen1_store

    async def process_host_request(self, stream):
        assert stream.read() == b'xpub m/84h/0h/0h'
        return BytesIO(b'test-xpub'), {}

forgebox_usb.forgebox = SerialFixture()
serial = ForgeBoxUSBHost('/ram/usb-test')
serial.init()
serial.manager = SerialManager()
serial.settings['enabled'] = True
gen1_store.set_mnemonic(alternate)
packets = iter((b'\r', b'\n\r\n', b'finger', b'print\r\n'))
responses = []
assert serial.read_to_file() is None
assert serial.read_to_file() is None
assert serial.read_to_file() is None
command = serial.read_to_file()
serial.usb.write(serial.ACK)
with open(command, 'rb') as request:
    asyncio.run(serial.process_command(request))
assert b''.join(responses) == b'ACK\r\n' + hexlify(gen1_store.fingerprint) + b'\r\n'
responses.clear()
asyncio.run(serial.process_command(BytesIO(b'xpub m/84h/0h/0h')))
assert b''.join(responses) == b'test-xpub\r\n'
serial.cleanup()
print('PASS: fragmented Specter USB command, upstream ACK and response framing')

apps = load_apps(whitelist=['bip85', 'getrandom', 'label', 'backup'])
assert len(apps) == 4 and any(app.name == 'bip85' for app in apps)
gen1_store.wallet_flash = WalletFlash(gen1_store)
gen1_store.wallet_flash.save_device_setting('network', 'signet')
gen1_store.wallet_flash.save_device_setting('global', {'experimental': {'taproot': True}})
gen1_store.wallet_flash.save_device_setting('camera', {'enabled': False})
serial.save_settings(gen1_store)
reconnected = ForgeBoxUSBHost('/ram/usb-reconnect')
assert reconnected.load_settings(gen1_store)['enabled']
settings = WalletFlash(gen1_store)
assert settings.load_device_setting('network') == 'signet'
assert settings.load_device_setting('global')['experimental']['taproot']
assert not settings.load_device_setting('camera')['enabled']
catalog, sequence, bank, _ = settings.read()
catalog['@device/test-fixture'] = 'x' * 24000
settings.commit(catalog, sequence, bank)
assert settings.load_device_setting('test-fixture') == 'x' * 24000
assert settings.space()[1] == 32767
print('PASS: device settings roundtrip and upstream BIP85, randomness, label and backup apps load')
