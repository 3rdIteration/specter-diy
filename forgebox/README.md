# Specter-DIY for ForgeBox

This port runs the Specter-DIY v1.10.5 Bitcoin application on Keystone ForgeBox as **v1.10.5-forgebox**. The standard build includes QR scanning, FAT32 microSD, Specter USB serial, device-bound encrypted storage, PIN change, fingerprint unlock, named seed backups, descriptors, persistent settings, and BIP85. The application uses ForgeBox's factory bootloader and owner-signed updates. Smartcard and Liquid workflows are outside this port.

The production profile is the configuration intended for regular use. Hardware results in the [board reference](docs/hardware.md) come from earlier builds on a v3.1 unit. The profile and storage-v1 changes require a first on-device check with a disposable seed. This port has no independent security audit.

## Build

From the root of this branch, with Git, Python 3 and Docker configured for Linux containers:

```powershell
python forgebox/tools/bootstrap.py
docker build -t specter-forgebox-toolchain forgebox
docker run --rm --mount "type=bind,source=$PWD,target=/repo" -w /repo/forgebox specter-forgebox-toolchain
```

On Linux or macOS use `$(pwd)` for the bind-mount source. The build produces `forgebox/dist/mh1903_full.bin` for signing and `forgebox/dist/build.json` with the version, build profile and SHA-256 hashes. The regular Specter-DIY targets remain at the repository root.

## Sign and install

Use the private key corresponding to the public key already registered on your ForgeBox. For a new unit, follow [Keystone's setup and one-time registration workflow](https://github.com/KeystoneHQ/forgebox). In PowerShell from the repository root:

```powershell
forgebox sign --s .\forgebox\dist\mh1903_full.bin --d .\forgebox.bin --key "$env:USERPROFILE\.forgebox\keys\private.pem"
python .\forgebox\tools\verify_ota.py .\forgebox.bin .\forgebox\dist\mh1903_full.bin "$env:USERPROFILE\.forgebox\keys\pubkey.pem"
```

The verification helper uses Python's `cryptography` package. Copy only `forgebox.bin` to the root of a FAT32 microSD card and install from ForgeBox Recovery. A binary signed by one owner works on a ForgeBox registered with that public key. The unsigned build is the shareable release input.

### First boot and reset

This release starts with storage format **v1** and expects blank wallet storage. If you ran a development image, first back up any seeds and wallet descriptors you need, then run **Wipe wallet storage** from the build that owns that data. Device-bound microSD backups depend on its device key, so export the actual recovery phrase and any BIP39 passphrase you need before wiping. Fresh setup opens the PIN flow directly. Unknown or occupied storage stops enrollment without automatically erasing it.

Use Specter's in-app wallet wipe for wallet resets. The factory recovery **Wipe Device** erases the application and the entire external SPI NOR. It also attempts a write to the DS28S60 binding page. See the [bootloader analysis](docs/bootloader.md) for the exact operations.

## Source and maintenance

- [ForgeBox hardware, storage and implementation reference](docs/hardware.md)
- [Bootloader, signing, factory verification and recovery analysis](docs/bootloader.md)
- [Dependency licenses and vendor camera library](THIRD_PARTY_NOTICES.md)

The build copies this checkout's `src/` into a generated application tree and applies the port adaptations there. `forgebox/modules/` provides platform and keystore adapters. `forgebox/port/` contains the MH1903 runtime and drivers. For an upstream update, merge the new SpecterDIY release tag, update the upstream revision and version in `forgebox/dependencies.lock.json`, and rebuild. The runtime follows the checkout's `f469-disco` gitlink. Review the generated-source edits against any upstream changes, then run the host and image checks before hardware validation. Development probes are opt-in through `FORGEBOX_PROFILE=development` and their individual build flags in `tools/build.py`.
