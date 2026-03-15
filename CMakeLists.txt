#-------------------------------------------------------------------------------
# CMakeLists.txt - PSoC Edge E81 LE Audio Demo
#
# Build system for the Infineon PSoC Edge E81 + CYW55511 LE Audio project.
#
# Prerequisites:
#   - ARM GNU Toolchain (arm-none-eabi-gcc)
#   - CMake 3.20+
#   - Ninja or Make
#
# Build Commands:
#   mkdir build && cd build
#   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-cortex-m55.cmake ..
#   cmake --build .
#
# Configuration Options:
#   -DCMAKE_BUILD_TYPE=Debug|Release|RelWithDebInfo|MinSizeRel
#   -DENABLE_LE_AUDIO=ON|OFF
#   -DENABLE_AURACAST=ON|OFF
#   -DENABLE_MIDI=ON|OFF
#
# Copyright 2024 Cristian Cotiga
# SPDX-License-Identifier: Apache-2.0
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

#-------------------------------------------------------------------------------
# Project Definition
#-------------------------------------------------------------------------------

project(infineon_le_audio
    VERSION 1.0.0
    DESCRIPTION "PSoC Edge E81 + CYW55511 LE Audio Musical Instrument Demo"
    LANGUAGES C ASM
)

#-------------------------------------------------------------------------------
# Build Options
#-------------------------------------------------------------------------------

option(ENABLE_LE_AUDIO "Enable LE Audio (BAP, PACS, ASCS)" ON)
option(ENABLE_AURACAST "Enable Auracast Broadcast (BIS)" ON)
option(ENABLE_MIDI "Enable MIDI over BLE and USB" ON)
option(ENABLE_USB "Enable USB Device support" ON)
option(ENABLE_DEBUG "Enable debug output" ON)
option(ENABLE_STATS "Enable runtime statistics" ON)
option(BUILD_TESTS "Build unit tests" OFF)

#-------------------------------------------------------------------------------
# Target Configuration
#-------------------------------------------------------------------------------

# PSoC Edge E81 - Cortex-M55
set(MCU_FAMILY "PSE81" CACHE STRING "MCU family")
set(MCU_CORE "cortex-m55" CACHE STRING "CPU core")
set(MCU_FPU "hard" CACHE STRING "FPU type (soft/hard)")
set(MCU_FLOAT_ABI "hard" CACHE STRING "Float ABI")

# Memory configuration
set(FLASH_SIZE "512K" CACHE STRING "Flash size")
set(SRAM_SIZE "4M" CACHE STRING "SRAM size")
set(FLASH_ORIGIN "0x10000000" CACHE STRING "Flash start address")
set(SRAM_ORIGIN "0x20000000" CACHE STRING "SRAM start address")

#-------------------------------------------------------------------------------
# Compiler Settings
#-------------------------------------------------------------------------------

# C Standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Common compiler flags
set(COMMON_FLAGS
    -mcpu=${MCU_CORE}
    -mthumb
    -mfloat-abi=${MCU_FLOAT_ABI}
    -mfpu=fpv5-d16
    -ffunction-sections
    -fdata-sections
    -fno-common
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Debug flags
set(DEBUG_FLAGS
    -g3
    -gdwarf-4
    -O0
    -DDEBUG
)

# Release flags
set(RELEASE_FLAGS
    -O2
    -DNDEBUG
    -flto
)

# Apply flags based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(${COMMON_FLAGS} ${DEBUG_FLAGS})
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(${COMMON_FLAGS} ${RELEASE_FLAGS})
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(${COMMON_FLAGS} -O2 -g)
elseif(CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(${COMMON_FLAGS} -Os -DNDEBUG)
else()
    # Default to Debug
    add_compile_options(${COMMON_FLAGS} ${DEBUG_FLAGS})
endif()

# Linker flags
add_link_options(
    -mcpu=${MCU_CORE}
    -mthumb
    -mfloat-abi=${MCU_FLOAT_ABI}
    -mfpu=fpv5-d16
    --specs=nano.specs
    --specs=nosys.specs
    -Wl,--gc-sections
    -Wl,-Map=${PROJECT_NAME}.map
)

#-------------------------------------------------------------------------------
# Compile Definitions
#-------------------------------------------------------------------------------

add_compile_definitions(
    # Target
    CY_MCU_PSE81
    __CORTEX_M55
    ARM_MATH_CM55

    # Features
    $<$<BOOL:${ENABLE_LE_AUDIO}>:ENABLE_LE_AUDIO=1>
    $<$<BOOL:${ENABLE_AURACAST}>:ENABLE_AURACAST=1>
    $<$<BOOL:${ENABLE_MIDI}>:ENABLE_MIDI=1>
    $<$<BOOL:${ENABLE_USB}>:ENABLE_USB=1>
    $<$<BOOL:${ENABLE_DEBUG}>:ENABLE_DEBUG=1>
    $<$<BOOL:${ENABLE_STATS}>:ENABLE_STATS=1>

    # FreeRTOS
    USE_FREERTOS
)

#-------------------------------------------------------------------------------
# Include Directories
#-------------------------------------------------------------------------------

set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/source
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio
    ${CMAKE_CURRENT_SOURCE_DIR}/source/midi
)

include_directories(${PROJECT_INCLUDE_DIRS})

#-------------------------------------------------------------------------------
# FreeRTOS Library
#-------------------------------------------------------------------------------

set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/freertos)

# FreeRTOS kernel sources
set(FREERTOS_SOURCES
    ${FREERTOS_DIR}/tasks.c
    ${FREERTOS_DIR}/queue.c
    ${FREERTOS_DIR}/list.c
    ${FREERTOS_DIR}/timers.c
    ${FREERTOS_DIR}/event_groups.c
    ${FREERTOS_DIR}/stream_buffer.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM55_NTZ/non_secure/port.c
    ${FREERTOS_DIR}/portable/GCC/ARM_CM55_NTZ/non_secure/portasm.c
    ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

# FreeRTOS include directories
set(FREERTOS_INCLUDE_DIRS
    ${FREERTOS_DIR}/include
    ${FREERTOS_DIR}/portable/GCC/ARM_CM55_NTZ/non_secure
    ${CMAKE_CURRENT_SOURCE_DIR}/config  # For FreeRTOSConfig.h
)

add_library(freertos STATIC ${FREERTOS_SOURCES})
target_include_directories(freertos PUBLIC ${FREERTOS_INCLUDE_DIRS})
target_compile_definitions(freertos PUBLIC
    portUSING_MPU_WRAPPERS=0
)

#-------------------------------------------------------------------------------
# liblc3 Library
#-------------------------------------------------------------------------------

set(LC3_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/liblc3)

# LC3 sources
set(LC3_SOURCES
    ${LC3_DIR}/src/attdet.c
    ${LC3_DIR}/src/bits.c
    ${LC3_DIR}/src/bwdet.c
    ${LC3_DIR}/src/energy.c
    ${LC3_DIR}/src/lc3.c
    ${LC3_DIR}/src/ltpf.c
    ${LC3_DIR}/src/mdct.c
    ${LC3_DIR}/src/plc.c
    ${LC3_DIR}/src/sns.c
    ${LC3_DIR}/src/spec.c
    ${LC3_DIR}/src/tables.c
    ${LC3_DIR}/src/tns.c
)

add_library(lc3 STATIC ${LC3_SOURCES})
target_include_directories(lc3 PUBLIC
    ${LC3_DIR}/include
    ${LC3_DIR}/src
)
target_compile_definitions(lc3 PRIVATE
    LC3_PLUS=0
)
# Enable ARM optimizations if available
target_compile_options(lc3 PRIVATE
    -O2
)

#-------------------------------------------------------------------------------
# BTStack Library (Infineon)
#-------------------------------------------------------------------------------

set(BTSTACK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/btstack)

# Core BTStack sources (subset - full list depends on features needed)
set(BTSTACK_SOURCES
    ${BTSTACK_DIR}/src/btstack_memory.c
    ${BTSTACK_DIR}/src/btstack_linked_list.c
    ${BTSTACK_DIR}/src/btstack_run_loop.c
    ${BTSTACK_DIR}/src/btstack_util.c
    ${BTSTACK_DIR}/src/hci.c
    ${BTSTACK_DIR}/src/hci_cmd.c
    ${BTSTACK_DIR}/src/hci_dump.c
    ${BTSTACK_DIR}/src/hci_transport_h4.c
    ${BTSTACK_DIR}/src/l2cap.c
    ${BTSTACK_DIR}/src/l2cap_signaling.c
    ${BTSTACK_DIR}/src/ad_parser.c
    ${BTSTACK_DIR}/src/btstack_crypto.c
    # BLE
    ${BTSTACK_DIR}/src/ble/att_db.c
    ${BTSTACK_DIR}/src/ble/att_dispatch.c
    ${BTSTACK_DIR}/src/ble/att_server.c
    ${BTSTACK_DIR}/src/ble/gatt_client.c
    ${BTSTACK_DIR}/src/ble/le_device_db_memory.c
    ${BTSTACK_DIR}/src/ble/sm.c
    # GAP
    ${BTSTACK_DIR}/src/gap.c
)

# BTStack include directories
set(BTSTACK_INCLUDE_DIRS
    ${BTSTACK_DIR}/src
    ${BTSTACK_DIR}/src/ble
    ${BTSTACK_DIR}/platform/freertos
    ${BTSTACK_DIR}/3rd-party/micro-ecc
)

add_library(btstack STATIC ${BTSTACK_SOURCES})
target_include_directories(btstack PUBLIC ${BTSTACK_INCLUDE_DIRS})
target_link_libraries(btstack PUBLIC freertos)
target_compile_definitions(btstack PUBLIC
    ENABLE_LOG_INFO
    ENABLE_LOG_ERROR
)

#-------------------------------------------------------------------------------
# USB Device Library (Infineon)
#-------------------------------------------------------------------------------

if(ENABLE_USB)
    set(USBDEV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/usbdev)

    # USB device sources (basic set)
    set(USBDEV_SOURCES
        ${USBDEV_DIR}/cy_usb_dev.c
        ${USBDEV_DIR}/cy_usb_dev_audio.c
        ${USBDEV_DIR}/cy_usb_dev_cdc.c
        ${USBDEV_DIR}/cy_usb_dev_descr.c
        ${USBDEV_DIR}/cy_usb_dev_hid.c
    )

    # Check if sources exist (library structure may vary)
    set(USBDEV_SOURCES_FOUND)
    foreach(src ${USBDEV_SOURCES})
        if(EXISTS ${src})
            list(APPEND USBDEV_SOURCES_FOUND ${src})
        endif()
    endforeach()

    if(USBDEV_SOURCES_FOUND)
        add_library(usbdev STATIC ${USBDEV_SOURCES_FOUND})
        target_include_directories(usbdev PUBLIC ${USBDEV_DIR})
    else()
        message(WARNING "USB device library sources not found at expected paths")
        # Create interface library as placeholder
        add_library(usbdev INTERFACE)
        target_include_directories(usbdev INTERFACE ${USBDEV_DIR})
    endif()
endif()

#-------------------------------------------------------------------------------
# Application Sources
#-------------------------------------------------------------------------------

# Audio module sources
set(AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio/audio_task.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio/audio_buffers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio/i2s_stream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/audio/lc3_wrapper.c
)

# Bluetooth module sources
set(BLUETOOTH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth/bt_init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth/hci_isoc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth/gap_config.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth/gatt_db.c
)

# LE Audio module sources
set(LE_AUDIO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio/le_audio_manager.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio/bap_unicast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio/bap_broadcast.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio/isoc_handler.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/le_audio/pacs.c
)

# MIDI module sources
set(MIDI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/midi/midi_ble_service.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/midi/midi_usb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/midi/midi_router.c
)

# Main application
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main.c
)

# Combine all sources
set(ALL_SOURCES
    ${APP_SOURCES}
    ${AUDIO_SOURCES}
    ${BLUETOOTH_SOURCES}
)

if(ENABLE_LE_AUDIO)
    list(APPEND ALL_SOURCES ${LE_AUDIO_SOURCES})
endif()

if(ENABLE_MIDI)
    list(APPEND ALL_SOURCES ${MIDI_SOURCES})
endif()

#-------------------------------------------------------------------------------
# Main Executable
#-------------------------------------------------------------------------------

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME}
    freertos
    lc3
    btstack
    $<$<BOOL:${ENABLE_USB}>:usbdev>
    m  # Math library
)

# Set linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/psoc_edge_e81.ld)
if(EXISTS ${LINKER_SCRIPT})
    target_link_options(${PROJECT_NAME} PRIVATE
        -T${LINKER_SCRIPT}
    )
endif()

#-------------------------------------------------------------------------------
# Post-Build Commands
#-------------------------------------------------------------------------------

# Generate binary file
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMENT "Generating binary file ${PROJECT_NAME}.bin"
)

# Generate hex file
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMENT "Generating hex file ${PROJECT_NAME}.hex"
)

# Print size information
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Memory usage:"
)

#-------------------------------------------------------------------------------
# Installation
#-------------------------------------------------------------------------------

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.hex
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
    DESTINATION firmware
)

#-------------------------------------------------------------------------------
# Custom Targets
#-------------------------------------------------------------------------------

# Flash target (using OpenOCD)
add_custom_target(flash
    COMMAND openocd -f interface/cmsis-dap.cfg -f target/psoc_edge.cfg
            -c "program ${PROJECT_NAME}.elf verify reset exit"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Flashing ${PROJECT_NAME} to target"
)

# Clean all
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build artifacts"
)

# Format source code
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i -style=file
                ${APP_SOURCES} ${AUDIO_SOURCES} ${BLUETOOTH_SOURCES}
                ${LE_AUDIO_SOURCES} ${MIDI_SOURCES}
        COMMENT "Formatting source code"
    )
endif()

#-------------------------------------------------------------------------------
# Print Configuration Summary
#-------------------------------------------------------------------------------

message(STATUS "")
message(STATUS "=== Infineon LE Audio Demo Configuration ===")
message(STATUS "")
message(STATUS "Target MCU:        ${MCU_FAMILY} (${MCU_CORE})")
message(STATUS "Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler:        ${CMAKE_C_COMPILER}")
message(STATUS "")
message(STATUS "Features:")
message(STATUS "  LE Audio:        ${ENABLE_LE_AUDIO}")
message(STATUS "  Auracast:        ${ENABLE_AURACAST}")
message(STATUS "  MIDI:            ${ENABLE_MIDI}")
message(STATUS "  USB:             ${ENABLE_USB}")
message(STATUS "  Debug:           ${ENABLE_DEBUG}")
message(STATUS "  Statistics:      ${ENABLE_STATS}")
message(STATUS "")
message(STATUS "Libraries:")
message(STATUS "  FreeRTOS:        ${FREERTOS_DIR}")
message(STATUS "  liblc3:          ${LC3_DIR}")
message(STATUS "  BTStack:         ${BTSTACK_DIR}")
if(ENABLE_USB)
    message(STATUS "  USB Device:      ${USBDEV_DIR}")
endif()
message(STATUS "")
message(STATUS "==============================================")
message(STATUS "")
