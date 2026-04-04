cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME kbhe)
set(KBHE_BOOTLOADER_TARGET kbhe_bootloader)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

set(STM32_SYSTEM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/system_stm32f7xx.c)

#--------------------------------------------------------------------+
# TinyUSB Configuration
#--------------------------------------------------------------------+
set(TINYUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/tinyusb)

# TinyUSB source files
set(TINYUSB_SRC
    # Core
    ${TINYUSB_DIR}/src/tusb.c
    ${TINYUSB_DIR}/src/common/tusb_fifo.c
    # Device stack
    ${TINYUSB_DIR}/src/device/usbd.c
    ${TINYUSB_DIR}/src/device/usbd_control.c
    # HID class
    ${TINYUSB_DIR}/src/class/hid/hid_device.c
    # DWC2 driver (Synopsys pour STM32)
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dcd_dwc2.c
    ${TINYUSB_DIR}/src/portable/synopsys/dwc2/dwc2_common.c
)

# TinyUSB include directories
set(TINYUSB_INC
    ${TINYUSB_DIR}/src
    ${TINYUSB_DIR}/src/common
)

function(kbhe_configure_binary_target target linker_script vect_tab_offset)
    target_compile_definitions(${target} PRIVATE
        HSE_VALUE=16000000U
        USER_VECT_TAB_ADDRESS
        VECT_TAB_OFFSET=${vect_tab_offset}
    )

    target_link_options(${target} PRIVATE
        "-T${linker_script}"
        "-Wl,-Map=$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.map"
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Obinary
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.bin
        COMMAND ${CMAKE_OBJCOPY} -Oihex
                $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.hex
        COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${target}>
        VERBATIM
    )

    set_target_properties(${target} PROPERTIES
        ADDITIONAL_CLEAN_FILES
        "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.bin;$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.hex;$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.map"
    )
endfunction()

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${STM32_SYSTEM_SRC}
    # TinyUSB sources
    ${TINYUSB_SRC}
    # USB application sources
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_descriptors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_hid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_hid_nkro.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_gamepad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/raw_hid.c
    # ADC filtering
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/adc_ema.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/adc_capture.c
    # Key trigger logic
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/trigger.c
    # Settings and flash storage
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/flash_storage.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/settings.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/hid_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/updater_shared.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/updater_app.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/analog/analog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/analog/calibration.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/analog/lut.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/analog/multiplexer.c
    # LED Matrix (WS2812)
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/led_matrix.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/WS2812/Src/ws2812.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/WS2812/Src/color_values.c
    # LED Indicator (PE0)
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/led_indicator.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # TinyUSB includes
    ${TINYUSB_INC}
    # Project includes (pour tusb_config.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    # WS2812 driver includes
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/WS2812/Inc
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

kbhe_configure_binary_target(
    ${CMAKE_PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/STM32F723XX_FLASH.ld
    0x00010000U
)

add_executable(${KBHE_BOOTLOADER_TARGET})

target_sources(${KBHE_BOOTLOADER_TARGET} PRIVATE
    ${STM32_SYSTEM_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_stm32f723xx.s
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/sysmem.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/syscalls.c
    ${TINYUSB_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/updater_shared.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Src/bootloader_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Src/updater_bootloader.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Src/usb_descriptors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Src/stm32f7xx_it.c
)

target_include_directories(${KBHE_BOOTLOADER_TARGET} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/Inc
)

target_include_directories(${KBHE_BOOTLOADER_TARGET} PRIVATE
    ${TINYUSB_INC}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
)

target_link_libraries(${KBHE_BOOTLOADER_TARGET}
    stm32cubemx
    STM32_Drivers
)

kbhe_configure_binary_target(
    ${KBHE_BOOTLOADER_TARGET}
    ${CMAKE_CURRENT_SOURCE_DIR}/Bootloader/STM32F723XX_BOOTLOADER.ld
    0x00000000U
)
