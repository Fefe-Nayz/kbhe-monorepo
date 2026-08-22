# Release Model

The monorepo uses tag prefixes to publish the right assets:

- `firmware-vX.Y.Z`: builds firmware and uploads `.bin`, `.hex`, `.elf`, `.map`, the detached `kbhe-app.bin.sig`, and a bootable signed `kbhe-factory.bin` image.
- `app-vX.Y.Z`: builds the Tauri configurator installer and uploads a matching `<installer>.sig` authentication asset.

The desktop configurator checks GitHub Releases for both app updates and
firmware updates. Firmware updates are downloaded to a temporary file and then
flashed through the RAW HID updater path. The configurator verifies every
download with the embedded Ed25519 release public key. The bootloader performs
the same verification over the firmware manifest before making an image
bootable.

## One-time release signing setup

Both tag workflows require a repository Actions secret named
`KBHE_RELEASE_SIGNING_KEY_B64`. Its value is the base64 encoding of the Ed25519
private PEM matching [`firmware/keys/firmware-ed25519-public.pem`](../firmware/keys/firmware-ed25519-public.pem).
The private key must stay outside the repository and must never be committed.

For example, an administrator can encode an existing private PEM locally with:

```powershell
[Convert]::ToBase64String(
  [System.IO.File]::ReadAllBytes("C:\secure\firmware-ed25519.pem")
)
```

Store the resulting single line as the repository secret. Tag release jobs
fail closed when the secret is absent, malformed, or does not match the
committed public key. Normal pull-request and `main` builds do not receive or
require the secret.

Windows app releases additionally require
`KBHE_WINDOWS_CODESIGN_PFX_B64` and `KBHE_WINDOWS_CODESIGN_PASSWORD` in the
protected `release` environment. The workflow Authenticode-signs and RFC3161
timestamps every EXE/MSI, verifies it with Windows policy, replaces the draft
asset, and only then creates the detached Ed25519 signature. Authenticode is
the OS/SmartScreen trust layer; the Ed25519 manifest independently binds the
GitHub release version, platform, architecture, filename and bytes.

Create a GitHub Actions environment named `release`, move the secret to that
environment, and configure required reviewers plus protected `app-v*` and
`firmware-v*` tag rules. Both release jobs target this environment and reject
tags whose commit is not reachable from `main`. The decoded key is removed from
the step environment immediately after the temporary PEM is created. For
production-scale signing, replace the long-lived PEM secret with a KMS/HSM
operation reached through short-lived OIDC credentials; workflow protections
reduce exposure but do not turn a repository secret into hardware-backed key
custody.

## CI vs CD Behavior

Both workflows split validation from publication:

- **Push to `main` / pull request**: CI runs (frontend tests/lint/build, `cargo check --locked` and `cargo test --locked` for the configurator; all native tests plus both ARM builds for firmware). **No release is created.**
- **Push of a `app-v*` or `firmware-v*` tag**: CI runs **and** the release job builds the artifacts and publishes them to GitHub Releases.

This means a green CI run on `main` does **not** mean a release exists — the
release only appears after the matching tag is pushed.

## Publishing the Configurator (app)

The Tauri installer is built only when a tag matching `app-v*` is pushed.
Because `cargo check --locked` runs in CI, **`Cargo.lock` must always be in
sync with `Cargo.toml` before push** — out-of-sync lockfiles fail the
`Check Tauri backend` step.

### Pre-push checklist

Run from the repo root.

1. **Bump the version in three files** (they must all match):
   - [`apps/configurator/package.json`](../apps/configurator/package.json) → `"version"`
   - [`apps/configurator/src-tauri/Cargo.toml`](../apps/configurator/src-tauri/Cargo.toml) → `version`
   - [`apps/configurator/src-tauri/tauri.conf.json`](../apps/configurator/src-tauri/tauri.conf.json) → `"version"`

2. **Refresh the JS lockfile** (picks up the new package.json version):

   ```powershell
   cd apps/configurator
   bun install
   ```

3. **Refresh the Rust lockfile** (this is the step the CI failure caught):

   ```powershell
   cd apps/configurator/src-tauri
   cargo check
   ```

   Running plain `cargo check` (without `--locked`) regenerates `Cargo.lock`
   in place. If you skip this, CI will fail with:
   `error: the lock file ... needs to be updated but --locked was passed`.

4. **Reproduce the CI checks locally** (catches problems before pushing):

   ```powershell
   cd apps/configurator
   bun run lint
   bun run build
   cd src-tauri
   cargo check --locked
   cargo test --locked
   ```

   All commands must succeed. The two locked Cargo commands are the exact
   backend validation performed by CI with Rust 1.88.0.

5. **Stage and commit every changed file**:

   ```powershell
   git add apps/configurator/package.json `
           apps/configurator/bun.lockb `
           apps/configurator/src-tauri/Cargo.toml `
           apps/configurator/src-tauri/Cargo.lock `
           apps/configurator/src-tauri/tauri.conf.json
   git commit -m "Bump configurator to X.Y.Z"
   git push
   ```

6. **Wait for the `Configurator CI` workflow on `main` to go green** before
   tagging. If main is red, the tag run will publish a broken installer.

7. **Tag and push** (this is what triggers the release build):

   ```powershell
   git tag app-vX.Y.Z
   git push origin app-vX.Y.Z
   ```

8. **Watch the tag run** in GitHub Actions. It publishes each installer and a
   matching `.sig`. The configurator ignores unsigned releases and verifies the
   signature again immediately before launching the installer. Publication is
   atomic from the user's perspective: Tauri first uploads to a draft release,
   signatures are generated and uploaded, then CI downloads the exact remote
   assets, compares their SHA-256 digests and re-verifies Ed25519 over the
   downloaded installers. Any missing, duplicate or unexpected draft asset
   blocks publication.

Installer signatures use the `KBHEAPP2` domain and bind the normalized app
version, OS, CPU architecture, exact asset filename, byte length and SHA-512
digest. The download command also rejects a requested version less than or
equal to the currently running configurator, preventing signed replay or
rollback through a relabelled GitHub release.

### Re-running the installer build

The publish step is gated on `if: startsWith(github.ref, 'refs/tags/app-v')`,
and `tauri-action` rejects publishing if a release for the same tag already
exists. To rebuild after a failure, **bump the patch version and tag again**
(`0.1.1` → `0.1.2`) — this is the normal release flow and avoids dangling
releases.

If you really need to retry the same version (rare), delete the GitHub
Release and the tag first, then re-tag and push. Force-pushing a tag without
deleting the release will not replace the assets.

### Local sanity check before tagging

You can dry-run the full installer build locally:

```powershell
cd apps/configurator
bun tauri build
```

Output lands in [`apps/configurator/src-tauri/target/release/bundle/`](../apps/configurator/src-tauri/target/release/bundle/)
(`msi/`, `nsis/`). If this works, the CI build will work too.

## Publishing Firmware

The firmware build is triggered by a `firmware-v*` tag. The firmware version
embedded in the binary comes from the firmware sources (`firmware/`), **not
from the tag** — the tag only labels the GitHub Release. The configurator
compares the tag's semver against the version reported by the running firmware
to decide if an update is available, so **the tag and the source constants
must match** (e.g. `firmware-v2.0.1` ↔ `MAJOR=2 MINOR=0 PATCH=1`).

### Pre-push checklist

1. **Bump firmware version constants** in
   [`firmware/Core/Src/settings.c`](../firmware/Core/Src/settings.c) so they
   match the tag you're about to push:

   ```c
   #define FIRMWARE_VERSION_MAJOR 2u
   #define FIRMWARE_VERSION_MINOR 0u
   #define FIRMWARE_VERSION_PATCH 1u
   ```

   Commit the change to `main` before tagging.

2. **Reproduce the CI build locally**:

   ```powershell
   cmake --preset Release
   cmake --build --preset Release
   cmake --preset Release-apponly
   cmake --build --preset Release-apponly
   ```

   All four must succeed. The CI runs the same commands and copies
   `kbhe-app.bin`, `kbhe-bootloader.bin`, `kbhe-app-only.bin` and their
   `.hex/.elf/.map` siblings into the release.

3. **Wait for the `Firmware CI` workflow on `main` to go green** before
   tagging.

4. **Tag and push** (use semver `X.Y.Z` matching the source constants from
   step 1):

   ```powershell
   git tag firmware-vX.Y.Z
   git push origin firmware-vX.Y.Z
   ```

5. The release appears at
   `https://github.com/<owner>/<repo>/releases/tag/firmware-vX.Y.Z` with
   `kbhe-app.bin` plus `kbhe-app.bin.sig`. The signature binds the exact image
   length, CRC32, SHA-512 digest and firmware version from the tag. Every other
   factory/debug artifact (`kbhe-factory.bin`, bootloader, app-only, HEX, ELF,
   MAP and manifest) also receives a role-bound `.sig` sibling. The signing
   step parses the embedded `KFWV` record and fails if it differs from the tag.

Before using a factory artifact with ST-Link, verify it locally. The
`KBHEART2` signature binds the canonical firmware version, exact hardware
target, exact release filename, length and SHA-512 digest, so every argument
must match the selected release:

```powershell
python tools/release/sign_release_asset.py artifact `
  --input kbhe-bootloader.bin `
  --signature kbhe-bootloader.bin.sig `
  --version X.Y.Z `
  --target kbhe-75he-stm32f723vet6 `
  --role kbhe-bootloader.bin `
  --verify-public firmware/keys/firmware-ed25519-public.pem
```

Protocol v3 changes the trailer format and trust policy. For a new board or a
keyboard running the legacy bootloader, verify `kbhe-factory.bin` with its
role-bound sibling signature, then program that single 256-KiB image at
`0x08000000` with ST-Link/STM32CubeProgrammer. It contains the bootloader at
`0x08000000..0x0800BFFF`, an append-only anti-rollback journal seeded with the
factory version in sector 3, the application at `0x08010000`, and the valid signed trailer at
`0x0803FF00`; no optional second USB flash is needed. Flashing only the raw
bootloader plus the raw app would omit the trailer and v3 would correctly stay
in updater mode.

After migration, USB updates are authenticated and their durable version floor
is committed before flash erase; the stored signature and the version floor are
revalidated before every application jump. A power cut between the floor
commit and slot erase therefore fails closed into updater mode. The current
project does not
provision STM32 WRP/PCROP/RDP option bytes: the bootloader is separated from
the USB updater erase range but is not protected against an attached hardware
debugger. Physical production locking, if desired, must be a separate and
carefully validated manufacturing step.

Firmware publication uses the same draft boundary: every remote asset is
downloaded and SHA-256-compared with the locally authenticated artifact before
the draft can become public. Release jobs for one tag are serialized so a
concurrent retry cannot replace assets between verification and publication.

### Re-running the firmware build

Same rule as the configurator: bump to a new patch version and re-tag rather
than retrying the same tag. The CI uses `softprops/action-gh-release` which
will refuse to overwrite an existing release for the same tag.

## Repository Target

Release checks default to `Fefe-Nayz/kbhe-monorepo` for local builds (see
[`releases.rs`](../apps/configurator/src-tauri/src/releases.rs)). In GitHub
Actions the target repository is compiled from the workflow environment:

- `KBHE_RELEASE_OWNER=${{ github.repository_owner }}`
- `KBHE_RELEASE_REPO=${{ github.event.repository.name }}`

These environment variables are read by `option_env!()` at compile time, so
they must be set when `cargo build` runs — not at runtime. Locally, an
installer built without them will check the default repository.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `cargo check --locked` fails on CI with "lock file needs to be updated" | `Cargo.toml` was modified without regenerating `Cargo.lock` | Run `cargo check` in `apps/configurator/src-tauri/`, commit the updated `Cargo.lock` |
| Tag pushed but the release job didn't run | The tag does not match the exact `app-v*` / `firmware-v*` prefix or the workflow is disabled | Check the tag spelling and the Actions run list; `workflow_dispatch` can be used for diagnostics but does not bypass release guards |
| In-app "Application is up to date" despite a new release | `KBHE_RELEASE_OWNER`/`KBHE_RELEASE_REPO` defaults point at the wrong repo, or the asset extension is not `.exe`/`.msi` on Windows | Check the compiled-in defaults in `releases.rs`, and confirm `tauri-action` published the expected installer |
| In-app firmware update always shown as available | The keyboard reports a version that doesn't match the published tag (e.g. flashed firmware was built before the constants were bumped) | Re-flash the keyboard with a build whose `FIRMWARE_VERSION_*` constants match the tag you published, or bump source + tag together |
| Updater protocol mismatch error after flashing | The bootloader on the keyboard is older than the configurator's `UPDATER_PROTOCOL_VERSION` | Verify and flash the matching `kbhe-factory.bin` at `0x08000000` with ST-Link/STM32CubeProgrammer; this updates bootloader, app and signed trailer together |
| Tag release fails at the signing step | `KBHE_RELEASE_SIGNING_KEY_B64` is absent, malformed, or belongs to another public key | Configure the repository secret from the release private PEM; do not weaken or remove the signature checks |
| A release is not offered by the configurator | The expected binary/installer has no exact sibling `<asset>.sig` or the signature metadata is not 64 bytes | Re-run a correctly configured tag release and confirm that both assets were uploaded |
