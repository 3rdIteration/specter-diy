import asyncio
import lvgl as lv
from gui.screens import Progress
from gui.specter import SpecterGUI
from gui.components.battery import Battery

class CameraProgress(Progress):
    def __init__(self):
        super().__init__('Scan QR code', 'Hold the QR code steady inside the preview.')
        self.arc.set_hidden(True)
        self.preview_data = bytearray(480 * 480 * 4)
        self.preview_descriptor = lv.img_dsc_t()
        self.preview_descriptor.data = self.preview_data
        self.preview_descriptor.data_size = len(self.preview_data)
        self.preview_descriptor.header.cf = lv.img.CF.TRUE_COLOR
        self.preview_descriptor.header.w = 480
        self.preview_descriptor.header.h = 480
        self.preview_image = lv.img(self)
        self.preview_image.set_src(self.preview_descriptor)
        self.preview_image.set_pos(0, 96)
        self.message.set_parent(self)
        self.message.set_pos(20, 595)
        self.progress.set_pos(20, 653)

    def tick(self, d=10):
        pass

    def refresh_preview(self):
        import forgebox
        if forgebox.camera_preview(self.preview_data):
            self.preview_image.invalidate()

class ForgeBoxGUI(SpecterGUI):
    async def update_battery(self, interval):
        # Retry a transiently unavailable sample instead of stopping the
        # monitor. Also refresh the screen behind a popup before it is reused.
        while True:
            Battery.VALUE, Battery.CHARGING = self.battery_callback()
            for screen in (self.scr, self.background):
                if screen is not None and hasattr(screen, 'battery'):
                    screen.battery.update()
            await asyncio.sleep_ms(interval)

    async def show_camera_progress(self, host):
        screen = CameraProgress()
        await self.open_popup(screen)
        asyncio.create_task(self.coro(host, screen))
        return screen
