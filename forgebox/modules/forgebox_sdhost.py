import os
import platform
from hosts import HostError, SDHost


class ForgeBoxSDHost(SDHost):
    def reset_and_mount(self):
        try:
            super().reset_and_mount()
        except OSError as error:
            raise HostError('Could not mount the microSD card. Use a FAT32 card. (%s)' % error)

    async def select_file(self, extensions):
        try:
            files = sorted(name for name, kind, *_ in os.ilistdir(self.sdpath)
                           if kind == 0x8000 and not name.startswith('.')
                           and any(name.lower().endswith(ext) for ext in extensions))
        except OSError as error:
            raise HostError('Could not list files on the microSD card. (%s)' % error)
        if not files:
            raise HostError('No matching files on microSD. Allowed: %s' % ', '.join(extensions))
        buttons = [(self.sdpath + '/' + name, self.truncate(name)) for name in files]
        try:
            return await self.manager.gui.menu(buttons, title='Select a file', last=(None, 'Cancel'))
        except Exception as error:
            raise HostError('Could not open the microSD file picker. (%s)' % error)

    async def get_data(self, raw=False, chunk_timeout=0.1):
        try:
            return await super().get_data(raw, chunk_timeout)
        except OSError as error:
            raise HostError('Could not read the selected microSD file. (%s)' % error)
