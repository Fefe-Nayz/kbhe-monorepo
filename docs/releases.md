# Release Model

The monorepo uses tag prefixes to publish the right assets:

- `firmware-vX.Y.Z`: builds firmware and uploads `.bin`, `.hex`, `.elf`, and `.map` assets.
- `app-vX.Y.Z`: builds the Tauri configurator installer and updater metadata.

The desktop configurator checks GitHub Releases for both app updates and
firmware updates. Firmware updates are downloaded to a temporary file and then
flashed through the existing RAW HID updater path.
