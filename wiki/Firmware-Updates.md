# Firmware Updates

Blipscope keeps itself up to date over the air (OTA) — there's nothing to plug in and no files to flash for normal use.

## How it works

Every firmware build carries a version number. Blipscope checks Valar Systems' [GitHub Releases](https://github.com/Valar-Systems/Blipscope/releases) for the latest published version and compares it to what it's running. If the release is newer, it downloads the new firmware image, flashes it, and reboots into it automatically.

Blipscope checks for updates:

- **On every boot**, before it starts showing aircraft, and
- **Once a day** while it's left running, so always-on devices stay current without a restart.

Kits ship with firmware already installed and OTA enabled, so for most people updates simply arrive on their own.

## Manual / developer flashing

If you want to build from source or flash a custom firmware over USB, the firmware lives in the [main repository](https://github.com/Valar-Systems/Blipscope) — see its README for PlatformIO build and upload instructions.

## Related

- **[[Home]]** — feature overview
- [GitHub Releases](https://github.com/Valar-Systems/Blipscope/releases) — published firmware versions
