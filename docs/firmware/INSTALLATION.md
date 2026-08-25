# Firmware installation and updates

Protocol v3 boots only an application whose CRC, SHA-512 digest and Ed25519
signature are present in the signed trailer. Do not install a raw bootloader
HEX plus a raw application HEX: that pair has no trailer and intentionally
stays in updater mode.

## New board or migration from a legacy bootloader

Download these matching assets from one `firmware-vX.Y.Z` release:

- `kbhe-factory.bin`
- `kbhe-factory.bin.sig`

The 256-KiB factory image contains executable bootloader code in sectors 0–2
(`0x08000000..0x0800BFFF`), an anti-rollback journal seeded with the factory
version in sector 3 (`0x0800C000..0x0800FFFF`), the application at
`0x08010000`, and the authenticated trailer at `0x0803FF00`.

Verify the detached, role-bound signature before connecting a programmer:

```powershell
python tools/release/sign_release_asset.py artifact `
  --input kbhe-factory.bin `
  --signature kbhe-factory.bin.sig `
  --version X.Y.Z `
  --target kbhe-75he-stm32f723vet6 `
  --role kbhe-factory.bin `
  --verify-public firmware/keys/firmware-ed25519-public.pem
```

The expected SHA-256 fingerprint of the public-key DER is:

```text
c4f5921f9886ed50500b14d6f7fe60a7115a96fe78f56148e82405269895bb50
```

Compare the fingerprint with an independently distributed value (for example
the project website/manufacturing documentation or a previously authenticated
configurator), not only with another file from the same GitHub download.
Detached signatures protect against replacement of release assets; they cannot
establish trust if an attacker replaces the artifact, verifier and public key
through the same compromised channel.

Using STM32CubeProgrammer with ST-Link or the STM32 ROM DFU mode:

1. Connect the board and perform a full-chip erase for a new install.
2. Select `kbhe-factory.bin`.
3. Set the start address to `0x08000000`.
4. Program and run the target.

No second “finalization” flash is required. On its first normal boot, the v3
bootloader validates the embedded trailer and starts the application.

The USB updater never erases the bootloader, anti-rollback journal, or profile
banks. The current project does not provision STM32 WRP/PCROP/RDP option
bytes, so an attached hardware debugger can still replace them; production
option-byte locking is a separate manufacturing decision.

## Automatic update from KBHE Configurator

1. Connect the keyboard in normal mode.
2. Open **Firmware** and choose **Check for updates**.
3. Download and install the offered release.

The configurator downloads `kbhe-app.bin` and its exact
`kbhe-app.bin.sig`, verifies the signature locally, then sends AUTH before the
bootloader is permitted to erase sectors 4–5. Stable releases also carry a
locally verified `kbhe-updater-v2-to-v3.bin` capsule. When HELLO reports v2,
one native update command installs that capsule, waits for v3 to re-enumerate,
then authenticates and installs the normal application before returning to
runtime mode. The bootloader re-verifies the signature before BEGIN, after
programming, and on every boot.

The configurator negotiates updater v2 and v3 explicitly. A normal application
is never sent to updater v2 because the legacy validity trailer and the current
sector-6 profile bank conflict. A release without the validated migration pair
fails with `UPDATER_MIGRATION_REQUIRED` before sending the normal application;
the physical factory flow above remains the recovery path. See
[Updater compatibility and v2-to-v3 migration](UPDATER_COMPATIBILITY.md).

Firmware versions are monotonic through the normal USB updater. Before erasing
the application, the bootloader transactionally raises an append-only version
floor in sector 3. Versions below that floor are rejected even after a power
failure has erased the application trailer. The reset and explicit BOOT paths
also consult the floor: a cut after the floor commit but before sector erase
leaves the keyboard safely in updater mode instead of starting the older image.
An entirely erased journal permits the first signed factory image; a programmed
journal with no valid committed record is treated as corrupt and also stays in
updater mode so recovery cannot silently weaken the floor. This state requires
an explicit physical factory reflash; the USB updater does not redefine an
unknown floor.
The exact floor version is allowed only as recovery while no valid application
exists; a replay less than or equal to a valid installed image is rejected. A
deliberately selected physical full-chip factory reflash remains the recovery
path.

## Manual signed USB update

Download the matching pair `kbhe-app.bin` and `kbhe-app.bin.sig`. In the
configurator, select the BIN; when prompted, select its SIG sibling. Unsigned,
mismatched and non-newer images are rejected before application flash erase.

The Python host performs the same authenticated flow:

```powershell
python tools/host/firmware_updater.py kbhe-app.bin `
  --signature kbhe-app.bin.sig `
  --fw-version 0x020006
```

The host follows the selected keyboard's USB serial number across runtime and
updater re-enumeration. With several KBHE keyboards connected, add
`--serial <STM32-UID>`; missing or duplicated serial identities are refused
before ENTER_BOOTLOADER or flash erase. The autonomous retry tool locks the
first unambiguous UID and keeps that same target for every later attempt.

Use the semantic version encoded by the selected release. Subsequent updates
do not require STM32CubeProgrammer and do not overwrite on-device profiles.
