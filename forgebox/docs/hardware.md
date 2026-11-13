# ForgeBox hardware and Specter-DIY implementation

The reference unit reports board ADC revision **v3.1**. Its security configuration and observed peripherals match the Keystone 3 Gen 1 code in the areas listed below. Board revisions and factory provisioning can differ. The factory Flash map and production bootloader implementation are unpublished. Addresses below come from the vendor example, Keystone 3 source, the checked-in port, a read-only scan and disassembly of this unit's bootloader.

## System and peripherals

| Area | Configuration and port |
| --- | --- |
| MCU and runtime | MegaHunt MH1903S Cortex-M4, FreeRTOS and one MicroPython VM task. The port uses the upstream Specter LVGL 6 bindings, embit and native secp256k1. The release pin is Specter v1.10.5, including its transaction change classification and mixed-input warnings. |
| Memory | Internal SRAM is 1 MiB and holds FreeRTOS, DMA bounce buffers and the VM stack. The vendor PSRAM driver configures 8 MiB. The port uses PSRAM for the VM heap and workspace, and mounts a 1 MiB FAT RAM filesystem. Specter's `/flash` and `/qspi` path names inside the VM map to RAM. Native storage routines handle persistent records. |
| Display and touch | 480×800 LCD through the vendor parallel display driver. The port converts LVGL pixels to byte-swapped RGB565 and uses the vendor touch driver detection for CST726, FT6336, GSL1691 and CST3xx. Display, taps and touch tracking were exercised on the reference unit. |
| Camera | DCMI capture and the pinned Keystone QR decoder library. Camera SCCB shares I²C0 with touch. The driver restores that bus for touch polling. It renders an enlarged live preview and handles Specter multipart and UR PSBT input through the normal wallet UI. QR capture worked on the reference unit. The binary decoder comes from the vendor example. |
| microSD | Vendor SDIO with aligned SRAM DMA bounce buffers and bounded mount lifetimes. The port handles FAT32 and closes and unmounts after each request. SDIO response errors are treated as failures so stale DMA data cannot be reported as a successful read. PSBT import and device-bound seed backup use the card. |
| USB | USB-C CDC ACM device mode. The Specter transport implements line-oriented requests and ACK/reply framing. It advertises the Specter identity `f055:9802` used by async-hwi. USB wallet commands require an enabled transport and a loaded wallet. Earlier builds passed Liana pairing and multisig registration with persistence across reboot. USB host/OTG peripherals and bootloader DFU remain unverified. |
| Battery and power | AW32001 charger state with Keystone ADC conversion and charge/discharge curves. Displayed percentage is an estimate. Shutdown uses the charger power-off path, with USB power disconnected. The application keeps a power-button recovery task separate from the VM. |
| Tamper and debug | PA2 is the live active-low tamper input. PF14 is the fingerprint IRQ. Diagnostics report their current levels and JTAG/core halting status. The application does not set irreversible debug protection or enforce a tamper response. Factory bootloader behavior is documented separately. |
| ATECC608B | I²C address `0x60`, revision reply `00006003` on the reference unit. Factory CheckMac with the OTP-backed slot 1 credential passed. The Gen 1 application uses account slots 10–12 for PIN authorization, rolling and KDF operations. Locked configuration and counter headroom are checked before account access. |
| DS28S60 | SPI secure element. Authenticated reads with Secret A succeeded using the factory OTP-backed pairing. The reference chip returned `040c` for user blocks and `0003/0001/0001/0001` for Secret A/B/C/D blocks. Alternate-secret provisioning is outside the port. The factory binding page is 91. |
| Fingerprint module | MAX32520-based subsystem on UART2 at 115200, with PF14 IRQ and PE11/PE12 control. The encrypted command path uses the factory communication key on DS28S60 page 82. This unit answered the encrypted ECB command path. Enrollment, match versus non-match, deletion, and returning the same wallet key after cold boot were exercised on earlier builds. |

Sources for the board implementation are the [ForgeBox example](https://github.com/KeystoneHQ/forgebox-helloworld) and [Keystone 3 firmware](https://github.com/KeystoneHQ/keystone3-firmware). The separate [bootloader analysis](bootloader.md) covers the boot chain, OTA format, verification-failure handoff, PA2 branch, wipe and genuine check.

## Flash map

QSPI is the CPU-mapped boot/application flash. The storage chip exposed to Specter is separate **16 MiB SPI NOR**. The tested JEDEC ID was `852018` and the driver also recognizes `C84018`. An erased-sector scan found 2,541 of 4,096 erased sectors, including `0x908000–0xE9FFFF`. Erase state describes the sampled contents. The production layout uses specific checked ranges and leaves the rest of the chip available to factory firmware or other owners.

| Area | Range or offset |
| --- | --- |
| QSPI bootloader | `0x01000000–0x01080FFF` |
| QSPI application vectors, version and text | `0x01081000`, `0x01082000`, `0x01083000` |
| QSPI installed signature record | `0x01FFF000` |
| SPI device-side staging filesystem | Starts at zero. The retail map reserves the first 8 MiB. |
| SPI boot parameter record | `0xF6D800`, inside erase sector `0xF6D000` |
| SPI descriptor and settings A/B banks | `0xE60000–0xE6FFFF`, two 32 KiB banks |
| SPI erase journal | `0xEAC000–0xEACFFF` |
| SPI encrypted seed catalog A/B | `0xEAE000–0xEAFFFF`, two 4 KiB sectors |
| SPI former development PIN sectors | `0xEB0000–0xEB1FFF`. Production expects them blank and stores the PIN authorization in the secure-element flow. |

The application validates JEDEC identity and record ownership, programs a commit byte last, and journals interrupted sector erases. Record and readback checks are limited to known locations. It does not expose a free-space scan in the production UI. A full-chip factory recovery wipe covers all of these SPI areas.

## Wallet storage v1

The first release uses fresh records with distinct v1 markers. `FS01` identifies the named seed catalog and `FW01` the descriptor/settings record and journal. The DS28S60 account marker is a domain-separated, device-specific HMAC for storage v1. The version marker is independent of the upstream firmware version. Fresh enrollment requires blank account and storage records. An older build must clear any wallet it owns using its in-app wipe. The new image does not migrate or silently reset old records.

- DS28S60 pages 5 and 11 hold account state, remaining PIN attempts, and a wallet-specific contribution. Pages 44–47 hold PIN-change wrapping banks. Pages 72 and 84 hold fingerprint credential and enrollment metadata. The production wallet does not provision OTP, rewrite chip configuration, change locks, or write the factory fingerprint communication key or DS28S60 binding page.
- The startup PIN uses PBKDF2-HMAC-SHA256 with 600,000 rounds and a per-wallet salt. The device secret combines the authorized ATECC KDF output and the DS28S60 contribution. The firmware consumes one of eight authenticated retry credits before attempting the PIN and restores the budget after a successful PIN. The last wrong PIN starts a resumable wallet wipe. The retry budget lives in the DS28S60 record and the firmware enforces it. The reference ATECC CountMatch setting was disabled.
- PIN change journals the wrapped device secret so existing encrypted records stay usable. PIN change revokes fingerprint unlock. Optional fingerprint enrollment is PIN-authorized and uses key material returned by the module to unwrap a PIN-derived credential and run the same device-secret unlock after a successful match. The PIN remains a recovery method when the biometric path is unavailable.
- BIP39 generation mixes two fresh samples apiece from MCU, ATECC608B and DS28S60 into domain-separated SHA-256 with the device secret and a PIN-derived context. A failed source aborts generation. Fingerprint unlock restores the context needed by the mixer without storing the raw PIN.
- The port stores up to 16 named seed entries in the encrypted seed catalog. It encrypts descriptors, labels, device settings and host settings using Specter's authenticated-encryption helpers. Descriptors are grouped by seed identity and Bitcoin network. Both 32 KiB banks hold alternate generations of the same catalog. A device-encrypted microSD backup needs this wallet's device secret, and BIP39 passphrases are entered separately.
- Transaction signing and the active recovery phrase live in the MCU/VM after unlock. The secure elements provide device-key wrapping and authorization. The factory recovery handoff, unsigned-boot exceptions, physical tamper behavior, and firmware rollback policy remain outside the application. A prior probe matched the MH1903 production ROM and observed RSA-2048 selection and debug disabled at runtime. Physical fault-injection resistance and attacks with extracted OTP and access to the original secure elements were not validated.

## Validation boundary

Earlier hardware builds exercised the display/touch, QR scan, microSD, battery, power-off and lock, Gen 1 enrollment and wipe, PIN change, BIP85, cold-boot fingerprint unlock, Liana discovery and registered wallet persistence. These observations are specific to the reference device and those builds. For the v1 production build, check a fresh PIN enrollment and two cold boots, confirm saved seeds and descriptors, exercise PIN change and fingerprint re-enrollment, verify an independently checked disposable PSBT signature, and verify that in-app wipe returns to fresh setup. Verify device-bound backups before erasing any prior wallet. Factory recovery wipe is a separate operation with a broader scope.
