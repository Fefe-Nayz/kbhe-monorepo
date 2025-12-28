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

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # TinyUSB sources
    ${TINYUSB_SRC}
    # USB application sources
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_descriptors.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/usb_hid.c
    # ADC filtering
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/adc_ema.c
    # Key trigger logic
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/trigger.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # TinyUSB includes
    ${TINYUSB_INC}
    # Project includes (pour tusb_config.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # HSE value for USB HS PHY PLL configuration
    HSE_VALUE=16000000U
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
