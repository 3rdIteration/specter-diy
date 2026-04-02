#!/bin/bash
# Build firmware for STM32U5G9J-DK2
#
# This script builds the Specter-DIY firmware targeting the DK2 board.
# Unlike the F469-DISCO build, this does not yet include the secure bootloader
# (which needs to be ported separately for the STM32U5 family).

INFO="\e[1;36m"
ENDCOLOR="\e[0m"

echo -e "${INFO}
══════════════════════ Building DK2 firmware ═══════════════════════════════
${ENDCOLOR}"
make clean
make dk2

echo -e "${INFO}
══════════════════════ Firmware build complete ═════════════════════════════
${ENDCOLOR}"

if [ -f bin/specter-diy-dk2.bin ]; then
    echo "DK2 firmware binary: bin/specter-diy-dk2.bin"
    echo "DK2 firmware hex:    bin/specter-diy-dk2.hex"
    echo ""
    echo "To flash via ST-Link:"
    echo "  st-flash write bin/specter-diy-dk2.bin 0x08000000"
    echo ""
    echo "Or via STM32CubeProgrammer:"
    echo "  STM32_Programmer_CLI -c port=SWD -w bin/specter-diy-dk2.bin 0x08000000"
    echo ""

    mkdir -p release
    cp bin/specter-diy-dk2.bin release/
    cp bin/specter-diy-dk2.hex release/

    cd release
    sha256sum specter-diy-dk2.* > sha256_dk2.txt
    cat sha256_dk2.txt
    echo ""
    echo "Hashes saved to release/sha256_dk2.txt"
else
    echo "ERROR: Build failed - firmware binary not found"
    exit 1
fi
