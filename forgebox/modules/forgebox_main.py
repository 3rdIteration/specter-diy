import os
import display
import forgebox
import platform
from specter import Specter
from embit.networks import NETWORKS
from camera_gui import ForgeBoxGUI
from hosts import Host, CameraHost
from forgebox_sdhost import ForgeBoxSDHost
from forgebox_diagnostics import DiagnosticsHost
from app import BaseApp
from helpers import aead_decrypt, aead_encrypt, load_apps, tagged_hash
import asyncio
import json
from forgebox_pin import FlashPinKeyStore
from forgebox_version import PRODUCTION

SLEEP_SESSION = b'ForgeBox wallet sleep v1'


class ForgeBoxSpecter(Specter):
    sleep_session = None

    def sleep_session_key(self):
        return tagged_hash('ForgeBox wallet sleep session v1', self.keystore.device_backup_key())

    def seal_sleep_session(self):
        store = self.keystore
        if store.is_locked:
            return self.sleep_session
        if store.mnemonic and hasattr(store, 'session_passphrase'):
            payload = json.dumps([store.mnemonic, store.session_passphrase]).encode()
            return aead_encrypt(self.sleep_session_key(), SLEEP_SESSION, payload)

    def restore_sleep_session(self, session):
        if self.keystore.is_locked:
            raise ValueError('Wallet is locked')
        header, payload = aead_decrypt(session, self.sleep_session_key())
        if header != SLEEP_SESSION:
            raise ValueError('Invalid sleep session')
        mnemonic, passphrase = json.loads(payload.decode())
        self.set_mnemonic(mnemonic, passphrase)

    async def power_monitor(self):
        while True:
            await asyncio.sleep_ms(200)
            if not forgebox.idle_timeout():
                continue
            try:
                session = self.seal_sleep_session()
            except Exception:
                # A failed snapshot must still lock the wallet.
                session = None
            forgebox.sleep(session)

    async def select_network(self):
        choice = await self.gui.menu([
            ('main', 'Bitcoin Mainnet'),
            ('test', 'Testnet'),
            ('signet', 'Signet'),
            ('regtest', 'Regtest'),
        ], last=(None, 'Back'))
        if choice is not None:
            self.set_network(choice)

    def set_network(self, network):
        if network not in NETWORKS:
            network = 'main'
        if not self.keystore.is_locked:
            self.keystore.wallet_flash.save_device_setting('network', network)
        super().set_network(network)

    async def unlock(self):
        await super().unlock()
        network = self.keystore.wallet_flash.load_device_setting('network', self.network)
        if network != self.network:
            self.set_network(network)
        if self.sleep_session is not None:
            session = self.sleep_session
            self.sleep_session = None
            try:
                self.restore_sleep_session(session)
            except Exception:
                self.keystore.clear_loaded_seed()
                await self.gui.alert('Wallet session unavailable',
                                     'Load your seed again to continue.')

    def load_settings(self, fname=None):
        if fname is not None:
            return super().load_settings(fname)
        return self.keystore.wallet_flash.load_device_setting('global', {})

    def save_settings(self, settings, fname=None):
        if fname is not None:
            return super().save_settings(settings, fname)
        self.keystore.wallet_flash.save_device_setting('global', settings)

    async def lock(self):
        await super().lock()
        self.current_menu = self.initmenu


class FingerprintTestSpecter(ForgeBoxSpecter):
    async def fingerprint_unlock_test(self):
        from forgebox_fp_unlock_test import fingerprint_unlock_test
        await fingerprint_unlock_test(self)

class FingerprintSpecter(ForgeBoxSpecter):
    async def fingerprint_settings(self):
        from forgebox_fp_unlock import fingerprint_settings
        await fingerprint_settings(self.keystore)

class BootExportSpecter(FingerprintSpecter):
    async def export_boot_flash(self):
        from forgebox_boot_export import export_boot_flash
        await export_boot_flash(self.gui)


def main():
    display.init(False)
    ram = platform.mount_sdram()
    import secp256k1
    secp256k1.context_randomize(os.urandom(32))
    os.chdir(ram)
    Host.SETTINGS_DIR = platform.fpath('/qspi/hosts')
    Specter.SETTINGS_DIR = platform.fpath('/qspi/global')
    BaseApp.TEMPDIR = ram + '/tmp'
    keystore = FlashPinKeyStore
    if PRODUCTION or forgebox.gen1_enabled():
        from forgebox_gen1 import SecureElementKeyStore
        keystore = SecureElementKeyStore
    signer_class = FingerprintTestSpecter if not PRODUCTION and forgebox.fp_unlock_test_enabled() else ForgeBoxSpecter
    if forgebox.fp_unlock_enabled():
        signer_class = FingerprintSpecter
    if not PRODUCTION and forgebox.boot_flash_available():
        signer_class = BootExportSpecter
    keystore.path = platform.fpath('/flash/keystore')
    hosts = [ForgeBoxSDHost(ram + '/sdhost'), CameraHost(ram + '/camera'),
             DiagnosticsHost(ram + '/diagnostics')]
    if forgebox.usb_serial_available():
        from forgebox_usb import ForgeBoxUSBHost
        hosts.append(ForgeBoxUSBHost(ram + '/usb'))
    signer = signer_class(gui=ForgeBoxGUI(), keystores=[keystore],
        hosts=hosts,
        apps=load_apps(whitelist=['wallets', 'xpubs', 'signmessage', 'compatibility',
                                  'bip85', 'backup', 'getrandom', 'label']),
        settings_path=platform.fpath('/flash'), network='main')
    from forgebox_wallet_flash import WalletFlash
    signer.keystore.wallet_flash = WalletFlash(signer.keystore)
    if hasattr(forgebox, 'resume_session'):
        signer.sleep_session = forgebox.resume_session()
        asyncio.create_task(signer.power_monitor())
    signer.start()

if __name__ == '__main__':
    main()
