# Frozen manifest for STM32U5G9J-DK2 firmware build.
# Includes the DK2-specific manifests from f469-disco and
# freezes the Specter-DIY application code and DK2 boot script.

include('../f469-disco/manifests/disco.py')
freeze('../src')
freeze('../boot/dk2')
