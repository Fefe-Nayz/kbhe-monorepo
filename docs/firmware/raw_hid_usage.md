# RAW HID Usage

Install the Python HID dependency:

```powershell
pip install hidapi
```

On Windows, if the bundled `hidapi.dll` is needed, keep it next to the Python
entry point that runs the tools.

## Global protocol documentation

See the complete protocol reference for application RAW HID and updater RAW HID:

- [`RAW_HID_PROTOCOL.MD`](RAW_HID_PROTOCOL.MD)

New identity commands:

- `GET_DEVICE_INFO (0x2B)` returns firmware version, serial number (UID base62), and keyboard name
- `GET_KEYBOARD_NAME (0x2C)` / `SET_KEYBOARD_NAME (0x2D)` read/write custom keyboard name (32 chars)

### Firmware Update
Use a matching signed pair downloaded from one firmware release:

```powershell
python tools/host/firmware_updater.py kbhe-app.bin `
  --signature kbhe-app.bin.sig --fw-version 0x020006
```

Timeout/retry controls are optional:

```powershell
python tools/host/firmware_updater.py kbhe-app.bin `
  --signature kbhe-app.bin.sig --fw-version 0x020006 `
  --timeout 5 --retries 5
```

The `.sig` argument is optional only syntactically: when omitted the tool reads
`<firmware>.sig`. Flashing always requires a valid detached Ed25519 signature,
and the host verifies it before asking the bootloader to erase flash.
Local CMake builds intentionally do not produce a release signature. A local
development image must be signed with an explicitly managed development key
whose public key is compiled into that development bootloader; never copy the
production private key into the repository or build directory.

### GUI
Launch the keyboard configurator GUI:

```powershell
python tools/host/raw_hid.py --gui
```

The GUI now includes a dedicated `Firmware` page with:

- file picker for the `.bin` (with a matching sibling `.bin.sig`)
- optional firmware version override
- timeout/retry controls
- updater log window
- one-click flash button
