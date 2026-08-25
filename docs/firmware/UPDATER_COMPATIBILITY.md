# Updater compatibility, v2 migration and v3 refresh

The configurator negotiates the updater protocol from the common 20-byte
`HELLO` response. It does not infer the updater version from the application
firmware version: the bootloader is persistent and the two versions can differ.

## Compatibility matrix

| Updater | Geometry | Allowed artifact | Transfer |
|---|---|---|---|
| v3 (`0x0003`) | app `0x08010000`, max `0x2FF00`, 4-byte writes | exact final carrier (`kbhe-app.bin` before 2.0.10, `kbhe-app-updater-v3.bin` at/after 2.0.10) or exact signed `kbhe-updater-v3-refresh.bin` inner image | `HELLO -> [BOOTLOADER_INFO] -> AUTH -> BEGIN -> DATA -> FINISH -> BOOT` |
| v2 (`0x0002`) | app `0x08010000`, max `0x4FF00`, 4-byte writes | exact signed `kbhe-updater-v2-to-v3.bin` package only | `HELLO -> BEGIN -> DATA -> FINISH -> BOOT` |
| other | exact matching is required | none | fail before `BEGIN` |

`BOOTLOADER_INFO` is a separate optional command; the 20-byte `HELLO` payload
is unchanged. A new v3 updater reports its independently versioned resident
bootloader and exact hardware target. An older v3 updater returns
`INVALID_COMMAND`, which the native configurator treats as an unknown/old
resident bootloader and refreshes once. If the reported target matches and the
resident version is equal to or newer than the refresh asset, the configurator
skips the refresh and flashes the final application directly.

A destructive bootloader stage also requires a durable schema-3 settings
backup. Schema 1 (calibration) and schema 2 (calibration/profiles) remain valid
restore inputs but do not contain the complete keyboard identity and never open
the destructive gate. Starting already in updater v3 without schema 3 permits
only HELLO and BOOTLOADER_INFO; a required refresh is refused before BEGIN.
**Recover runtime only** first sends BOOT to a validated v3 `APP_VALID` image,
with no download, BEGIN or erase. Only an invalid app downloads the exact
installed-version carrier with all support-asset discovery disabled, then
recovers sectors 4–5 without consuming the newer refresh version.
After runtime reconnect, **Continue updater refresh** captures schema 3 and
retries the newer release. This explicit recovery is v3-only because updater
v2 geometry overlaps a profile bank.

The bootloader version is not the application release version. It is stored in
the `KBLV` record defined by `updater_bootloader_version.h` and is bumped only
when the persistent updater/bootloader changes. This lets later application
releases reuse the same refresh asset contract without rewriting sectors 0–3.
The migrator also scans the resident KBLV before erasing anything: a signed
candidate older than a coherent resident is rejected on-device. A legacy or
torn resident with no valid KBLV remains eligible so initial migration and
power-cut recovery cannot be locked out.

An old v3 updater requires the refresh image's firmware release version to be
strictly newer than its currently valid normal application. Consequently,
publish configurator `app-v0.1.18` (the minimum version that understands
descriptor schema 2 and both asset roles) before the firmware release that
first carries a resident updater fix. Firmware tag CI enforces that this stable
configurator release already exists and records the minimum in
`firmware-manifest.json`. If an older configurator has already installed
application release `X` without refreshing the bootloader, `X` cannot be reused
for the retrofit; publish a signed `X+1` release. This follows from the deployed
v3 anti-rollback contract and must not be bypassed by weakening equal-version
acceptance.

The v2 path must never be implemented as “skip AUTH and flash the normal
application”. Updater v2 erases sectors 4–6 and stores its validity trailer at
`0x0805FF00`. Current firmware uses sector 6 as profile bank A and erases it
during legacy sector-7 migration. A normal current application flashed by v2
would therefore erase the v2 trailer on its first boot and remain stuck in
updater mode after the next reset.

The native configurator validates artifact type, target, geometry, CRC,
SHA-512 digest, descriptor, vector tables and Ed25519 signature before entering
updater mode. The browser/TypeScript fallback deliberately refuses protocol v2
because it does not yet provide the pinned-key preflight required for a
bootloader that has no device-side authentication.

## Refresh image and v2 package layouts

`tools/release/build_updater_migration_package.py` creates two distinct release
roles from one exact signed inner image:

- `kbhe-updater-v3-refresh.bin` is the signed migrator executable, embedded
  updater-v3 binary and 128-byte descriptor. It fits the v3 app slot and is the
  only bootloader-refresh artifact accepted by an existing v3 updater.
- `kbhe-updater-v2-to-v3.bin` is the complete legacy package below. It contains
  the exact refresh image plus erased padding and the pre-seeded v3 trailer. It
  is the only migration artifact accepted by updater v2.

```text
0x08010000  signed migrator executable
            embedded updater-v3 binary
            128-byte KBHEMIG3 descriptor (last bytes of signed image)
            erased 0xFF padding
0x0803FF00  pre-seeded 84-byte updater-v3 signed trailer
0x0803FF54  end of distributed package
...
0x0805FF00  updater-v2 validity trailer (created by updater v2, not in file)
```

The signed `KBHEMIG3` descriptor binds the source and target protocols, exact
STM32F723/75HE hardware target, embedded bootloader offset/length, CRC-32 and
SHA-512 digest, and the exact resident bootloader version from its `KBLV`
record. The detached signatures for both release roles contain the same 64
bytes: they sign the exact inner migrator image and are also copied into the
pre-seeded v3 trailer. Neither signature is a generic artifact signature over
the padded v2 package. Padding between the signed image and trailer must remain
`0xFF`.

Pre-seeding the v3 trailer is important. Updater v2 writes it while the old
bootloader is still intact, so a power cut can be retried in updater mode. The
migrator never has to erase sector 5 while executing from sectors 4–5 merely to
repair a torn trailer.

Release preparation is intentionally two-stage because the trailer contains
the inner-image signature:

```powershell
python tools/release/build_updater_migration_package.py prepare `
  --migrator build/Release/kbhe_updater_migrator.bin `
  --bootloader build/Release/kbhe_bootloader.bin `
  --output dist/kbhe-updater-v3-refresh.bin

python tools/release/sign_release_asset.py firmware `
  --key firmware-release-private.pem `
  --input dist/kbhe-updater-v3-refresh.bin `
  --output dist/kbhe-updater-v3-refresh.bin.sig `
  --version X.Y.Z `
  --verify-public firmware/keys/firmware-ed25519-public.pem

python tools/release/build_updater_migration_package.py inspect-image `
  --input dist/kbhe-updater-v3-refresh.bin `
  --signature dist/kbhe-updater-v3-refresh.bin.sig `
  --version X.Y.Z `
  --verify-public firmware/keys/firmware-ed25519-public.pem

Copy-Item dist/kbhe-updater-v3-refresh.bin.sig `
  dist/kbhe-updater-v2-to-v3.bin.sig

python tools/release/build_updater_migration_package.py assemble `
  --image dist/kbhe-updater-v3-refresh.bin `
  --signature dist/kbhe-updater-v2-to-v3.bin.sig `
  --version X.Y.Z `
  --verify-public firmware/keys/firmware-ed25519-public.pem `
  --output dist/kbhe-updater-v2-to-v3.bin

python tools/release/build_updater_migration_package.py inspect `
  --input dist/kbhe-updater-v2-to-v3.bin `
  --signature dist/kbhe-updater-v2-to-v3.bin.sig `
  --version X.Y.Z `
  --verify-public firmware/keys/firmware-ed25519-public.pem
```

Do not publish a migration package until the migrator target and hardware
power-cut suite described below are green. Until then, the supported recovery
for a protocol-v2 keyboard remains the signed factory image through ROM DFU or
ST-Link.

Firmware tag CI therefore signs, uploads and byte-for-byte verifies a GitHub
**draft**, but never makes that draft public automatically. After the required
recovery-equipped HIL pass, publish that exact draft manually with
`gh release edit firmware-vX.Y.Z --draft=false`; do not rebuild or replace its
verified assets between HIL and publication.

### Manual HIL publication gate

The release operator must test the assets downloaded from the authenticated
draft, not a separate local rebuild. With `gh` authenticated for the repository:

```powershell
$tag = "firmware-vX.Y.Z"
$version = $tag -replace '^firmware-v', ''
$hil = Join-Path $env:TEMP "kbhe-$tag-hil"
New-Item -ItemType Directory -Path $hil | Out-Null
gh release view $tag --repo Fefe-Nayz/kbhe-monorepo --json isDraft,assets
gh release download $tag --repo Fefe-Nayz/kbhe-monorepo --dir $hil `
  --pattern 'kbhe-app-updater-v3.bin' `
  --pattern 'kbhe-app-updater-v3.bin.sig' `
  --pattern 'kbhe-updater-v2-to-v3.bin' `
  --pattern 'kbhe-updater-v2-to-v3.bin.sig' `
  --pattern 'kbhe-updater-v3-refresh.bin' `
  --pattern 'kbhe-updater-v3-refresh.bin.sig'

python tools/release/sign_release_asset.py firmware `
  --input "$hil/kbhe-app-updater-v3.bin" `
  --signature "$hil/kbhe-app-updater-v3.bin.sig" `
  --version $version `
  --verify-public firmware/keys/firmware-ed25519-public.pem
python tools/release/build_updater_migration_package.py inspect `
  --input "$hil/kbhe-updater-v2-to-v3.bin" `
  --signature "$hil/kbhe-updater-v2-to-v3.bin.sig" `
  --version $version `
  --verify-public firmware/keys/firmware-ed25519-public.pem
python tools/release/build_updater_migration_package.py inspect-image `
  --input "$hil/kbhe-updater-v3-refresh.bin" `
  --signature "$hil/kbhe-updater-v3-refresh.bin.sig" `
  --version $version `
  --verify-public firmware/keys/firmware-ed25519-public.pem
```

Confirm `isDraft` is `true` and that exactly one asset with each required name
was downloaded. Keep all six files together. In the native configurator,
select `kbhe-app-updater-v3.bin` and its detached signature; the native updater command
discovers the exact sibling support pairs and authenticates them before
touching the keyboard. It performs either `v2 -> full package -> migrator ->
v3 -> final app -> runtime`, `old v3 -> refresh image -> migrator -> current v3
-> final app -> runtime`, or skips directly to the final app when
`BOOTLOADER_INFO` reports a current resident bootloader.

Run this on a keyboard that still reports updater v2, with ROM-DFU or ST-Link
connected and a known-good factory image ready. Record at minimum: v2 HELLO,
successful capsule FINISH/BOOT, v3 HELLO after re-enumeration, exact
`BOOTLOADER_INFO`, v3 AUTH before BEGIN, final application version, option-byte
readback, several cold boots, and a successful recovery exercise. Repeat the
host flow starting from an older v3 updater and confirm that a second run skips
the refresh. Also interrupt power at each documented sector/option-byte
boundary on a dedicated destructive-test board. Only after those results are
attached to the release evidence may the operator run:

```powershell
gh release edit $tag --repo Fefe-Nayz/kbhe-monorepo --draft=false
```

## Resumable migrator state machine

The firmware migrator must derive progress from verified flash contents, not a
single mutable flag:

1. Validate its signed descriptor, pre-seeded v3 trailer and embedded updater.
2. Accept only the expected normal boot address (`0x0080` ITCM alias or
   `0x2000` AXIM) and a safe USB supply/PVD state.
3. Program and read back `BOOT_ADD0=0x2004` so resets start the migrator at
   `0x08010000`; reload options and reset before touching sectors 0–3.
4. Erase/program/verify updater v3 in sectors 0–2. Any reset repeats validation
   and resumes from the still-intact signed migrator.
5. Repair/initialize the transactional version-floor journal in sector 3 and
   verify that updater v3 accepts the pre-seeded signed migrator trailer.
6. Program/read back `BOOT_ADD0=0x2000`, request updater mode and reset. The new
   v3 updater remains available for the normal final application update.

The same signed inner image drives both entry paths. On an existing v3 updater,
AUTH and BEGIN first install it as the application; on v2, the complete package
also supplies the canonical v3 trailer that v2 cannot authenticate itself.
After the migrator returns, the configurator requires an exact target/version
`BOOTLOADER_INFO` response before it sends the final app. If power is lost,
leave the keyboard connected and retry: while sectors 0–3 are being replaced,
`BOOT_ADD0` points to the still-signed migrator in the app slot. Physical BOOT0
ROM-DFU or ST-Link remains the recovery path for the option-byte failure window
described below.

`BOOT_ADD0` values are addresses shifted right by 14: `0x08010000` is `0x2004`
and `0x08000000` is `0x2000`. The application build already relocates VTOR to
`0x08010000`, which is required when it is temporarily the reset target.

This design makes every main-flash erase/program phase resumable, but the
STM32F723 option-byte information block itself is erased and reprogrammed when
`OPTSTRT` is used. A power loss inside either short option-byte commit is not
guaranteed atomic by STM32. The migrator checks supply/PVD, quiesces interrupts,
changes only `BOOT_ADD0`, verifies readback and keeps physical BOOT0 ROM-DFU/
ST-Link recovery documented. Do not market the
software-only flow as unconditionally power-loss-safe until destructive HIL
tests demonstrate the board-specific behavior.

## Required tests

- Pure host tests for v2/v3 HELLO parsing, exact geometry and unknown versions.
- Transcript tests proving v2 never sends `AUTH` and can only receive a parsed,
  signed migration package; v3 must always send `AUTH` before `BEGIN` and must
  accept only the exact inner refresh role, never the padded v2 package.
- Optional bootloader-info tests covering an old v3 `INVALID_COMMAND`, exact
  target/version parsing, skip-current behavior and post-refresh verification.
- KBLV tests covering candidate/descriptor binding, refusal of a resident
  bootloader downgrade, and recovery when a legacy/torn resident has no record.
- Package tests for target, descriptor/trailer CRC, SHA-512, vector ranges,
  canonical padding, detached signature equality and all one-byte corruptions.
- Regression proving a normal v3 app is rejected on v2 before `BEGIN`.
- Hardware cuts before, during and after every sector erase/program and both
  option-byte commits, with ROM DFU/ST-Link attached for recovery evidence.
- Final v3 update, anti-rollback, legacy sector-7 profile migration and repeated
  reset tests after the one-time updater migration.
