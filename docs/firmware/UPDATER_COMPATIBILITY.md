# Updater compatibility and v2-to-v3 migration

The configurator negotiates the updater protocol from the common 20-byte
`HELLO` response. It does not infer the updater version from the application
firmware version: the bootloader is persistent and the two versions can differ.

## Compatibility matrix

| Updater | Geometry | Allowed artifact | Transfer |
|---|---|---|---|
| v3 (`0x0003`) | app `0x08010000`, max `0x2FF00`, 4-byte writes | normal signed `kbhe-app.bin` only | `HELLO -> AUTH -> BEGIN -> DATA -> FINISH -> BOOT` |
| v2 (`0x0002`) | app `0x08010000`, max `0x4FF00`, 4-byte writes | exact signed `kbhe-updater-v2-to-v3.bin` package only | `HELLO -> BEGIN -> DATA -> FINISH -> BOOT` |
| other | exact matching is required | none | fail before `BEGIN` |

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

## Migration package layout

`tools/release/build_updater_migration_package.py` defines the release format:

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
SHA-512 digest. The detached `.sig` signs the exact inner migrator image and is
also copied into the pre-seeded v3 trailer. Padding between the signed image and
trailer must remain `0xFF`.

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
  --output dist/kbhe-updater-v2-to-v3-inner.bin

python tools/release/sign_release_asset.py firmware `
  --key firmware-release-private.pem `
  --input dist/kbhe-updater-v2-to-v3-inner.bin `
  --output dist/kbhe-updater-v2-to-v3.bin.sig `
  --version X.Y.Z `
  --verify-public firmware/keys/firmware-ed25519-public.pem

python tools/release/build_updater_migration_package.py assemble `
  --image dist/kbhe-updater-v2-to-v3-inner.bin `
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
  --pattern 'kbhe-app.bin' `
  --pattern 'kbhe-app.bin.sig' `
  --pattern 'kbhe-updater-v2-to-v3.bin' `
  --pattern 'kbhe-updater-v2-to-v3.bin.sig'

python tools/release/sign_release_asset.py firmware `
  --input "$hil/kbhe-app.bin" `
  --signature "$hil/kbhe-app.bin.sig" `
  --version $version `
  --verify-public firmware/keys/firmware-ed25519-public.pem
python tools/release/build_updater_migration_package.py inspect `
  --input "$hil/kbhe-updater-v2-to-v3.bin" `
  --signature "$hil/kbhe-updater-v2-to-v3.bin.sig" `
  --version $version `
  --verify-public firmware/keys/firmware-ed25519-public.pem
```

Confirm `isDraft` is `true` and that exactly one asset with each required name
was downloaded. Keep all four files together. In the native configurator,
select `kbhe-app.bin` and its detached signature; the native updater command
discovers the exact sibling migration pair, authenticates both before touching
the keyboard, and performs `v2 -> migrator -> v3 -> final app -> runtime`.

Run this on a keyboard that still reports updater v2, with ROM-DFU or ST-Link
connected and a known-good factory image ready. Record at minimum: v2 HELLO,
successful capsule FINISH/BOOT, v3 HELLO after re-enumeration, v3 AUTH before
BEGIN, final application version, option-byte readback, several cold boots, and
a successful recovery exercise. Also interrupt power at each documented
sector/option-byte boundary on a dedicated destructive-test board. Only after
those results are attached to the release evidence may the operator run:

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
  signed migration package; v3 must always send `AUTH` before `BEGIN`.
- Package tests for target, descriptor/trailer CRC, SHA-512, vector ranges,
  canonical padding, detached signature equality and all one-byte corruptions.
- Regression proving a normal v3 app is rejected on v2 before `BEGIN`.
- Hardware cuts before, during and after every sector erase/program and both
  option-byte commits, with ROM DFU/ST-Link attached for recovery evidence.
- Final v3 update, anti-rollback, legacy sector-7 profile migration and repeated
  reset tests after the one-time updater migration.
