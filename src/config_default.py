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

# Detect board type for pin defaults
_board_name = ""
if not simulator:
    try:
        _board_name = os.uname().machine
    except:
        pass

_is_dk2 = "STM32U5G9" in _board_name or "U5G9J-DK2" in _board_name

# pin that triggers QR code scanner
# DK2: QR scanner trigger on Arduino D2 (PF15)
# F469: QR scanner trigger on Arduino D2 (PG13)
# Both boards map this as "D2" in their pins.csv
QRSCANNER_TRIGGER = "D2"

# UART for QR scanner
# DK2: Use UART2 mapped as "YA" (Arduino D5/D6 pins)
# F469: Use UART6 mapped as "YA"
QRSCANNER_UART = "YA"
