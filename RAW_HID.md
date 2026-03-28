## RAW HID
``pip install hidapi``

Move hidapi.dll in the same folder of python executable

### Firmware Update
Flash the application image with the integrated updater command:

```bash
python raw_hid.py --flash build/Release/kbhe.bin
```

Optional arguments:

```bash
python raw_hid.py --flash build/Release/kbhe.bin --fw-version 0x0102 --timeout 5 --retries 5
```

### GUI
Launch the keyboard configurator GUI:

```bash
python raw_hid.py --gui
```

The GUI now includes a dedicated `Firmware` page with:
- file picker for the `.bin`
- optional firmware version override
- timeout/retry controls
- updater log window
- one-click flash button
