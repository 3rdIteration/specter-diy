"""Bounded block devices. Only SDCard accesses persistent media."""
import forgebox

class RAMDisk:
    def __init__(self, blocks=2048):
        self.data = bytearray(blocks * 512)

    def _offset(self, block, buf):
        start = block * 512
        if block < 0 or len(buf) % 512 or start + len(buf) > len(self.data):
            raise ValueError("RAM disk block out of range")
        return start

    def readblocks(self, block, buf):
        start = self._offset(block, buf)
        buf[:] = memoryview(self.data)[start:start + len(buf)]
        return 0

    def writeblocks(self, block, buf):
        start = self._offset(block, buf)
        self.data[start:start + len(buf)] = buf
        return 0

    def ioctl(self, op, arg):
        if op == 4:
            return len(self.data) // 512
        if op == 5:
            return 512
        if op in (1, 2, 3):
            return 0
        return None

class SDCard:
    def readblocks(self, block, buf):
        return forgebox.sd_io(False, block, buf)

    def writeblocks(self, block, buf):
        return forgebox.sd_io(True, block, buf)

    def ioctl(self, op, arg):
        if op == 1:
            return 0 if forgebox.sd_init() else -1
        if op in (2, 3):
            return 0
        if op == 4:
            return forgebox.sd_blocks()
        if op == 5:
            return 512
        return None
