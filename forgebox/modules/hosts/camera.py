import asyncio
import gc
import os
from time import ticks_ms, ticks_diff
import forgebox
import platform
from .core import Host, HostError
from qr_transport import QRAccumulator
from helpers import a2b_base64_stream

START_ERRORS = {
    90: 'Could not allocate camera preview memory',
    101: 'The camera decoder license check failed',
    102: 'The camera decoder rejected its memory pool',
    103: 'The camera sensor did not initialize',
}
READ_ERRORS = {
    201: 'The camera decoder result did not fit in its buffer',
    202: 'The camera decoder had no complete frame',
    203: 'The camera decoder was not initialized',
    211: 'The camera is inactive',
    212: 'The camera result exceeded the buffer',
    213: 'The camera reported a capture error',
    214: 'No camera frame arrived within two seconds',
}

class CameraHost(Host):
    def load_settings(self, keystore):
        storage = getattr(keystore, 'wallet_flash', None)
        if storage is None:
            return super().load_settings(keystore)
        self.settings = storage.load_device_setting('camera', self.settings)
        return self.settings

    def save_settings(self, keystore):
        storage = getattr(keystore, 'wallet_flash', None)
        if storage is None:
            return super().save_settings(keystore)
        storage.save_device_setting('camera', self.settings)

    button = 'Scan QR code'
    settings_button = 'Camera QR scanner'
    RECOVERY_TIME = 0
    MAX_SCAN_MS = 90000

    def __init__(self, path):
        super().__init__(path)
        self.scanning = False
        self.cancelled = False
        self.accumulator = None
        self.f = None
        self.bcur = False
        self.bcur2 = False

    @property
    def in_progress(self):
        return self.scanning

    @property
    def progress(self):
        return self.accumulator.progress if self.accumulator else 0

    def abort(self):
        self.cancelled = True
        self.scanning = False
        forgebox.camera_stop()

    async def disable(self):
        self.abort()
        await super().disable()

    async def get_data(self, raw=True, chunk_timeout=None):
        if self.scanning:
            raise HostError('Camera is already scanning')
        forgebox.camera_ui_phase(1)
        if self.f is not None:
            self.f.close()
            self.f = None
        platform.delete_recursively(self.path)
        self.accumulator = QRAccumulator(self.path)
        self.scanning = True
        self.cancelled = False
        preview = None
        started = ticks_ms()
        try:
            try:
                forgebox.camera_start()
            except OSError as error:
                code = error.args[0]
                raise HostError('%s (camera code %s)' %
                                (START_ERRORS.get(code, 'Could not start the camera'), code))
            if self.manager is not None:
                forgebox.camera_ui_phase(2)
                preview = await self.manager.gui.show_camera_progress(self)
            forgebox.camera_ui_phase(3)
            while self.scanning:
                if ticks_diff(ticks_ms(), started) >= self.MAX_SCAN_MS:
                    raise HostError('Camera scan timed out; reposition the QR code and retry')
                try:
                    data = forgebox.camera_read()
                except OSError as error:
                    code = error.args[0]
                    raise HostError('%s (camera code %s)' %
                                    (READ_ERRORS.get(code, 'Could not read the camera'), code))
                if preview is not None:
                    preview.refresh_preview()
                if data is not None and self.accumulator.feed(data):
                    self.scanning = False
                    break
                await asyncio.sleep_ms(20)
        finally:
            self.scanning = False
            forgebox.camera_stop()
            if self.cancelled or not self.accumulator.done:
                platform.delete_recursively(self.path)
        if self.cancelled:
            self.accumulator = None
            return None
        self.bcur, self.bcur2 = self.accumulator.bcur, self.accumulator.bcur2
        self.f = open(self.accumulator.result_path, 'rb')
        self.accumulator = None
        gc.collect()
        return self.f

    async def send_data(self, stream, meta=None, *args, **kwargs):
        # Use upstream encoders and QR display for the matching response format.
        meta = meta or {}
        if isinstance(stream, str):
            with open(stream, 'rb') as f:
                return await self.send_data(f, meta)
        start = stream.read(4)
        stream.seek(-len(start), 1)
        if start in (b'cHNi', b'cHNl'):
            with open(self.path + '/signed_raw', 'wb') as f:
                a2b_base64_stream(stream, f)
            return await self.send_data(self.path + '/signed_raw', meta)
        title, note = meta.get('title', 'Your data:'), meta.get('note')
        if start not in (b'psbt', b'pset'):
            response = stream.read().decode()
            return await self.manager.gui.qr_alert(title, meta.get('message', response), response,
                                                   note=note, qr_width=480)
        if self.bcur2:
            from qrencoder import CryptoPSBTEncoder as Encoder
        elif self.bcur:
            from qrencoder import LegacyBCUREncoder as Encoder
        else:
            from qrencoder import Base64QREncoder as Encoder
        with Encoder(stream, tempfile=self.path + '/qrtmp') as encoder:
            await self.manager.gui.qr_alert(title, '', encoder, note=note, qr_width=480)
