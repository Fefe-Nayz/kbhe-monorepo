# Release Model

The monorepo uses tag prefixes to publish the right assets:

- `firmware-vX.Y.Z`: builds firmware and uploads `.bin`, `.hex`, `.elf`, and `.map` assets.
- `app-vX.Y.Z`: builds the Tauri configurator installer.

The desktop configurator checks GitHub Releases for both app updates and
firmware updates. Firmware updates are downloaded to a temporary file and then
flashed through the existing RAW HID updater path.

## CI vs CD Behavior

Both workflows split validation from publication:

- **Push to `main` / pull request**: CI runs (lint, frontend build, `cargo check --locked` for the configurator; CMake configure + build for firmware). **No release is created.**
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
   ```

   All four must succeed. `cargo check --locked` is the exact command CI runs.

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

8. **Watch the tag run** in GitHub Actions. The `Build and publish installer`
   step (which is skipped on `main` runs) now executes and publishes the
   release at `https://github.com/<owner>/<repo>/releases/tag/app-vX.Y.Z`.

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
   `kbhe-app.bin` as the primary binary the configurator downloads.

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
| Tag pushed but the release job didn't run | The `paths` filter on `push` may skip tag pushes whose target commit doesn't touch matching files | Make sure the tagged commit modifies a file under `apps/configurator/**` (a version bump does), or trigger manually via `workflow_dispatch` on the tag ref |
| In-app "Application is up to date" despite a new release | `KBHE_RELEASE_OWNER`/`KBHE_RELEASE_REPO` defaults point at the wrong repo, or the asset extension is not `.exe`/`.msi` on Windows | Check the compiled-in defaults in `releases.rs`, and confirm `tauri-action` published the expected installer |
| In-app firmware update always shown as available | The keyboard reports a version that doesn't match the published tag (e.g. flashed firmware was built before the constants were bumped) | Re-flash the keyboard with a build whose `FIRMWARE_VERSION_*` constants match the tag you published, or bump source + tag together |
| Updater protocol mismatch error after flashing | The bootloader on the keyboard is older than the configurator's `UPDATER_PROTOCOL_VERSION` | The bootloader sits in protected flash and is only updated via ST-Link / debugger. Reflash `kbhe_bootloader.bin` at `0x08000000` along with `kbhe.bin` at `0x08010000` |
