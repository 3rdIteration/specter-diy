# boot.py -- run on boot-up for STM32F746G-Discovery
# This board does not have a power hold circuit or battery management.
import pyb, os, micropython, time
import sys

# Clean sys.path from qspi
for p in sys.path:
    if "qspi" in p:
        sys.path.remove(p)

version = "<version:tag10>0100900099</version:tag10>"

# STM32F746G-Discovery has a single green LED on PI1
leds = [pyb.LED(i) for i in range(1, 2)]

# User button on PI11 (active high)
# No power-off functionality on this board (no battery/power hold)
def usrbtn_cb(e):
    for led in leds:
        led.toggle()

pyb.ExtInt(pyb.Pin('SW'), pyb.ExtInt.IRQ_RISING, pyb.Pin.PULL_NONE, usrbtn_cb)

# configure usb from start if you want,
# otherwise will be configured after PIN
# pyb.usb_mode("VCP+MSC") # debug mode with USB and mounted storages from start
# pyb.usb_mode("VCP") # debug mode with USB from start
# disable at start
pyb.usb_mode(None)
os.dupterm(None, 0)
os.dupterm(None, 1)

# inject version to platform module
# Note: no i2c battery management on F746G-Discovery
import platform
platform.version = version
platform.i2c = None
