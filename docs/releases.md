# Release Model

The monorepo uses tag prefixes to build and stage the right assets:

- `firmware-vX.Y.Z`: builds firmware and uploads `.bin`, `.hex`, `.elf`, `.map`, the versioned final carrier (`kbhe-app.bin` before 2.0.10, `kbhe-app-updater-v3.bin` at/after 2.0.10) with its detached signature, and a bootable signed `kbhe-factory.bin` image.
- `app-vX.Y.Z`: builds the Tauri configurator installer and uploads a matching `<installer>.sig` authentication asset.

The desktop configurator checks GitHub Releases for both app updates and
firmware updates. Firmware updates are downloaded to a temporary file and then
flashed through the RAW HID updater path. The configurator verifies every
download with the embedded Ed25519 release public key. The bootloader performs
the same verification over the firmware manifest before making an image
bootable.

## One-time release signing setup

App and firmware releases deliberately use different Ed25519 keys. A compromise
of the JavaScript/Windows build boundary must not grant the ability to sign a
firmware image accepted by the bootloader.

- `KBHE_FIRMWARE_RELEASE_SIGNING_KEY_B64` matches
  [`firmware/keys/firmware-ed25519-public.pem`](../firmware/keys/firmware-ed25519-public.pem).
- `KBHE_APP_RELEASE_SIGNING_KEY_B64` matches
  [`apps/configurator/keys/app-ed25519-public.pem`](../apps/configurator/keys/app-ed25519-public.pem).

Generate the two private keys independently on a trusted machine. Keep at least
two encrypted offline backups of the firmware key: the bootloader trust root
cannot be replaced by a normal USB application update.

```powershell
openssl genpkey -algorithm ED25519 -out C:\secure\firmware-ed25519-private.pem
openssl pkey -in C:\secure\firmware-ed25519-private.pem `
  -pubout -out firmware\keys\firmware-ed25519-public.pem
openssl genpkey -algorithm ED25519 -out C:\secure\app-ed25519-private.pem
openssl pkey -in C:\secure\app-ed25519-private.pem `
  -pubout -out apps\configurator\keys\app-ed25519-public.pem
```

The private PEM files are ignored by the repository and must never be copied
under a different unignored name. Encode and upload them through stdin so their
contents do not appear in a command line or terminal history:

```powershell
[Convert]::ToBase64String(
  [IO.File]::ReadAllBytes("C:\secure\firmware-ed25519-private.pem")
) | gh secret set KBHE_FIRMWARE_RELEASE_SIGNING_KEY_B64 `
  --env firmware-release --repo Fefe-Nayz/kbhe-monorepo

[Convert]::ToBase64String(
  [IO.File]::ReadAllBytes("C:\secure\app-ed25519-private.pem")
) | gh secret set KBHE_APP_RELEASE_SIGNING_KEY_B64 `
  --env app-publish --repo Fefe-Nayz/kbhe-monorepo
```

### Protected GitHub environments

The release boundary is split across three GitHub Actions environments:

| Environment | Allowed refs | Secrets | Environment variables |
|---|---|---|---|
| `firmware-release` | `firmware-v*`, `main` for manual preflight | `KBHE_FIRMWARE_RELEASE_SIGNING_KEY_B64` | none |
| `app-codesign` | `app-v*`, `main` for manual preflight | `KBHE_WINDOWS_CODESIGN_PFX_B64`, `KBHE_WINDOWS_CODESIGN_PASSWORD` | `KBHE_WINDOWS_CODESIGN_MODE`, `KBHE_WINDOWS_CODESIGN_CERT_THUMBPRINT` |
| `app-publish` | `app-v*`, `main` for manual preflight | `KBHE_APP_RELEASE_SIGNING_KEY_B64` | the same mode and thumbprint values |

Configure required reviewers, restrict each environment to the listed refs, and
disable administrator bypass. A protected job does not receive its environment
secrets before a reviewer approves it. The `main` exception exists only for the
manually dispatched signing preflight; normal pull-request and `main` CI jobs do
not target these environments and never receive release secrets.

The app workflow intentionally runs on two different Windows runners. The
`app-codesign` runner is the only runner that receives the PFX; it builds and
Authenticode-signs Tauri, verifies every release installer and uploads a one-day
private Actions artifact. Tauri signs each bundle-specific application binary
before packaging it, then restores the unsigned raw build output; that restored
`target/release` executable is not a release asset and is deliberately excluded
from post-build verification. A fresh `app-publish` runner does **not** run Bun,
npm, frontend code, or the Tauri build. It re-verifies the transferred hashes,
certificate, signer and any timestamp required by the selected mode, then
receives the app Ed25519 key, creates a draft GitHub Release, re-downloads every
asset, verifies it again, and only then makes the draft public. The firmware key
is never available to either app job.

### Windows Authenticode bootstrap

`app-codesign` also requires a code-signing PFX. The public half is committed as
[`kbhe-community-authenticode.pem`](../apps/configurator/keys/kbhe-community-authenticode.pem).
The workflow requires the PFX certificate, the committed PEM and the protected
thumbprint variable to be byte-for-byte/thumbprint consistent. It also requires
the code-signing EKU, current validity, SHA-256 signing, Windows Authenticode
validation, and the exact expected signer. `public-ca` additionally requires an
RFC3161 timestamp; `self-signed-bootstrap` deliberately signs without an
external timestamp and relies on the detached KBHE Ed25519 signature for update
authentication.

The current `self-signed-bootstrap` mode is free and keeps the pipeline fail
closed, but it is **not a publicly trusted publisher identity**. Microsoft
documents that a self-signed certificate has the same SmartScreen warning
behavior as an unsigned download. Users can therefore still see “Unknown
publisher”; the detached KBHE Ed25519 signature is the actual application
update trust layer. Never describe the bootstrap certificate as eliminating
SmartScreen warnings. Switch `KBHE_WINDOWS_CODESIGN_MODE` to `public-ca` only
after replacing the PFX, committed certificate and thumbprint with a real
publicly trusted signing identity. See [Microsoft's SmartScreen guidance](https://learn.microsoft.com/windows/apps/package-and-deploy/smartscreen-reputation).

### Mandatory signing preflight

Before any tag is created, dispatch **Release Signing Preflight** with target
`app`, `firmware`, or `both`. It derives each Ed25519 public key from its
protected private key and compares it with the committed PEM, performs a real
sign/verify round trip, and signs/verifies a disposable Windows executable with
the protected PFX. It also exercises and requires RFC3161 timestamping in
`public-ca` mode. The workflow writes no release and has only `contents: read`
permission.

## CI vs CD Behavior

The workflows split validation, code signing and publication:

- **Push to `main` / pull request**: CI runs (frontend tests/lint/build, `cargo check --locked` and `cargo test --locked` for the configurator; all native tests plus both ARM builds for firmware). **No release is created.**
- **Protected signing preflight**: validates all selected key pairs and the
  mode-appropriate Authenticode path without publishing anything.
- **Push of an `app-v*` tag**: CI runs **and** the protected release jobs build,
  authenticate and publish the installer artifacts.
- **Push of a `firmware-v*` tag**: CI builds, signs, uploads and remotely
  verifies an authenticated **draft**. Publication remains a manual action
  after recovery-equipped updater-migration HIL.

This means a green CI run on `main` does **not** mean a release exists. An app
release appears after its tag succeeds; a firmware tag initially produces only
a verified draft.

## Canonical release automation

Run the release helper from a clean, up-to-date `main`. Preparation and
publication are separate on purpose:

```powershell
# Bump, refresh locks, run every documented test/build, whitelist-stage,
# commit and push main, then wait for the matching main CI run.
.\tools\release\run-release.ps1 -Phase prepare -Target both -BumpPart patch

# Re-check exact origin/main, wait for main CI, dispatch/wait for protected
# signing preflight, then atomically push the annotated app+firmware tags.
.\tools\release\run-release.ps1 -Phase publish -Target both -BumpPart patch
```

On Windows, `-UseWsl` keeps every build and test in Linux while Git and GitHub
authentication remain on the host. This avoids spawning native compiler
consoles and also makes the local toolchain match CI more closely:

```powershell
# Optional when Bun/Cargo are not on WSL's non-interactive PATH.
$env:KBHE_WSL_BUN = "/home/<wsl-user>/.bun/bin/bun"
$env:KBHE_WSL_CARGO = "/home/<wsl-user>/.cargo/bin/cargo"
$env:KBHE_WSL_RUSTUP_HOME = "/home/<wsl-user>/.rustup"
$env:KBHE_WSL_CARGO_HOME = "/home/<wsl-user>/.cargo"
# Keep Cargo artifacts on the Linux filesystem if /mnt/c produces I/O errors.
$env:KBHE_WSL_CARGO_TARGET_DIR = "/tmp/kbhe-tauri-target"

.\tools\release\run-release.ps1 -Phase prepare -Target both -BumpPart patch `
  -UseWsl -WslDistribution Ubuntu
.\tools\release\run-release.ps1 -Phase publish -Target both -BumpPart patch `
  -UseWsl -WslDistribution Ubuntu
```

With this option, Bun, Cargo, CMake, CTest, Ninja, Python and both firmware
compilers run inside the selected WSL distribution. The same validation and
release gates remain mandatory; this is an execution backend, not a skip flag.

`-Phase all` performs both phases in one invocation but still stops for the CI
and environment approval gates. `-Yes` skips local confirmations; it cannot and
must not bypass GitHub environment reviewers. The helper refuses a dirty tree,
a branch other than `main`, a local/remote mismatch, unexpected generated
changes, an existing tag/release, red CI, or a failed signing preflight. It
stages only the documented version and lock files and uses an atomic multi-tag
push for a combined release.

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
   bun test
   bun run lint
   bun run build
   cd src-tauri
   cargo check --locked
   cargo test --locked
   ```

   All commands must succeed. These are the exact frontend/backend validations
   performed by CI with Bun 1.4.0 and Rust 1.88.0.

5. **Stage and commit every changed file**:

   ```powershell
   git add apps/configurator/package.json `
           apps/configurator/bun.lock `
           apps/configurator/src-tauri/Cargo.toml `
           apps/configurator/src-tauri/Cargo.lock `
           apps/configurator/src-tauri/tauri.conf.json
   git commit -m "Bump configurator to X.Y.Z"
   git push
   ```

6. **Wait for the `Configurator CI` workflow on `main` to go green** before
   tagging. If main is red, the tag run will publish a broken installer.

7. **Run the protected `Release Signing Preflight` workflow** for target
   `app` and wait for the `app-codesign` and `app-publish` reviewers/jobs. Do
   not create a tag if either key pair, certificate, required timestamp or
   verification check fails.

8. **Tag and push** (this is what triggers the release build):

   ```powershell
   git tag app-vX.Y.Z
   git push origin app-vX.Y.Z
   ```

9. **Watch both protected tag jobs** in GitHub Actions. The first runner builds
   and signs Tauri with only the Authenticode PFX, adding an RFC3161 timestamp in
   `public-ca` mode, then transfers a hash manifest, the public certificate and
   installers as a short-lived private Actions artifact. A fresh runner
   re-verifies Authenticode and exact hashes,
   then obtains only the app Ed25519 key. It creates detached `.sig` siblings in
   a draft release, downloads the exact remote assets, compares SHA-256 digests,
   re-verifies Authenticode and Ed25519, and finally publishes. Any missing,
   duplicate, untrusted, unexpectedly untimestamped or unexpected asset blocks
   publication.

   Before the private transfer, installer filenames are canonicalized to the
   GitHub-safe `[A-Za-z0-9._-]` set (for example, `KBHE configurator_...` becomes
   `KBHE.configurator_...`). GitHub itself replaces spaces in uploaded asset
   names with dots; canonicalizing first ensures the transfer manifest, the
   Ed25519 role and the downloaded public asset all bind the exact same name.

Installer signatures use the `KBHEAPP2` domain and bind the normalized app
version, OS, CPU architecture, exact asset filename, byte length and SHA-512
digest. The download command also rejects a requested version less than or
equal to the currently running configurator, preventing signed replay or
rollback through a relabelled GitHub release.

### Re-running the installer build

The publish jobs are gated on `if: startsWith(github.ref, 'refs/tags/app-v')`
and refuse to replace a release for the same tag. To rebuild after a failure,
**bump the patch version and tag again**
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
   [`firmware/Core/Inc/firmware_version.h`](../firmware/Core/Inc/firmware_version.h) so they
   match the tag you're about to push:

   ```c
   #define FIRMWARE_VERSION_MAJOR 2u
   #define FIRMWARE_VERSION_MINOR 0u
   #define FIRMWARE_VERSION_PATCH 1u
   ```

   Commit the change to `main` before tagging.

   If any resident bootloader source or behavior changed, also bump the
   independent `UPDATER_BOOTLOADER_VERSION_*` constants in
   `firmware/Bootloader/Inc/updater_bootloader_version.h`. Reusing a KBLV for a
   different bootloader binary would make already-current devices skip the new
   payload. Never decrease KBLV. The tag workflow downloads and authenticates
   the previous published `kbhe-bootloader.bin`, then rejects a decreasing KBLV
   or different binary with the same KBLV before release signing.

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

4. **Run the protected `Release Signing Preflight` workflow** for target
   `firmware`, approve the `firmware-release` environment job, and require a
   successful private/public key derivation plus signing round trip.

   For a release that changes the resident bootloader, publish stable
   configurator `app-v0.1.18` first. Firmware tag CI checks this minimum and
   writes it into `firmware-manifest.json`. Deployed v3 updaters accept the
   migrator only under a strictly newer firmware release version; if
   application `X` was already installed by an older configurator without
   refresh, issue `X+1` rather than trying to weaken anti-rollback or reuse
   `X`. Older configurators do not understand the exact schema-2
   refresh/capsule roles.

   For the current `firmware-v2.0.9` source baseline, the minimum intended order
   is therefore `app-v0.1.18` first, then `firmware-v2.0.10`. The carrier must
   also be strictly newer than every application version that may already be on
   affected keyboards: if 2.0.10 reached even one device through an older
   configurator, this rollout must use `firmware-v2.0.11` (or higher), never a
   rebuilt or re-signed 2.0.10.

5. **Tag and push** (use semver `X.Y.Z` matching the source constants from
   step 1):

   ```powershell
   git tag firmware-vX.Y.Z
   git push origin firmware-vX.Y.Z
   ```

6. CI creates and remotely verifies a **draft** release at
   `https://github.com/<owner>/<repo>/releases/tag/firmware-vX.Y.Z` with
   the exact versioned final carrier plus its signature (at/after 2.0.10,
   `kbhe-app-updater-v3.bin` and `.sig`), the complete
   `kbhe-updater-v2-to-v3.bin` capsule, and the distinct inner
   `kbhe-updater-v3-refresh.bin` image with their signature siblings. The
   firmware signatures bind the exact inner image length, CRC32, SHA-512 digest
   and firmware version from the tag; the v2 package embeds that exact signed
   image and signature in its canonical layout. Every other
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

Firmware publication has an additional destructive-HIL boundary: every remote
asset is downloaded and SHA-256-compared with the locally authenticated
artifact, then CI intentionally leaves the release as a draft. On a recovery-
equipped keyboard, validate the complete legacy `v2 -> signed migrator -> v3 ->
final application` path and the `old v3 -> signed refresh -> current v3 -> final
application` path, both option-byte changes, the current-version skip, repeated
resets and the documented recovery path. The exact authenticated-draft download, local
signature inspection, native sibling-asset flow and required evidence are in
[Updater compatibility and v2-to-v3 migration](firmware/UPDATER_COMPATIBILITY.md#manual-hil-publication-gate).
Only after recording that evidence may a release operator publish the
already-verified draft:

```powershell
gh release edit firmware-vX.Y.Z --draft=false --repo Fefe-Nayz/kbhe-monorepo
```

Release jobs for one tag are serialized so a concurrent retry cannot replace
assets during draft verification. Never use the publish command merely because
the software-only CI is green.

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
| In-app "Application is up to date" despite a new release | `KBHE_RELEASE_OWNER`/`KBHE_RELEASE_REPO` defaults point at the wrong repo, or the asset extension is not `.exe`/`.msi` on Windows | Check the compiled-in defaults in `releases.rs`, and confirm the authenticated publish job uploaded the expected installer |
| In-app firmware update always shown as available | The keyboard reports a version that doesn't match the published tag (e.g. flashed firmware was built before the constants were bumped) | Re-flash the keyboard with a build whose `FIRMWARE_VERSION_*` constants match the tag you published, or bump source + tag together |
| `UPDATER_MIGRATION_REQUIRED` before flashing | The keyboard reports updater v2 and the selected file is the normal v3 application; flashing it through v2 would invalidate the legacy trailer when sector-6 storage starts | Use only a published, signed `kbhe-updater-v2-to-v3.bin` after its HIL release gate is enabled; until then verify and flash `kbhe-factory.bin` at `0x08000000` with ROM DFU or ST-Link |
| `UPDATER_PROTOCOL_UNSUPPORTED` / `UPDATER_GEOMETRY_UNSUPPORTED` | HELLO reports an unknown protocol or unexpected flash layout | Do not force the transfer. Match the device to a documented migration/factory release and preserve the old application with ABORT/BOOT |
| Firmware signing fails | `KBHE_FIRMWARE_RELEASE_SIGNING_KEY_B64` is absent, malformed, or does not match the committed firmware public key | Correct the `firmware-release` environment secret and rerun signing preflight; never weaken the verifier |
| App detached signing fails | `KBHE_APP_RELEASE_SIGNING_KEY_B64` is absent, malformed, or does not match the committed app public key | Correct the `app-publish` environment secret and rerun signing preflight |
| Authenticode signing or verification fails | PFX/password, mode, expected thumbprint and committed public certificate disagree; the certificate lacks code-signing EKU/validity; or required `public-ca` RFC3161 timestamping failed | Correct `app-codesign` and mirror its mode/thumbprint variables in `app-publish`, then require a green signing preflight before creating another version tag |
| A release is not offered by the configurator | The expected binary/installer has no exact sibling `<asset>.sig` or the signature metadata is not 64 bytes | Re-run a correctly configured tag release and confirm that both assets were uploaded |
