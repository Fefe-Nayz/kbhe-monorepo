# Build

This project uses CMake.

Available presets:

- `Debug`
- `Release`
- `Release-apponly` (without custom bootloader)

## Requirements

- CMake 4.2.1

## Build firmware

```bash
cmake --build --preset Release
```

The expected artifacts in `Release` are:

- `build/Release/kbhe_bootloader.hex`
- `build/Release/kbhe_bootloader.bin`
- `build/Release/kbhe.hex`
- `build/Release/kbhe.bin`