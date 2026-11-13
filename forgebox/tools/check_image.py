"""Fail a build whose ELF can overwrite the factory bootloader or misplace vectors."""
from pathlib import Path
import struct
import sys
from elftools.elf.elffile import ELFFile

APP_START = 0x01081000
APP_END = 0x01F81000

def require(condition, message):
    if not condition:
        raise ValueError(message)

def check(path):
    with Path(path).open('rb') as f:
        elf = ELFFile(f)
        require(elf['e_machine'] == 'EM_ARM', 'not ARM firmware')
        for seg in elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD' or not seg['p_filesz']:
                continue
            lo, size = seg['p_paddr'], seg['p_filesz']
            require(APP_START <= lo < lo + size <= APP_END, 'load segment outside application region')
        vec = elf.get_section_by_name('.interrupts')
        require(vec and vec['sh_addr'] == APP_START, 'incorrect vector table')
        sp, reset = struct.unpack_from('<II', vec.data())
        require(0x20000000 < sp <= 0x20100000 and sp % 8 == 0, 'invalid stack')
        require(reset & 1 and APP_START <= reset < APP_END, 'invalid reset vector')
        require(elf['e_entry'] == reset, 'ELF entry differs from reset vector')
        print('ELF checked: ARM vectors and all flash load segments remain in application region')

if __name__ == '__main__':
    check(sys.argv[1])
