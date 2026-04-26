# Release Model

The monorepo uses tag prefixes to publish the right assets:

- `firmware-vX.Y.Z`: builds firmware and uploads `.bin`, `.hex`, `.elf`, and `.map` assets.
- `app-vX.Y.Z`: builds the Tauri configurator installer.

The desktop configurator checks GitHub Releases for both app updates and
firmware updates. Firmware updates are downloaded to a temporary file and then
flashed through the existing RAW HID updater path.

## Publish Firmware

```powershell
git tag firmware-v0.1.0
git push origin firmware-v0.1.0
```

The primary binary consumed by the configurator is `kbhe-app.bin`.

## Publish Configurator

```powershell
git tag app-v0.1.0
git push origin app-v0.1.0
```

The Windows installer asset is selected by the configurator from the release
assets, preferring `.exe` and falling back to `.msi`.

## Repository Target

Release checks default to `Fefe-Nayz/kbhe-monorepo` for local builds. In GitHub
Actions the target repository is compiled from the workflow environment:

- `KBHE_RELEASE_OWNER=${{ github.repository_owner }}`
- `KBHE_RELEASE_REPO=${{ github.event.repository.name }}`
