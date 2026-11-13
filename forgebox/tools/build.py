"""Build the pinned Specter application on the MH1903 runtime, without flashing."""
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCIES = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8-sig'))
VERSION = DEPENDENCIES['version']
PRODUCTION = os.environ.get('FORGEBOX_PROFILE', 'production') == 'production'
if os.environ.get('FORGEBOX_PROFILE', 'production') not in ('production', 'development'):
    raise SystemExit('FORGEBOX_PROFILE must be production or development')


def run(*args, cwd=ROOT):
    subprocess.run(args, cwd=cwd, check=True, timeout=900)


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def prepare():
    build = ROOT / 'build'
    build.mkdir(exist_ok=True)
    app = build / 'app'
    if app.exists():
        shutil.rmtree(app)
    shutil.copytree(ROOT.parent / 'src', app)
    shutil.copytree(ROOT / 'modules', app, dirs_exist_ok=True)
    host_core = app / 'hosts/core.py'
    host_source = host_core.read_text(encoding='utf-8')
    host_core.write_text(host_source.replace(
        '    def load_settings(self, keystore):\n',
        '    def load_settings(self, keystore):\n'
        '        storage = getattr(keystore, "wallet_flash", None)\n'
        '        if storage is not None and not keystore.is_locked:\n'
        '            self.settings = storage.load_device_setting("host/" + type(self).__name__, self.settings)\n'
        '            return self.settings\n',
    ).replace(
        '    def save_settings(self, keystore):\n',
        '    def save_settings(self, keystore):\n'
        '        storage = getattr(keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            return storage.save_device_setting("host/" + type(self).__name__, self.settings)\n',
    ), encoding='utf-8')
    usb_host = app / 'hosts/usb.py'
    usb_source = usb_host.read_text(encoding='utf-8')
    usb_host.write_text(usb_source.replace('import pyb\n', '').replace(
        '            self.usb = pyb.USB_VCP()', '            import pyb\n            self.usb = pyb.USB_VCP()'),
        encoding='utf-8')
    label_app = app / 'apps/label.py'
    source = label_app.read_text(encoding='utf-8')
    label_app.write_text(source.replace('    def get_label(self):\n',
        '    def get_label(self):\n'
        '        storage = getattr(self.keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            return storage.load_device_setting("label", "Specter-DIY")\n')
        .replace('    def set_label(self, label):\n',
        '    def set_label(self, label):\n'
        '        storage = getattr(self.keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            return storage.save_device_setting("label", label)\n'), encoding='utf-8')
    gui_core = app / 'gui/core.py'
    gui_source = gui_core.read_text(encoding='utf-8')
    blank_screen = '    scr = lv.obj()\n    lv.scr_load(scr)\n    update()\n'
    if gui_source.count(blank_screen) != 1:
        raise ValueError('Unexpected Specter initial screen')
    gui_core.write_text(gui_source.replace(blank_screen, ''), encoding='utf-8')
    controller = app / 'specter.py'
    source = controller.read_text(encoding='utf-8')
    generator = '            mnemonic = await self.gui.new_mnemonic(gen_mnemonic, bip39.WORDLIST, fix_mnemonic)'
    if source.count(generator) != 1:
        raise ValueError('Unexpected Specter mnemonic generator call')
    source = source.replace(
        generator,
        '            from rng import RNGError\n'
        '            generator = getattr(self.keystore, "generate_mnemonic", gen_mnemonic)\n'
        '            try:\n'
        '                mnemonic = await self.gui.new_mnemonic(generator, bip39.WORDLIST, fix_mnemonic)\n'
        '                if isinstance(mnemonic, RNGError):\n'
        '                    raise mnemonic\n'
        '            except RNGError as error:\n'
        '                await self.gui.alert(error.NAME, str(error))\n'
        '                return',
    )
    settings_item = '            buttons.extend([(777, "Change PIN code")])'
    if source.count(settings_item) != 1:
        raise ValueError('Unexpected Specter device settings menu')
    source = source.replace(
        settings_item,
        settings_item + '\n'
        '        if hasattr(self, "fingerprint_unlock_test"):\n'
        '            buttons.append((458, "Fingerprint unlock test"))\n'
        '        if hasattr(self, "fingerprint_settings"):\n'
        '            buttons.append((459, "Fingerprint unlock"))\n'
        '        if hasattr(self, "export_boot_flash"):\n'
        '            buttons.append((460, "Export boot flash to microSD"))',
    )
    source = source.replace(
        '            (456, "Reboot"),',
        '            (456, "Reboot"),\n            (457, "Power off"),', 1,
    ).replace(
        '            elif menuitem == 456:',
        '            elif menuitem == 457:\n'
        '                if await self.gui.prompt("Power off?", "Disconnect USB power first."):\n'
        '                    from platform import power_off\n'
        '                    power_off()\n'
        '                return\n'
        '            elif menuitem == 458:\n'
        '                await self.fingerprint_unlock_test()\n'
        '                return\n'
        '            elif menuitem == 459:\n'
        '                await self.fingerprint_settings()\n'
        '                return\n'
        '            elif menuitem == 460:\n'
        '                await self.export_boot_flash()\n'
        '                return\n'
        '            elif menuitem == 456:', 1,
    ).replace(
        '                    "Wiping the device will erase everything in the internal storage!",\n'
        '                    "This includes multisig wallet files, keys, apps data etc.\\n\\n"\n'
        '                    "But it doesn\'t include files stored on SD card or smartcard.\\n\\n"',
        '                    "Erase wallet storage?",\n'
        '                    "This erases the startup PIN, saved seeds, and wallet descriptors.\\n\\n"\n'
        '                    "Device-encrypted microSD backups will become unreadable.\\n\\n"',
        1,
    )
    controller.write_text(source, encoding='utf-8')
    wallet = app / 'apps/wallets/wallet.py'
    source = wallet.read_text(encoding='utf-8')
    save = '        keystore.save_aead(self.path + "/meta", plaintext=meta)'
    if source.count(save) != 1:
        raise ValueError('Unexpected Specter wallet save')
    wallet.write_text(source.replace(save, save + '\n'
        '        storage = getattr(keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            storage.update(self.path)'), encoding='utf-8')
    manager = app / 'apps/wallets/manager.py'
    source = manager.read_text(encoding='utf-8')
    load = '        self.wallets = self.load_wallets()'
    add = '        self.wallets.append(w)\n        wallet_ids = sorted('
    save_new = '        w.save(self.keystore, path=newpath)'
    delete = '        self.wallets.pop(self.wallets.index(w))\n        w.wipe()'
    if any(source.count(marker) != 1 for marker in (load, add, save_new, delete)):
        raise ValueError('Unexpected Specter wallet manager')
    manager.write_text(source.replace(load,
        '        storage = getattr(keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            storage.restore(self.path)\n' + load).replace(add, '        wallet_ids = sorted(')
        .replace(save_new,
        '        try:\n' + '    ' + save_new + '\n'
        '        except Exception:\n'
        '            platform.delete_recursively(newpath, include_self=True)\n'
        '            raise\n'
        '        self.wallets.append(w)').replace(delete,
        '        storage = getattr(self.keystore, "wallet_flash", None)\n'
        '        if storage is not None:\n'
        '            storage.update(w.path, remove=True)\n' + delete), encoding='utf-8')
    mnemonic_screen = app / 'gui/screens/mnemonic.py'
    source = mnemonic_screen.read_text(encoding='utf-8')
    regenerate = '            self.table.set_mnemonic(generator(wordcount))'
    if source.count(regenerate) != 1:
        raise ValueError('Unexpected Specter mnemonic length toggle')
    mnemonic_screen.write_text(source.replace(
        regenerate,
        '            from rng import RNGError\n'
        '            try:\n'
        '                self.table.set_mnemonic(generator(wordcount))\n'
        '            except RNGError as error:\n'
        '                self.set_value(error)',
    ), encoding='utf-8')
    # Never freeze alternate platform entry points or persistent/smartcard stores.
    for name in ('main.py', 'config_default.py', 'keystore/flash.py',
                 'keystore/sdcard.py', 'keystore/memorycard.py', 'hosts/qr.py'):
        (app / name).unlink(missing_ok=True)
    if PRODUCTION:
        for name in ('forgebox_boot_export.py', 'forgebox_fp_unlock_test.py'):
            (app / name).unlink()
        shutil.rmtree(app / 'keystore/javacard')
    disco = ROOT / 'vendor/specter/f469-disco'
    usermods = build / 'usermods'
    usermods.mkdir(exist_ok=True)
    for name in ('secp256k1', 'uhashlib', 'qrcode', 'uebmit'):
        target = usermods / name
        if not target.exists():
            target.symlink_to(disco / 'usermods' / name, target_is_directory=True)
    # The pre-generated bindings use LVGL 6's original configuration and fonts.
    graphics = disco / 'usermods/udisplay_f469'
    lv_obj = (graphics / 'lvgl/src/lv_core/lv_obj.c').read_text()
    init_flag = 'static bool lv_initialized = false;'
    if lv_obj.count(init_flag) != 1:
        raise ValueError('Unexpected LVGL initialization guard')
    write_if_changed(build / 'lv_obj.c', lv_obj.replace(init_flag, init_flag +
        '\nvoid forgebox_lv_reinit(void) { lv_initialized = false; lv_disp_set_default(NULL); }'))
    heap = (ROOT / 'vendor/forgebox/src/ram/psram_heap_4.c').read_text()
    write_if_changed(build / 'psram_heap_4.c', heap +
        '\nvoid forgebox_psram_heap_reset(void) { pxEnd = NULL; }\n')
    config = (graphics / 'lv_conf.h').read_text()
    # The standalone C diagnostic has no MicroPython GC heap.
    for define in ('#define LV_MEM_CUSTOM      1', '#define LV_ENABLE_GC 1 /* Enable GC for Micropython */'):
        if config.count(define) != 1:
            raise ValueError('Unexpected LVGL allocator configuration')
        native = define.replace('1', '0', 1)
        config = config.replace(
            define, '#if defined(FORGEBOX_SE_PAIRING) || defined(FORGEBOX_BOOT_PROBE)\n' +
            native + '\n#else\n' + define + '\n#endif', 1)
    write_if_changed(ROOT / 'port/lv_conf.h', config)
    # Official driver references a retail event bus only in the unused card IRQ.
    # Keep card I/O; remove the event bus dependency in this generated copy.
    sd = (ROOT / 'vendor/forgebox/src/driver/drv_sdcard.c').read_text()
    sd = sd.replace('#include "user_msg.h"', '')
    sd = sd.replace('PubValueMsg(BACKGROUND_MSG_SD_CARD_CHANGE, 0);', '')
    write_if_changed(build / 'drv_sdcard.c', sd)
    sdio = (ROOT / 'vendor/forgebox/src/driver/drv_sdio.c').read_text()
    # A nonzero response-error mask becomes true in the vendor's bool transfer
    # function, exposing the previous DMA buffer as if the read had succeeded.
    write_if_changed(build / 'drv_sdio.c', sdio.replace('return error;', 'return false;'))
    charger = (ROOT / 'vendor/forgebox/src/driver/drv_aw32001.c').read_text()
    start = charger.index('int32_t Aw32001PowerOff(void)')
    end = charger.index('int32_t Aw32001RefreshState(void)', start)
    power_off = charger[start:end]
    if power_off.count('do {') != 1 or power_off.count('} while (1);') != 1:
        raise ValueError('Unexpected AW32001 shutdown loop')
    power_off = power_off.replace('do {', 'for (unsigned attempt = 0; attempt < 3; attempt++) {')
    power_off = power_off.replace('} while (1);\n    return ret;', '}\n    return -1;')
    write_if_changed(build / 'drv_aw32001.c', charger[:start] + power_off + charger[end:])
    startup = (ROOT / 'vendor/forgebox/external/mh1903_lib/Device/MegaHunt/mhscpu/Source/GCC/startup_mhscpu.s').read_text()
    write_if_changed(build / 'startup.s', '.global Reset_Handler\n' + startup)
    write_if_changed(app / 'git_info.py',
        'REPOSITORY = "3rdIteration/specter-diy"\nBRANCH = "forgebox"\nCOMMIT = "%s"\n' % DEPENDENCIES['specter'])
    write_if_changed(app / 'forgebox_version.py',
        'VERSION = %r\nPRODUCTION = %r\n' % (VERSION, PRODUCTION))
    return disco


def main():
    os.chdir(ROOT)
    disco = prepare()
    run('make', '-C', str(disco / 'micropython/mpy-cross'), '-j4',
        'CFLAGS_EXTRA=-Wno-error')
    profile = 'PRODUCTION=1' if PRODUCTION else 'PRODUCTION=0'
    profile_file = ROOT / 'build/vm-profile'
    if not profile_file.exists() or profile_file.read_text() != profile:
        vm = ROOT / 'build/vm'
        if vm.exists():
            shutil.rmtree(vm)
        profile_file.write_text(profile)
    run('make', '-C', 'port', profile, '../build/vm/genhdr/qstrdefs.generated.h')
    # This older makemanifest caches output without tracking changes to base QSTR IDs.
    (ROOT / 'build/vm/frozen_content.c').unlink(missing_ok=True)
    run('make', '-C', 'port', profile, '-j4')
    se_combined_test = os.environ.get('FORGEBOX_SE_COMBINED_TEST') == '1'
    se_reset_test = se_combined_test or os.environ.get('FORGEBOX_SE_RESET_TEST') == '1'
    se_pin_test = se_reset_test or os.environ.get('FORGEBOX_SE_PIN_TEST') == '1'
    se_storage_write = se_pin_test or os.environ.get('FORGEBOX_SE_STORAGE_WRITE') == '1'
    se_pairing = se_storage_write or os.environ.get('FORGEBOX_SE_PAIRING') == '1'
    fp_enroll_test = os.environ.get('FORGEBOX_FP_ENROLL_TEST') == '1'
    fp_unlock_test = os.environ.get('FORGEBOX_FP_UNLOCK_TEST') == '1'
    fp_unlock = PRODUCTION or os.environ.get('FORGEBOX_FP_UNLOCK') == '1'
    usb_serial = PRODUCTION or os.environ.get('FORGEBOX_USB_SERIAL') == '1'
    boot_export = os.environ.get('FORGEBOX_BOOT_EXPORT') == '1'
    if boot_export and not fp_unlock:
        raise SystemExit('Boot export requires the persistent fingerprint build')
    boot_probe = fp_enroll_test or os.environ.get('FORGEBOX_BOOT_PROBE') == '1'
    if boot_probe and se_pairing:
        raise SystemExit('Boot probe and secure-element pairing tests are separate builds')
    usb_debug = boot_probe or se_pairing or os.environ.get('FORGEBOX_USB_DEBUG') == '1'
    gen1_storage = os.environ.get('FORGEBOX_GEN1_STORAGE', '0' if usb_debug else '1') == '1'
    if gen1_storage and usb_debug:
        raise SystemExit('Gen 1 wallet storage requires a build without diagnostics')
    if PRODUCTION and (usb_debug or boot_export or fp_unlock_test or not gen1_storage):
        raise SystemExit('Diagnostic and legacy options require FORGEBOX_PROFILE=development')
    suffix = ''
    if fp_unlock:
        suffix = '-fingerprint-unlock'
    elif fp_unlock_test:
        suffix = '-fingerprint-unlock-test'
    elif fp_enroll_test:
        suffix = '-fingerprint-enroll-test'
    elif boot_probe:
        suffix = '-boot-probe'
    elif se_combined_test:
        suffix = '-se-combined-test'
    elif se_reset_test:
        suffix = '-se-reset-test'
    elif se_pin_test:
        suffix = '-se-pin-test'
    elif se_storage_write:
        suffix = '-se-storage-write'
    elif se_pairing:
        suffix = '-se-pairing'
    elif usb_debug:
        suffix = '-usb-debug'
    elif not gen1_storage:
        suffix = '-mcu-only'
    if usb_serial:
        suffix += '-usb-serial'
    if boot_export:
        suffix += '-boot-export'
    if PRODUCTION:
        suffix = ''
    firmware = 'build/firmware' + suffix
    if usb_debug or usb_serial:
        sdk = ROOT / 'vendor/forgebox/external/mh1903_lib/SCPU_USB_Lib'
        requests = (sdk / 'SCPU_USB_Device_Library/Core/src/usbd_req.c').read_text()
        # Unsupported optional descriptors need a STALL, including Windows' 0xEE probe.
        requests = requests.replace('if ((len != 0) && (req->wLength != 0)) {',
                                    'if (!pbuf || !len) { USBD_CtlError(pdev, req); return; }\n'
                                    '    if (req->wLength != 0) {')
        write_if_changed(ROOT / 'build/usbd_debug_req.c', requests)
    run('cmake', '-S', '.', '-B', firmware, '-DCMAKE_BUILD_TYPE=Release',
        '-DFORGEBOX_VERSION=' + VERSION,
        '-DFORGEBOX_PRODUCTION=' + ('ON' if PRODUCTION else 'OFF'),
        '-DFORGEBOX_USB_DEBUG=' + ('ON' if usb_debug else 'OFF'),
        '-DFORGEBOX_USB_SERIAL=' + ('ON' if usb_serial else 'OFF'),
        '-DFORGEBOX_BOOT_EXPORT=' + ('ON' if boot_export else 'OFF'),
        '-DFORGEBOX_BOOT_PROBE=' + ('ON' if boot_probe else 'OFF'),
        '-DFORGEBOX_FP_ENROLL_TEST=' + ('ON' if fp_enroll_test else 'OFF'),
        '-DFORGEBOX_FP_UNLOCK_TEST=' + ('ON' if fp_unlock_test else 'OFF'),
        '-DFORGEBOX_FP_UNLOCK=' + ('ON' if fp_unlock else 'OFF'),
        '-DFORGEBOX_SE_PAIRING=' + ('ON' if se_pairing else 'OFF'),
        '-DFORGEBOX_SE_STORAGE_WRITE=' + ('ON' if se_storage_write else 'OFF'),
        '-DFORGEBOX_SE_PIN_TEST=' + ('ON' if se_pin_test else 'OFF'),
        '-DFORGEBOX_SE_RESET_TEST=' + ('ON' if se_reset_test else 'OFF'),
        '-DFORGEBOX_GEN1_STORAGE=' + ('ON' if gen1_storage else 'OFF'),
        '-DFORGEBOX_SE_COMBINED_TEST=' + ('ON' if se_combined_test else 'OFF'))
    run('cmake', '--build', firmware, '-j4')
    run('python3', 'tools/check_image.py', firmware + '/specter.elf')
    dist = ROOT / ('dist' + suffix)
    dist.mkdir(exist_ok=True)
    shutil.copy2(firmware + '/specter.bin', dist / 'mh1903.bin')
    run('python3', str(ROOT / 'vendor/forgebox/tools/padding_bin_file/padding_bin_file.py'),
        'mh1903.bin', cwd=dist)
    import hashlib
    manifest = {'version': VERSION, 'profile': 'production' if PRODUCTION else 'development',
                'storage_version': 1 if PRODUCTION else None, 'usb_debug': usb_debug,
                'usb_serial': usb_serial,
                'boot_export': boot_export,
                'boot_probe': boot_probe,
                'fingerprint_enroll_test': fp_enroll_test,
                'fingerprint_unlock_test': fp_unlock_test,
                'fingerprint_unlock': fp_unlock,
                'se_pairing_diagnostics': se_pairing,
                'se_storage_write_test': se_storage_write,
                'se_pin_retry_test': se_pin_test,
                'se_wallet_reset_test': se_reset_test,
                'se_combined_account_test': se_combined_test,
                'gen1_storage': gen1_storage,
                'dependencies': {}}
    for name in ('specter', 'forgebox'):
        manifest['dependencies'][name] = DEPENDENCIES[name]
    manifest['sha256'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in dist.glob('*.bin')}
    (dist / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
