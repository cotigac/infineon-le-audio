#-------------------------------------------------------------------------------
# arm-cortex-m55.cmake - ARM Cortex-M55 Toolchain File
#
# Cross-compilation toolchain for PSoC Edge E81 (Cortex-M55)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-cortex-m55.cmake ..
#
# Prerequisites:
#   - ARM GNU Toolchain installed
#   - arm-none-eabi-gcc in PATH
#
# Download ARM GNU Toolchain:
#   https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
#
# Copyright 2024 Cristian Cotiga
# SPDX-License-Identifier: Apache-2.0
#-------------------------------------------------------------------------------

# Target system
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m55)

# Prevent CMake from testing the compiler
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

#-------------------------------------------------------------------------------
# Toolchain Prefix
#-------------------------------------------------------------------------------

set(TOOLCHAIN_PREFIX arm-none-eabi-)

# Try to find the toolchain in common locations
if(NOT DEFINED ARM_TOOLCHAIN_PATH)
    # Check if tools are in PATH
    find_program(ARM_GCC_EXECUTABLE ${TOOLCHAIN_PREFIX}gcc)
    if(ARM_GCC_EXECUTABLE)
        get_filename_component(ARM_TOOLCHAIN_PATH ${ARM_GCC_EXECUTABLE} DIRECTORY)
    else()
        # Common installation paths
        if(WIN32)
            set(TOOLCHAIN_SEARCH_PATHS
                "C:/Program Files/Arm GNU Toolchain arm-none-eabi/*/bin"
                "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin"
                "C:/gcc-arm-none-eabi-*/bin"
                "$ENV{USERPROFILE}/gcc-arm-none-eabi-*/bin"
            )
        else()
            set(TOOLCHAIN_SEARCH_PATHS
                "/opt/arm-gnu-toolchain-*/bin"
                "/usr/local/gcc-arm-none-eabi-*/bin"
                "$ENV{HOME}/gcc-arm-none-eabi-*/bin"
                "/Applications/ARM/bin"
            )
        endif()

        foreach(PATH ${TOOLCHAIN_SEARCH_PATHS})
            file(GLOB FOUND_PATHS ${PATH})
            if(FOUND_PATHS)
                list(GET FOUND_PATHS -1 ARM_TOOLCHAIN_PATH)
                break()
            endif()
        endforeach()
    endif()
endif()

# Set toolchain path hint
if(DEFINED ARM_TOOLCHAIN_PATH)
    message(STATUS "ARM Toolchain Path: ${ARM_TOOLCHAIN_PATH}")
    set(TOOLCHAIN_BIN_DIR ${ARM_TOOLCHAIN_PATH})
else()
    message(STATUS "ARM Toolchain: Using PATH environment")
    set(TOOLCHAIN_BIN_DIR "")
endif()

#-------------------------------------------------------------------------------
# Compilers and Tools
#-------------------------------------------------------------------------------

set(CMAKE_C_COMPILER ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}size)
set(CMAKE_AR ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}ar)
set(CMAKE_RANLIB ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}ranlib)
set(CMAKE_NM ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}nm)
set(CMAKE_STRIP ${TOOLCHAIN_BIN_DIR}${TOOLCHAIN_PREFIX}strip)

#-------------------------------------------------------------------------------
# Cortex-M55 Specific Flags
#-------------------------------------------------------------------------------

# Core-specific flags
set(CPU_FLAGS "-mcpu=cortex-m55 -mthumb")

# FPU flags (Cortex-M55 has FPv5-D16 FPU)
set(FPU_FLAGS "-mfloat-abi=hard -mfpu=fpv5-d16")

# MVE (Helium) flags - M-Profile Vector Extension
# Uncomment to enable Helium SIMD instructions
# set(MVE_FLAGS "-march=armv8.1-m.main+mve.fp")

# Combined architecture flags
set(ARCH_FLAGS "${CPU_FLAGS} ${FPU_FLAGS}")

#-------------------------------------------------------------------------------
# Compiler Flags
#-------------------------------------------------------------------------------

# Common flags for all build types
set(COMMON_C_FLAGS
    "${ARCH_FLAGS}"
    "-ffunction-sections"       # Place functions in separate sections
    "-fdata-sections"           # Place data in separate sections
    "-fno-common"               # Don't allow uninitialized globals
    "-fno-exceptions"           # Disable C++ exceptions
    "-Wall"                     # Enable all warnings
    "-Wextra"                   # Extra warnings
    "-Wno-unused-parameter"     # Don't warn about unused params
    "-Wformat=2"                # Format string checks
    "-Wformat-security"         # Format security warnings
)

# Convert list to string
string(REPLACE ";" " " COMMON_C_FLAGS_STR "${COMMON_C_FLAGS}")

# C flags
set(CMAKE_C_FLAGS_INIT "${COMMON_C_FLAGS_STR}")
set(CMAKE_C_FLAGS_DEBUG "-Og -g3 -gdwarf-4 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -flto")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "-Os -DNDEBUG -flto")

# ASM flags
set(CMAKE_ASM_FLAGS_INIT "${ARCH_FLAGS}")
set(CMAKE_ASM_FLAGS_DEBUG "-g")
set(CMAKE_ASM_FLAGS_RELEASE "")

#-------------------------------------------------------------------------------
# Linker Flags
#-------------------------------------------------------------------------------

set(COMMON_LINK_FLAGS
    "${ARCH_FLAGS}"
    "--specs=nano.specs"        # Use newlib-nano
    "--specs=nosys.specs"       # No system calls
    "-Wl,--gc-sections"         # Remove unused sections
    "-Wl,--print-memory-usage"  # Print memory usage
    "-Wl,--cref"                # Cross reference table in map file
)

string(REPLACE ";" " " COMMON_LINK_FLAGS_STR "${COMMON_LINK_FLAGS}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "${COMMON_LINK_FLAGS_STR}")
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "-Wl,-Map=output.map")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-flto -Wl,-Map=output.map")

#-------------------------------------------------------------------------------
# Search Paths
#-------------------------------------------------------------------------------

# Only search in target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

#-------------------------------------------------------------------------------
# Target Definitions
#-------------------------------------------------------------------------------

# Define target architecture
add_compile_definitions(
    __CORTEX_M55
    ARM_MATH_CM55
    __ARM_FEATURE_MVE=1
)

#-------------------------------------------------------------------------------
# Helper Functions
#-------------------------------------------------------------------------------

# Function to generate binary file from ELF
function(target_generate_binary TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> ${TARGET}.bin
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating ${TARGET}.bin"
    )
endfunction()

# Function to generate hex file from ELF
function(target_generate_hex TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET}> ${TARGET}.hex
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating ${TARGET}.hex"
    )
endfunction()

# Function to print size information
function(target_print_size TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${TARGET}>
        COMMENT "Size of ${TARGET}:"
    )
endfunction()

# Function to generate disassembly
function(target_generate_disassembly TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_OBJDUMP} -d -S $<TARGET_FILE:${TARGET}> > ${TARGET}.lst
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating ${TARGET}.lst"
    )
endfunction()
