import sys
import os

simulator = sys.platform != "pyboard"

# to overwrite these settings create a config.py file

if simulator:
    if len(sys.argv) > 1:
        storage_root = sys.argv[1]
    else:
        storage_root = "./fs"
    try:
        os.mkdir(storage_root)
    except:
        pass

else:
    storage_root = ""

# pin that triggers QR code
# if command mode failed
QRSCANNER_TRIGGER = "D2"

# Battery voltage measurement via ADC (new shield board).
# The firmware probes this pin on startup; if the reading falls within a
# plausible Li-Ion voltage range the ADC path is used automatically.
# Set to None in config.py to disable ADC probing entirely.
# Divider ratio R2/(R1+R2) scales Vbat.  With R1=100k, R2=150k the
# ratio is 0.6 → 4.2 V becomes 2.52 V (safe for the 3.3 V ADC ref).
BATTERY_ADC_PIN = "A0"          # ADC pin for the voltage divider
BATTERY_ADC_DIVIDER_RATIO = 0.6 # R2/(R1+R2) for the external divider

# Charging state input pin (active-low from TP4056 CHRG output).
# The TP4056 CHRG pin (pin 7) is open-drain: pulled LOW while charging,
# floating (HIGH-Z) when charge is complete or no USB power.  An
# internal pull-up on the MCU reads HIGH when not charging.
BATTERY_CHARGING_PIN = "A1"     # TP4056 CHRG pin
