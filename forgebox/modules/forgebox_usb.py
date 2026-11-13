import forgebox
from time import ticks_ms, ticks_diff
from binascii import hexlify
from hosts.usb import USBHost
from hosts import Host

MAX_COMMAND = 256 * 1024


class SerialIO:
    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        for start in range(0, len(data), 32):
            forgebox.usb_serial_write(data[start:start + 32])


class ForgeBoxUSBHost(USBHost):
    def init(self):
        self.usb = SerialIO()
        self.length = 0
        self.last_rx = 0
        self.discard = False
        self.skip_lf = False

    def load_settings(self, keystore):
        storage = getattr(keystore, 'wallet_flash', None)
        if storage is not None and not keystore.is_locked:
            self.settings = storage.load_device_setting('host/usb', self.settings)
        return self.settings

    def save_settings(self, keystore):
        keystore.wallet_flash.save_device_setting('host/usb', self.settings)

    async def settings_menu(self, show_screen, keystore):
        await Host.settings_menu(self, show_screen, keystore)
        self.cleanup()
        forgebox.usb_serial_enable(self.is_enabled and self.enabled and keystore.is_ready)

    async def enable(self):
        await Host.enable(self)
        forgebox.usb_serial_enable(self.is_enabled and self.manager.keystore.is_ready)

    async def disable(self):
        forgebox.usb_serial_enable(False)
        self.cleanup()
        await super().disable()

    def cleanup(self):
        super().cleanup()
        self.length = 0
        self.discard = False

    def read_to_file(self):
        if (self.length or self.discard) and ticks_diff(ticks_ms(), self.last_rx) > 5000:
            self.cleanup()
        packet = forgebox.usb_serial_read()
        if not packet:
            return None
        self.last_rx = ticks_ms()
        # Ignore the upstream client's leading empty lines, including CRLF
        # split across two USB packets.
        if self.skip_lf and packet.startswith(b'\n'):
            packet = packet[1:]
        self.skip_lf = False
        packet = packet.replace(b'\r\n', b'\n')
        self.skip_lf = packet.endswith(b'\r')
        packet = packet.replace(b'\r', b'\n')
        if not self.length and not self.discard:
            packet = packet.lstrip(b'\n')
        chunk, sep, _ = packet.partition(b'\n')
        if not chunk and not sep:
            return None
        if self.length + len(chunk) > MAX_COMMAND:
            self.cleanup()
            self.discard = True
        if self.discard:
            if sep:
                self.respond(self.ACK.rstrip())
                self.respond(b'error: Request too large')
                self.cleanup()
            return None
        if self.f is None:
            self.f = open(self.path + '/data', 'wb')
        self.f.write(chunk)
        self.length += len(chunk)
        if sep:
            self.f.close()
            self.f = None
            return self.path + '/data'

    async def update(self):
        if not self.is_enabled or not self.manager.keystore.is_ready or not forgebox.usb_serial_connected():
            if self.length or self.discard or self.f is not None:
                self.cleanup()
            return
        try:
            await super().update()
        except OSError:
            self.cleanup()

    async def process_command(self, stream):
        keystore = self.manager.keystore
        if not self.is_enabled or not keystore.is_ready:
            self.respond(b'error: Wallet is locked')
            return
        # Liana polls this public identifier twice when opening a connection.
        # Avoid synchronous LCD redraws on its time-limited discovery path.
        if stream.read(12) == b'fingerprint':
            self.respond(hexlify(keystore.fingerprint))
            return
        stream.seek(0)
        await super().process_command(stream)
