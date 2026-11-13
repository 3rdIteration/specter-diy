# ForgeBox v1.0.1 bootloader analysis

## Image and method

A read-only export from the reference ForgeBox contains 528,384 bytes, mapped at `0x01000000–0x01080FFF`. Its SHA-256 is `91550907260100ff30648483955cc3c76624c963346da0c42288998c977e5d67`. The header begins with the little-endian word `0x5555AAAA`. The Cortex-M vector table starts at `0x01001000`, with an initial stack pointer of `0x20018FF0` and a Thumb reset vector of `0x01027AFD`. The image identifies itself as `Boot v1.0.1`. The final 64 KiB repeat a 32-byte pattern. Its purpose is unresolved.

The analysis uses Capstone to disassemble ARM Thumb instructions and reconstruct the relevant control flow by hand. The addresses below refer to the memory-mapped image. The export reads through the CPU’s application-visible QSPI mapping. It contains the flash-resident bootloader and its header. The immutable MH1903 mask ROM, live OTP contents, installed application and signature sector are outside this file. No firmware or device state was modified during this analysis. [ForgeBox flash map and recovery documentation](https://github.com/KeystoneHQ/forgebox)

## Bootloader authentication and application signing

The earlier on-device probe reported the matching MH1903 mask-ROM fingerprint, a programmed production marker, and the RSA-2048 selector. Together with the recognized image header, this supports the vendor-provisioned RSA bootloader chain. The OEM signing identity, exact root public key and signature verification over this particular export were not independently established. The earlier probe also observed JTAG control and test-ROM control at zero and core halting debug disabled at runtime. Those readings cover the sampled boot state. [MH1903 mask-ROM analysis](https://lucasteske.dev/2026/04/reverse-engineering-air105-bootrom-mh1903s-boot-process)

The bootloader selects **one secp256k1 application verification key** in `0x01012020`. It powers up the OTP interface and scans sixteen 64-byte records, starting at `0x40009FC0` and descending to `0x40009C00`. `0x40009BC0` is the loop boundary and is excluded. It skips all-`FF` records. The first other record ends the scan. If that record consists entirely of `00` and `FF` bytes, or all records were `FF`, it uses the compiled-in key. Otherwise it prepends the uncompressed SEC1 prefix `04` and uses that record as `X || Y`. A signature failure with a selected custom key goes to the failure path. There is no second verification attempt with the compiled key or an older OTP record in the traced callers. The physical OTP programming permissions and exact populated record on this unit are outside the dump. Retail Keystone 3 source labels `0x40009C00` as the update public-key region, and the ForgeBox CLI documents one lifetime registration with on-device confirmation. [Keystone 3 OTP layout](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/src/driver/drv_otp.h) · [ForgeBox CLI registration](https://github.com/KeystoneHQ/forgebox-cli/blob/master/README.md)

The compiled fallback is at `0x0103C108`, with an identical coordinate copy at `0x0103C9F0`. I validated it as a point on secp256k1. Its uncompressed SEC1 encoding follows.

```text
0461e6f835abebf1f807ac46a9474b04e61ed7c0fc082828860e95e7c9b9e7e25fa8495a6eb0088791b851cb0b8d0cd41a1a22cc34055e6be82330a37bd89b5d45
```

Its SHA-256 over those 65 bytes is `c37784067c9df4892d82abfdbd05ca7f8a3c606604909096635f11354c9a5612`. An owner-signed Specter OTA validated with its registered public key, which differed from the fallback. I found no evidence tying the fallback to a particular retail firmware release. The two literal copies represent one key. The key lookup is in OTP, so editing either application flash or the external SPI data chip does not change the selected key.

## OTA installation and boot-time enforcement

`0x01012BF4` is the OTA installer. It parses the `~fwdata!` container, checks file size and the SHA-256 of the compressed payload, checks the package signature at `0x01012F54`, and runs the decompressor in a verification pass before a write pass. The verifier compares the SHA-256 of the decompressed application with `originalHash` and verifies `originalSignature` at `0x010123DC`. The write pass programs the app at `0x01081000` and saves the application’s signature record in QSPI at `0x01FFF000`. The normal path rejects unsigned OTA packages. The startup code considers both `pillar.bin` and `forgebox.bin` and calls the same installer for both. The microSD staging code uses drive `0:` as source and drive `1:` as the device-side staging volume. The installer can delete the staged package there. I found version-related UI and staging logic, including a `0.0.1` setup-image test. I did not establish a monotonic application rollback counter or an enforced minimum across all installation paths. [ForgeBox OTA format and signing implementation](https://github.com/KeystoneHQ/forgebox-cli)

`0x010132E0` is the installed-application verification screen. It derives a hash by scanning the application’s sector layout, loads the selected key and the record at `0x01FFF000`, and checks the signature. Success shows the user-firmware notice and offers a five-second countdown. Failure from a blank record, failed signature or other checks leads to a separate verification-failure screen. That screen’s **Enter System** button points to the same callback as the success screen, Thumb pointer `0x01011E01`. The UI dispatches that callback. Its body checks only whether the first app vector word is `0xFFFFFFFF`. Otherwise it disables interrupts, loads MSP from `0x01081000`, and branches through the reset vector at `0x01081004`. It performs no signature check there. Thus an already-programmed image with a usable vector table can reach execution after failed verification if the user takes that path. This does not establish a way to install unsigned code through the OTA updater or to write raw application flash on a locked unit from outside.

There is also an earlier path in the startup routine at `0x0101F7D8`. It configures and samples GPIOA bit 2 and, when low, directly calls the same handoff at `0x0101F810`, before OTA or application verification. In the retail Keystone 3 source, PA2 is the tamper input and low indicates tamper. The ForgeBox branch establishes the control flow and pin selection. I did not trip the line or test what the board’s electrical tamper circuit does to other state, so the practical result on a physical attack remains unverified. This is a reason to describe the production mask-ROM check and the application boot policy separately. [Keystone 3 tamper driver](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/src/driver/drv_tamper.c)

The application hashing routine at `0x01011C98` identifies an end-marker sector by a big-endian leading 16-bit value below 4096 and `FF` in the remaining 4,094 bytes. It hashes full sectors from `0x01081000` through that sector inclusive. A reference Specter build reproduced the same digest as its OTA original hash when hashed using this sector rule.

The QSPI programming path at `0x0100704C` has an optional encryption layer. The branch at `0x010070CA` reads a 16-byte OTP key and a 16-byte IV at `0x40009128` and `0x40009138`. If both are all `FF`, the driver selects plain programming. Otherwise it selects encryption. The retail SDK contains the matching logic. This establishes how the writer chooses its mode. The export by itself cannot establish the bytes a physical QSPI reader would see or which mode covers each range on this unit. [MH1903 QSPI driver](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/external/mh1903_lib/MHSCPU_Driver/src/mhscpu_qspi.c)

### Reconstructed control flow

Names are descriptive. The following omits display setup, update error handling, and other unrelated initialization.

```text
select_application_key():                       # 0x01012020
    for address from 0x40009FC0 down to 0x40009C00 by 0x40:
        record = read_otp(address, 64)
        if all_bytes_are_FF(record):
            continue
        if any_byte_is_neither_00_nor_FF(record):
            return SEC1_uncompressed(record)
        break
    return compiled_default_key

application_handoff():                          # 0x01011E00
    stack = read_word(0x01081000)
    if stack == 0xFFFFFFFF:
        return
    reset = read_word(0x01081004)
    disable_interrupts()
    set_main_stack_pointer(stack)
    branch_to(reset)

boot_application():
    if GPIOA_bit_2_is_low():                     # 0x0101F804
        application_handoff()
        wait_forever()
    process_pending_updates()
    if application_is_present():
        if installed_signature_verifies():      # 0x0101335E
            show_notice_with_countdown_and_enter_system()
        else:
            show_failure_with_wipe_and_enter_system()
        # Both Enter System buttons invoke application_handoff().
```

## Recovery Wipe Device

The recovery confirmation and verification-failure screen lead to the wipe worker at `0x01014220`. On the failure screen the button binds directly to the worker. The table follows the execution order.

| Area | Observed operation |
| --- | --- |
| DS28S60 | Sends authenticated writes of 32 zero bytes to pages `0–81`, then page `91`. The helper at `0x01009880` reads the old page and issues opcode `0xB4`. The loop discards its return code. |
| DS28S60 pages `82–90` | Skipped. Page `82` is the documented fingerprint communication key. No fingerprint-module template-delete call is visible in the wipe worker. |
| QSPI | Requests 3,967 erases in 4 KiB steps across `0x01081000–0x01FFFFFF`. This includes the installed application and its signature sector and starts immediately after the bootloader. The wrapper calls the ROM flash API via `0x0000801C`. |
| External SPI NOR | Issues chip erase `0xC7` and polls the write-in-progress status bit. This is the whole chip, including the device-side staging filesystem and Specter’s SPI storage, not just the erased space reported by the scanner. |
| Boot parameters and reset | If the factory marker is present, attempts to recreate a 16-byte default boot parameter record at external SPI offset `0x00F6D800`, using OTP-derived AES-128-CBC and checking readback. Then requests a Cortex-M system reset. |

I saw no OTP write, ATECC erase, or microSD erase in this worker. That explains why registered firmware trust should survive a bootloader wipe. A notable finding is that **page `91` is the DS28S60 binding page** in both the retail driver and the bootloader’s key-derivation helper at `0x010092D4`. The loop really submits a zero write to page `91`. Whether the write succeeds on this unit and what follows were not tested. The completion screen is insufficient to prove which secure-element writes succeeded. In the current Specter port, the fingerprint credential is on page `72` and its metadata is on page `84`; this loop targets the former and skips the latter. Treat the recovery wipe as firmware removal plus broad storage erasure, with possible effects on secure-element and biometric state. The Specter in-app wipe is a different routine. [ForgeBox page 82 warning](https://github.com/KeystoneHQ/forgebox) · [Keystone DS28S60 driver](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/src/driver/drv_ds28s60.c) · [DS28S60 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds28s60.pdf)

## Factory marker, registration, and genuine check

The bootloader’s `factory pass` function at `0x01012548` checks whether the OTP word at `0x40009400` differs from `0xFFFFFFFF`. This is a manufacturing marker. Its callers use it when loading or restoring boot parameters. The ForgeBox CLI’s registration flow proves possession of the user’s signing private key. The newer CLI mode signs the public key concatenated with a device nonce. The older mode signs the public key alone. Both modes serve user-key registration. I did not locate a retail-style Web Authentication handler in this boot image or identify the ForgeBox setup application’s provisioning write handler from this file. A stock ForgeBox setup application or a capture of the exact initial genuine-check interaction would let us trace that path independently. [Keystone factory provisioning source](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/src/presetting.c) · [ForgeBox CLI source](https://github.com/KeystoneHQ/forgebox-cli)

Retail Keystone 3’s published onboarding code implements an independent Web Authentication flow. The device scans a QR challenge, verifies its ciphertext against a dedicated compiled-in secp256k1 service key, and uses an OTP-stored RSA private key to decrypt it with OAEP-SHA1. The plaintext verification code is displayed and entered on Keystone’s site. The C caller retrieves two 512-byte components from the Web Authentication OTP region at `0x40009800`. The Rust code passes them as the RSA modulus and private exponent and uses public exponent 65537. I did not find that service public key in the ForgeBox boot export in raw or ASCII hex form. The on-device retail routine shown here validates the service challenge and decrypts it. It does not compute a measured-boot hash for the response. The server-side issuance and validation rules remain outside this analysis. This flow is distinct from the factory marker, the earlier secure-element pairing challenge, the retail ATECC device-key signing API, and the ForgeBox OTA signing key. [Keystone device verification page](https://keyst.one/authentication) · [Retail C caller](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/src/ui/gui_model/gui_model.c) · [Retail Rust implementation](https://github.com/KeystoneHQ/keystone3-firmware/blob/master/rust/rust_c/src/common/web_auth.rs)
