# Getting Started

## Installation

Compiled firmware binary are available in the [releases tab](https://github.com/kbhe/kbhe-monorepo/releases)

### 1) Download firmware bootloader

Download the latest `kbhe_bootloader.hex` from the [releases](https://github.com/kbhe/kbhe-monorepo/releases)

### 2) Download firmware application

Download the latest `kbhe_application.hex` from the [releases](https://github.com/kbhe/kbhe-monorepo/releases)

### 3) Flash firmware

Using STM32CubeProgrammer:

1. Connect the board in DFU mode (using boot switch) or use an external programmer (ST-Link)
2. Make a full chip erase (mass erase)
3. Flash the bootloader `kbhe_bootloader.hex` ensure address is set to `0x08000000`
4. Flash the application `kbhe_application.hex` ensure address is set to `0x08010000`

### 4) First boot

Using KBHE Configurator:

1. Connect the board in normal mode (boot switch in normal position)
2. Open KBHE Configurator
3. Go to the Firmware Update section
4. Flash the application `kbhe-app.bin`

### 5) Check firmware version

1. Go to the About section in KBHE Configurator
2. Check the firmware version displayed matches the version of the application flashed

## Update firmware

### Auto-update from KBHE Configurator:

> Update to the latest firmware version. This doesn't update bootloader.

1. Connect the board in normal mode (boot switch in normal position)
2. Open KBHE Configurator
3. Go to the Firmware Update section
4. Click "Check for updates"
5. If an update is available, click "Download and install"
6. Follow the prompts to complete the update

### Manual update from KBHE Configurator:

> Update to any specific firmware version. This doesn't update bootloader.

1. Connect the board in normal mode (boot switch in normal position)
2. Open KBHE Configurator
3. Go to the Firmware Update section
4. Click "Select firmware file"
5. Choose the downloaded `kbhe-app.bin` file
6. Click "Flash firmware"

### Manual update from STM32CubeProgrammer:

> Update both bootloader and firmware to any specific version. This is the only way to update the bootloader.

Follow the same steps as [Installation](#Installation)