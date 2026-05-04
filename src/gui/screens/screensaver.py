"""Screensaver screen: Specter logo bouncing around a black background."""
import lvgl as lv
import asyncio
from ..common import HOR_RES, VER_RES
from ..logo_data import get_logo_data, LOGO_W, LOGO_H

# How far the logo can travel
_MAX_X = HOR_RES - LOGO_W
_MAX_Y = VER_RES - LOGO_H

# Animation speed (pixels per frame, at ~30 ms/frame)
_SPEED_X = 3
_SPEED_Y = 2


class ScreenSaver(lv.obj):
    """
    Full-screen black overlay with a bouncing Specter logo.
    Resolves (returns True) when the user taps the screen.
    """

    def __init__(self):
        super().__init__()
        self.waiting = True

        # Black background – fill the whole screen
        self._bg_style = lv.style_t()
        lv.style_copy(self._bg_style, lv.style_plain_color)
        self._bg_style.body.main_color = lv.color_hex(0x000000)
        self._bg_style.body.grad_color = lv.color_hex(0x000000)
        self._bg_style.body.opa = lv.OPA.COVER
        self._bg_style.body.radius = 0
        self._bg_style.body.border.width = 0
        self.set_style(self._bg_style)
        self.set_size(HOR_RES, VER_RES)
        self.set_pos(0, 0)
        self.set_click(True)
        self.set_event_cb(self._on_touch)

        # Build the LVGL image descriptor from compressed logo bytes.
        # self._raw keeps the bytearray alive so the GC does not free it while
        # LVGL still holds a C pointer into the buffer.
        self._raw = get_logo_data()
        self._img_dsc = lv.img_dsc_t()
        self._img_dsc.header.always_zero = 0
        self._img_dsc.header.w = LOGO_W
        self._img_dsc.header.h = LOGO_H
        self._img_dsc.header.cf = lv.img.CF.TRUE_COLOR_ALPHA
        self._img_dsc.data_size = len(self._raw)
        self._img_dsc.data = self._raw

        # Image widget
        self._img = lv.img(self)
        self._img.set_src(self._img_dsc)
        self._img.set_click(False)  # touches should reach the parent

        # Starting position and velocity
        self._x = _MAX_X // 4
        self._y = _MAX_Y // 4
        self._vx = _SPEED_X
        self._vy = _SPEED_Y
        self._img.set_pos(self._x, self._y)

    # ------------------------------------------------------------------
    # LVGL event callback
    # ------------------------------------------------------------------

    def _on_touch(self, obj, event):
        if event == lv.EVENT.RELEASED:
            self.waiting = False

    # ------------------------------------------------------------------
    # Animation
    # ------------------------------------------------------------------

    def tick(self):
        """Advance logo by one frame and return current (x, y)."""
        self._x += self._vx
        self._y += self._vy

        if self._x <= 0:
            self._x = 0
            self._vx = _SPEED_X
        elif self._x >= _MAX_X:
            self._x = _MAX_X
            self._vx = -_SPEED_X

        if self._y <= 0:
            self._y = 0
            self._vy = _SPEED_Y
        elif self._y >= _MAX_Y:
            self._y = _MAX_Y
            self._vy = -_SPEED_Y

        self._img.set_pos(self._x, self._y)
        return self._x, self._y

    # ------------------------------------------------------------------
    # Result awaitable
    # ------------------------------------------------------------------

    async def result(self):
        while self.waiting:
            self.tick()
            await asyncio.sleep_ms(30)
        return True
