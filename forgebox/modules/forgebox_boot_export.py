import os
import asyncio
import forgebox
import platform
from binascii import hexlify
from hashlib import sha256
from gui.screens import Progress

SIZE = 0x81000
NAME = '/sd/forgebox-boot-01000000.bin'
TEMP = NAME + '.part'


async def export_boot_flash(gui):
    if not await gui.prompt('Export boot flash?',
            'Copy the factory boot flash window to microSD for local analysis. '
            'The file may include device-specific data.'):
        return
    progress = Progress('Reading boot flash', 'Writing to microSD.')
    await gui.load_screen(progress)
    data = bytearray(1024)
    digest = sha256()
    offset = 0
    created = False
    try:
        with platform.sdcard:
            if platform.file_exists(NAME) or platform.file_exists(TEMP):
                raise OSError('Move the existing export file off the card first')
            try:
                with open(TEMP, 'wb') as out:
                    created = True
                    for offset in range(0, SIZE, len(data)):
                        if not progress.waiting:
                            return
                        forgebox.boot_flash_read(offset, data)
                        if out.write(data) != len(data):
                            raise OSError('Incomplete microSD write')
                        digest.update(data)
                        if offset % 8192 == 0:
                            progress.set_progress((offset + len(data)) / SIZE)
                            await asyncio.sleep_ms(1)
                platform.sync()
                os.rename(TEMP, NAME)
                platform.sync()
                created = False
            finally:
                if created and platform.file_exists(TEMP):
                    os.remove(TEMP)
    except OSError as error:
        await gui.alert('Boot export failed', 'Block at 0x%08x. %s' % (0x01000000 + offset, error))
        return
    await gui.alert('Boot flash exported', 'forgebox-boot-01000000.bin\n\nSHA-256\n' +
                    hexlify(digest.digest()).decode())
