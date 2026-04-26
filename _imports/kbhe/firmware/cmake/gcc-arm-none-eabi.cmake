set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
set(TOOLCHAIN_PREFIX                arm-none-eabi-)
set(ARM_GCC_BIN_DIR "" CACHE PATH "Directory containing arm-none-eabi toolchain binaries")

if(WIN32 AND NOT ARM_GCC_BIN_DIR)
    file(GLOB _cubeide_arm_gcc
        "C:/ST/STM32CubeIDE_*/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-gcc.exe"
    )

    if(_cubeide_arm_gcc)
        list(SORT _cubeide_arm_gcc)
        list(REVERSE _cubeide_arm_gcc)
        list(GET _cubeide_arm_gcc 0 _arm_gcc_path)
        get_filename_component(ARM_GCC_BIN_DIR "${_arm_gcc_path}" DIRECTORY)
    endif()
endif()

if(ARM_GCC_BIN_DIR)
    file(TO_CMAKE_PATH "${ARM_GCC_BIN_DIR}" ARM_GCC_BIN_DIR)
    set(_toolchain_cmd_prefix "${ARM_GCC_BIN_DIR}/${TOOLCHAIN_PREFIX}")
else()
    set(_toolchain_cmd_prefix "${TOOLCHAIN_PREFIX}")
endif()

if(WIN32)
    set(_toolchain_exe_suffix ".exe")
else()
    set(_toolchain_exe_suffix "")
endif()

set(CMAKE_C_COMPILER                ${_toolchain_cmd_prefix}gcc${_toolchain_exe_suffix})
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${_toolchain_cmd_prefix}g++${_toolchain_exe_suffix})
set(CMAKE_LINKER                    ${_toolchain_cmd_prefix}g++${_toolchain_exe_suffix})
set(CMAKE_OBJCOPY                   ${_toolchain_cmd_prefix}objcopy${_toolchain_exe_suffix})
set(CMAKE_SIZE                      ${_toolchain_cmd_prefix}size${_toolchain_exe_suffix})

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -fdata-sections -ffunction-sections")

set(CMAKE_C_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_C_FLAGS_RELEASE "-O3 -g0 -flto")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -g0 -flto")

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_EXE_LINKER_FLAGS "${TARGET_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--print-memory-usage")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto")
set(TOOLCHAIN_LINK_LIBRARIES "m")
