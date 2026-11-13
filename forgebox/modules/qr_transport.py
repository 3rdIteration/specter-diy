"""Bounded decoded-frame transport; no camera/UART side effects.

Uses Specter's pinned microur decoder and QR encoders. Raw QR bytes, including
Compact SeedQR zeros and carriage returns, never pass through text stripping.
"""
import os
from io import BytesIO
from binascii import crc32
from hosts.core import HostError
from microur.decoder import FileURDecoder
from microur.util import bytewords, cbor
from microur.util.ur import decode_hrp, _read_header

MAX_FRAME = 4096
MAX_MESSAGE = 128 * 1024
MAX_PARTS = 256
MAX_STORAGE = 512 * 1024

class QRAccumulator:
    def __init__(self, path):
        self.path = path
        self.result_path = path + '/data'
        self.kind = None
        self.parts = {}
        self.total = None
        self.identity = None
        self.decoder = None
        self.done = False
        self.bcur = False
        self.bcur2 = False

    def _store_result(self, data):
        if not data or len(data) > MAX_MESSAGE:
            raise HostError('QR message is empty or too large')
        with open(self.result_path, 'wb') as f:
            f.write(data)
        self.done = True

    def _claim(self, kind):
        if self.kind is not None and self.kind != kind:
            raise HostError('Different QR formats in one scan; cancel and restart')
        self.kind = kind

    def _part(self, number, total, payload):
        if not 1 <= number <= total <= MAX_PARTS:
            raise HostError('Invalid QR frame number or too many parts')
        if self.total is not None and total != self.total:
            raise HostError('QR part count changed during scanning')
        self.total = total
        if number in self.parts:
            if self.parts[number] != payload:
                raise HostError('Conflicting duplicate QR part')
            return
        if sum(len(p) for p in self.parts.values()) + len(payload) > MAX_MESSAGE:
            raise HostError('QR message exceeds RAM limit')
        self.parts[number] = payload
        if len(self.parts) == self.total:
            data = b''.join(self.parts[i] for i in range(1, self.total + 1))
            if self.bcur:
                data = b'UR:BYTES/' + self.identity + b'/' + data
            self._store_result(data)

    def _modern_ur(self, data):
        self._claim('crypto-psbt')
        self.bcur2 = True
        stream = BytesIO(data)
        ur_type, number, total = decode_hrp(stream)
        if ur_type != 'crypto-psbt' or not 1 <= total <= MAX_PARTS or not 1 <= number <= 0xffffffff:
            raise HostError('Unsupported UR type or invalid sequence')
        # Validate each Bytewords CRC and size header before the fountain decoder
        # can allocate memory or choose a set based on an untrusted sequence count.
        packet = bytewords.decode_check(stream.read())
        if data.count(b'/') == 1:
            message = packet
        else:
            fields = BytesIO(packet)
            sn, count, length, checksum, part_length = _read_header(fields)
            if sn != number or count != total or not 0 < length <= MAX_MESSAGE:
                raise HostError('Invalid UR size/sequence header')
            if not 0 < part_length <= MAX_FRAME or len(fields.read()) != part_length:
                raise HostError('Invalid UR fragment length')
            if not (total - 1) * part_length < length <= total * part_length:
                raise HostError('Inconsistent UR message length')
            identity = (count, length, checksum, part_length)
            if self.identity is not None and identity != self.identity:
                raise HostError('UR message changed during scanning')
            self.identity = identity
            if self.decoder is None:
                self.decoder = FileURDecoder(self.path)
            complete = self.decoder.read_part(BytesIO(data))
            used = sum(os.stat(self.path + '/' + n)[6] for n in os.listdir(self.path))
            if used > MAX_STORAGE:
                raise HostError('UR fragments exceed RAM storage limit')
            if not complete:
                return
            with self.decoder.result() as f:
                message = f.read(MAX_MESSAGE + 1)
            if len(message) != length or crc32(message) != checksum:
                raise HostError('UR message checksum/length mismatch')
        decoded = BytesIO(message)
        length = cbor.read_bytes_len(decoded)
        payload = decoded.read(MAX_MESSAGE + 1)
        if len(payload) != length or not payload.startswith(b'psbt\xff'):
            raise HostError('UR crypto-psbt does not contain a valid PSBT byte string')
        self._store_result(payload)

    def feed(self, data):
        if self.done:
            return True
        if not isinstance(data, bytes) or not data or len(data) > MAX_FRAME:
            raise HostError('Invalid decoded QR frame length')
        upper = data.upper()
        try:
            if upper.startswith(b'UR:CRYPTO-PSBT/'):
                self._modern_ur(data)
            elif upper.startswith(b'UR:BYTES/'):
                self._claim('legacy-bcur')
                self.bcur = True
                sections = data.split(b'/')
                if len(sections) == 4 and b'OF' in sections[1].upper():
                    number, total = sections[1].upper().split(b'OF')
                    identity = sections[2]
                    if self.identity is not None and identity != self.identity:
                        raise HostError('Legacy UR checksum changed')
                    self.identity = identity
                    self._part(int(number), int(total), sections[3])
                else:
                    self._store_result(data)
            elif upper.startswith(b'UR:'):
                raise HostError('This QR transport supports UR crypto-psbt and legacy UR bytes')
            elif data.startswith(b'p') and b' ' in data and b'of' in data.split(b' ', 1)[0]:
                self._claim('specter')
                prefix, payload = data.split(b' ', 1)
                if len(prefix) > 24:
                    raise HostError('Invalid multipart QR prefix')
                number, total = prefix[1:].split(b'of')
                self._part(int(number), int(total), payload)
            else:
                self._claim('raw')
                self._store_result(data)
        except HostError:
            raise
        except (ValueError, AssertionError, IndexError, TypeError):
            raise HostError('Malformed QR frame')
        return self.done

    @property
    def progress(self):
        if self.done:
            return 1
        if self.decoder is not None:
            return self.decoder.progress
        if self.total:
            return len(self.parts) / self.total
        return 0
