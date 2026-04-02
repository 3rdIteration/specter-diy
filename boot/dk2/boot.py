# boot.py -- run on boot-up for STM32U5G9J-DK2
# Adapted from the F469-DISCO boot.py for the DK2 board.
#
# Key differences from F469-DISCO:
#   - No external power hold circuit (DK2 is USB-powered)
#   - User button on PC13 (active high) instead of B1 (falling edge)
#   - 2 LEDs instead of 4 (LED1=PD2 green, LED2=PD4 red)
#   - Battery monitor on I2C2 (Specter Shield) instead of I2C1
#   - I2C1 is used for touch controller on DK2

import pyb, os, micropython, time
import sys

# Clean sys.path from qspi
# Shouldn't happen in production, but just in case.
for p in sys.path:
    if "qspi" in p:
        sys.path.remove(p)

# DK2 is USB-powered; no external power hold circuit.
# On a custom enclosure with battery, connect power hold to an available GPIO
# and uncomment:
# pwr = pyb.Pin("XX", pyb.Pin.OUT)
# pwr.on()

version = "<version:tag10>0100900099</version:tag10>"

# I2C2 for Specter Shield battery monitor (if connected via Arduino headers)
# I2C1 is reserved for the DK2's onboard touch controller
i2c = pyb.I2C(2)
i2c.init()
# start battery measurements if battery monitor present
if 112 in i2c.scan():
    i2c.mem_write(0b00010000, 112, 0)

# DK2 has 2 user LEDs (we reference them as 1-2; pyb.LED wraps safely)
leds = [pyb.LED(i) for i in range(1, 3)]

# poweroff on button press (user button PC13, active high, rising edge)
def pwrcb(e):
    micropython.schedule(poweroff, 0)

# callback scheduled from the interrupt
def poweroff(_):
    # make sure it disables power no matter what
    try:
        for led in leds:
            led.toggle()
        # stop battery management
        if 112 in i2c.scan():
            i2c.mem_write(0, 112, 0)
        # sync filesystem
        os.sync()
        time.sleep_ms(300)
    finally:
        # On DK2 without power hold, we just do a hard reset
        # If power hold is wired, add: pwr.off()
        pyb.hard_reset()
    time.sleep_ms(300)
    # will never reach here
    for led in leds:
        led.toggle()

pyb.ExtInt(pyb.Pin('C13'), pyb.ExtInt.IRQ_RISING, pyb.Pin.PULL_NONE, pwrcb)

# configure usb from start if you want,
# otherwise will be configured after PIN
# pyb.usb_mode("VCP+MSC") # debug mode with USB and mounted storages from start
# pyb.usb_mode("VCP") # debug mode with USB from start
# disable at start
pyb.usb_mode(None)
os.dupterm(None, 0)
os.dupterm(None, 1)

# inject version and i2c to platform module
import platform
platform.version = version
platform.i2c = i2c
