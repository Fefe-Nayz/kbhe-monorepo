## RAW HID
``pip install hidapi``

Move hidapi.dll in the same folder of python executable

## Global protocol documentation

See the complete protocol reference (application RAW HID + updater RAW HID):

- docs/RAW_HID_PROTOCOL.MD

New identity commands:

- `GET_DEVICE_INFO (0x2B)` returns firmware version, serial number (UID base62), and keyboard name
- `GET_KEYBOARD_NAME (0x2C)` / `SET_KEYBOARD_NAME (0x2D)` read/write custom keyboard name (32 chars)

### Firmware Update
Flash the application image with the integrated updater command:

```bash
python host/raw_hid.py --flash build/Release/kbhe.bin
```

Optional arguments:

```bash
python host/raw_hid.py --flash build/Release/kbhe.bin --fw-version 0x0102 --timeout 5 --retries 5
```

### GUI
Launch the keyboard configurator GUI:

```bash
python host/raw_hid.py --gui
```

The GUI now includes a dedicated `Firmware` page with:
- file picker for the `.bin`
- optional firmware version override
- timeout/retry controls
- updater log window
- one-click flash button
