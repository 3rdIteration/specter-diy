from pathlib import Path
import json
import struct
import sys
import pytest
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_image import check

ELF = ROOT / 'build/firmware/specter.elf'

def test_valid_application():
    check(ELF)

def test_default_image_uses_gen1_storage():
    manifest = json.loads((ROOT / 'dist/build.json').read_text())
    assert manifest['profile'] == 'production' and manifest['storage_version'] == 1
    assert manifest['version'] == '1.10.5-forgebox'
    assert manifest['gen1_storage'] and manifest['fingerprint_unlock'] and manifest['usb_serial']
    assert not manifest['usb_debug'] and not manifest['boot_export']
    data = ELF.read_bytes()
    assert b'Firmware v1.10.5-forgebox' in data
    assert b'ForgeBox Specter Gen 1 storage v1 release' in data
    assert b'FW01 ForgeBox Specter storage' in data
    assert b'Scan SPI flash (read-only)' not in data
    assert b'forgebox_fp_feedback.py' in data
    assert b'forgebox_fp_unlock_test.py' not in data
    assert b'ForgeBox BIP39 entropy v1' in data

def test_no_host_fixtures_or_provisioning_linked():
    data = ELF.read_bytes()
    assert b'native_smoke.py' not in data
    assert b'board_test_sd_insert' not in data
    assert b'board_test_camera_frame' not in data
    with ELF.open('rb') as f:
        symbols = {s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    assert {'Gd25FlashSectorErase', 'Gd25FlashWriteBuffer'} <= symbols
    assert not symbols.intersection({
        'Gd25FlashBlockErase', 'Gd25FlashChipErase', 'Gd25FlashWriteBufferNoMutex',
        'QspiFlashEraseAndWrite', 'WriteOtpData', 'DS28S60_Init', 'DS28S60_WritePage',
        'Atecc608bInit', 'Atecc608bWriteConfigGen2', 'TamperStartup', 'board_test_sd_insert',
    })

def test_no_debug_transport_linked():
    with ELF.open('rb') as f:
        symbols = {s.name for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}
    assert 'USBD_Init' in symbols
    assert not symbols.intersection({
        'board_boot_flash_read', 'board_boot_probe', 'board_se_pairing_report',
        'pin_flash_write', 'seed_flash_write', 'wallet_spi_erased_sectors',
        'pyexec_friendly_repl', 'pyexec_raw_repl', 'pyexec_event_repl_init',
    })

def test_rejects_bootloader_load_segment(tmp_path):
    data = bytearray(ELF.read_bytes())
    with ELF.open('rb') as f:
        elf = ELFFile(f)
        for i, seg in enumerate(elf.iter_segments()):
            if seg['p_type'] == 'PT_LOAD':
                struct.pack_into('<I', data, elf['e_phoff'] + i * elf['e_phentsize'] + 12, 0x01000000)
                break
    bad = tmp_path / 'boot-overlap.elf'
    bad.write_bytes(data)
    with pytest.raises(ValueError, match='outside application'):
        check(bad)

@pytest.mark.parametrize('reset', [0, 0x01002001, 0x01083000])
def test_rejects_invalid_reset_vector(tmp_path, reset):
    data = bytearray(ELF.read_bytes())
    with ELF.open('rb') as f:
        offset = ELFFile(f).get_section_by_name('.interrupts')['sh_offset']
    struct.pack_into('<I', data, offset + 4, reset)
    bad = tmp_path / 'bad-reset.elf'
    bad.write_bytes(data)
    with pytest.raises(ValueError, match='reset vector'):
        check(bad)
