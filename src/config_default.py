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

# Battery voltage measurement via ADC (optional, for new shield board).
# Set to an ADC-capable pin name (e.g. "A0") to enable ADC-based battery
# monitoring instead of the I2C fuel-gauge IC.  The voltage divider on
# the shield should scale the battery voltage so the full-charge value
# (4.2 V) maps to ≤ 3.3 V at the ADC input.
# Divider ratio R2/(R1+R2) scales Vbat.  With R1=100k, R2=150k the
# ratio is 0.6 → 4.2 V becomes 2.52 V (safe for the 3.3 V ADC ref).
BATTERY_ADC_PIN = None          # e.g. "A0" – set in config.py
BATTERY_ADC_DIVIDER_RATIO = 0.6 # R2/(R1+R2) for the external divider

# Charging state input pin (active-low from charger STAT output).
# When the charger IC is charging the battery the STAT pin is pulled
# low; it floats (pulled high externally) when charging is complete or
# no battery is present.
BATTERY_CHARGING_PIN = None     # e.g. "A1" – set in config.py
