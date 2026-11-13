"""ForgeBox platform contract for RAM settings, SD transactions and wallet storage."""
import os
import forgebox
import config
from errors import BaseError
from blockdev import RAMDisk, SDCard as SDBlock
from forgebox_version import PRODUCTION, VERSION

simulator = False
bootloader_locked = None  # Owner key/factory boot policy is not probed here.
build_type = "forgebox" if PRODUCTION else "forgebox-development"
_ramdisk = None

class CriticalErrorWipeImmediately(Exception):
    pass

def fpath(path):
    # Legacy Specter directory names are aliases inside a volatile FAT volume.
    if path == '/flash' or path.startswith('/flash/'):
        return '/ram/flash' + path[6:]
    if path == '/qspi' or path.startswith('/qspi/'):
        return '/ram/qspi' + path[5:]
    return path

def maybe_mkdir(path):
    try:
        os.mkdir(path)
    except OSError:
        if not (os.stat(path)[0] & 0x4000):
            raise

def mount_sdram():
    global _ramdisk
    if _ramdisk is None:
        _ramdisk = RAMDisk()
        os.VfsFat.mkfs(_ramdisk)  # Only the fresh, in-memory disk is formatted.
        os.mount(os.VfsFat(_ramdisk), '/ram')
        for name in ('flash', 'qspi', 'tmp'):
            maybe_mkdir('/ram/' + name)
    return '/ram'

class SDCard:
    def __init__(self):
        self._mounted = False
        self._depth = 0
        self._block = SDBlock()

    @property
    def is_present(self):
        return forgebox.sd_present()

    def mount(self):
        if self._mounted:
            return
        if not self.is_present:
            raise OSError('SD card unavailable')
        # VfsFat initializes the controller through the block device for every
        # fresh mount. An unrecognized filesystem is never autoformatted.
        os.mount(os.VfsFat(self._block), '/sd')
        self._mounted = True

    def unmount(self):
        if self._mounted:
            os.umount('/sd')
            self._mounted = False

    def open(self, name, *args, **kwargs):
        # Reject traversal so callers cannot escape the SD mount.
        parts = name.lstrip('/').split('/')
        if '..' in parts:
            raise ValueError('invalid SD path')
        return open('/sd/' + '/'.join(parts), *args, **kwargs)

    def file_exists(self, name):
        return file_exists('/sd/' + name.lstrip('/'))

    def __enter__(self):
        self.mount()
        self._depth += 1
        return self

    def __exit__(self, *args):
        self._depth -= 1
        if self._depth == 0:
            self.unmount()

sdcard = SDCard()

def file_exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False

def delete_recursively(path, include_self=False):
    # Protect SD and hardware: all application-owned storage is volatile.
    if not path.startswith('/ram/') or '..' in path.split('/'):
        raise ValueError('only RAM directories may be wiped')
    for name, kind, *_ in os.ilistdir(path):
        child = path.rstrip('/') + '/' + name
        if kind == 0x4000:
            delete_recursively(child, True)
        else:
            os.remove(child)
    if include_self:
        os.rmdir(path)

def get_preallocated_ram():
    return forgebox.scratch(), 1024 * 1024

def sync():
    os.sync()

def reboot():
    forgebox.reset()

def wipe():
    if forgebox.gen1_enabled():
        try:
            forgebox.gen1_wipe()
        except OSError:
            raise BaseError('Could not verify wallet wipe. Restart to finish a pending wipe.')
        reboot()
        return
    try:
        forgebox.wallet_flash_wipe()
    finally:
        # Leave the running wallet even after an I/O failure. Startup checks
        # the journal before presenting the PIN or allowing new enrollment.
        reboot()

def power_off():
    sync()
    try:
        forgebox.power_off()
    except OSError:
        raise BaseError('Power off unavailable. Disconnect USB power and try again.')

def get_version():
    return VERSION

def get_git_info():
    from git_info import REPOSITORY, BRANCH, COMMIT
    return REPOSITORY, BRANCH, COMMIT

def get_bootloader_lock_status():
    return 'unknown'

def get_build_type():
    return build_type

def get_firmware_boot_mode():
    return 'ForgeBox factory bootloader'

def get_flash_read_protection_status():
    return 'unknown'

def get_flash_write_protection_status():
    return 'unknown'

def get_battery_status():
    reading = forgebox.battery_status()
    return reading[:2] if reading is not None else (None, None)

def enable_usb():
    raise RuntimeError('USB host and REPL are not built in this port')

def disable_usb():
    pass

def set_usb_mode(dev=False, usb=False):
    if dev or usb:
        raise RuntimeError('USB host and REPL are not built in this port')

def usb_connected():
    return forgebox.usb_serial_available() and forgebox.usb_serial_connected()
