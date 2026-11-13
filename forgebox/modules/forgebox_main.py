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
from helpers import load_apps
from forgebox_pin import FlashPinKeyStore
from forgebox_version import PRODUCTION

class ForgeBoxSpecter(Specter):
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
    signer.start()

if __name__ == '__main__':
    main()
