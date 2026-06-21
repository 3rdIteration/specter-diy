# Data Storage on Specter-DIY

This document lists all data persisted by Specter-DIY, where it is stored, and how it is protected.

## Storage Areas

The device uses three storage areas:

- **Internal Flash** (`/flash`) -- keystore secrets, PIN state, saved mnemonics
- **QSPI Flash** (`/qspi`) -- wallet data, host settings, global settings
- **SD Card** (`/sd`) -- optional mnemonic backup, exported files

## The Device Secret

On first boot the device generates a single 32-byte random secret stored at `/flash/keystore/secret`. This is the root of all derived keys:

| Derived Key | Tag | Purpose |
|-------------|-----|---------|
| Anti-phishing key | `tagged_hash("auth", secret)` | Generates words shown during PIN entry |
| Settings key | `tagged_hash("settings key", secret)` | Encrypts global and host settings |
| User key | `tagged_hash("userkey", secret)` | Encrypts Liquid asset labels, user identity |
| UID | `tagged_hash("uid", userkey)[:4]` | 4-character device identifier |

The anti-phishing words are generated per PIN digit using HMAC-SHA256 of the derived key and the BIP39 wordlist. They are **not** stored -- computed fresh each time you type your PIN.

## Internal Flash (`/flash`)

| Data | Path | Format | Encryption |
|------|------|--------|------------|
| Device secret | `/flash/keystore/secret` | 32 raw bytes | None |
| PIN state | `/flash/keystore/pin` | JSON: `{"pin": hex, "pin_attempts_max": N, "pin_attempts_left": N}` | AEAD with device secret |
| Encryption secret | `/flash/keystore/enc_secret` | 32 random bytes | AEAD with PIN-derived key |
| Saved mnemonics | `/flash/keystore/reckless.<name>` | Plaintext mnemonic string | AEAD with encryption secret |
| Network selection | `/flash/network` | Plain text: `main`, `test`, `liquidv1` | None |

The PIN itself is stored as an HMAC of the device secret and your PIN code. The encryption secret (`enc_secret`) is a random key used to encrypt saved mnemonics -- it is itself encrypted with a key derived from both the device secret and your PIN, so changing your PIN only requires re-encrypting this one key.

## QSPI Flash (`/qspi`)

### Wallet Data

Each wallet lives in `/qspi/wallets/<fingerprint_hex>/<network>/<wallet_id>/` with two files:

| File | Content | Encryption |
|------|---------|------------|
| `descriptor` | Full descriptor string, e.g. `wpkh([fp/path]xpub/{0,1}/*)` | AEAD with idkey (`m/0x1D'` from seed) |
| `meta` | JSON: `{"gaps": [20, 20], "name": "My Wallet", "unused_recv": 0}` | AEAD with idkey |

The XPub is embedded inside the descriptor string. Address indexes (`gaps`) track the maximum derived index per branch (receive/change) plus a gap limit of 20. `unused_recv` tracks the next unused receive address.

Wallet names are stored in the `meta` file. When importing a wallet via descriptor, if the input contains `&`, everything before the last `&` is parsed as the name (e.g., `"My Wallet&wpkh(...)"`).

### Liquid Asset Labels (`/qspi/wallets/uid<4_hex>/assets_<network>`)

User-defined labels for Liquid network assets. JSON mapping asset hex IDs to label strings. Encrypted with `userkey`.

### Host Settings (`/qspi/hosts/<ClassName>.settings`)

| Host | Stored Fields |
|------|---------------|
| `USBHost.settings` | `{"enabled": true/false}` |
| `QRHost.settings` | `{"enabled", "aim", "light", "sound", "raw_fix_applied"}` |
| `SDHost.settings` | `{"enabled": true/false}` |

All encrypted with the settings key. The QR scanner marker file `/qspi/hosts/.qr_factory_reset_done` tracks whether initial factory reset was performed.

### Global Settings (`/qspi/global/global.settings`)

JSON: `{"experimental": {"taproot": true/false}}`. Encrypted with the settings key.

## SD Card (Optional)

| File | Content | Encryption |
|------|---------|------------|
| `specterdiy<hex_id>.<name>` | Mnemonic backup | AEAD with encryption secret |
| `<first_word>.txt` | Plaintext mnemonic export | None |
| `<name>.json` | Wallet descriptor + label export | None |
| `bip85-*.txt` | BIP-85 derived mnemonics or keys | None |
| XPUB exports | Various formats (txt/json) | None |

The SD card mnemonic files use a device-specific prefix (`specterdiy<hex_id>`) so only this device can recognize and decrypt them.

## Smartcard (Optional)

When using the smartcard keystore, data is stored on-card encrypted with `tagged_hash("scenc", secret)`:

- **Encryption key** -- 32 bytes for mnemonic encryption
- **Entropy** -- raw seed entropy from the mnemonic

The smartcard also stores a fingerprint (`tagged_hash("scid", secret)[:4]`) so the device can verify the data belongs to it.

## What Is NOT Persisted

- **Entropy pool**: rebuilt fresh each boot from hardware TRNG + touchscreen touches
- **XPubs**: derived on-demand from mnemonic; only descriptor strings containing them are stored
- **Anti-phishing words**: computed on-the-fly from device secret + PIN digit
- **Mnemonic in amnesiac mode**: if you never save your key, it lives only in RAM

## Encryption Details

All encrypted data uses AES-GCM (AEAD). The idkey for wallet encryption is the private key at derivation path `m/0x1D'` from your seed -- this means wallet data is tied to your specific mnemonic and cannot be tampered with undetected.
