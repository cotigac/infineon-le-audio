#-------------------------------------------------------------------------------
# CMakeLists.txt - PSoC Edge E82 LE Audio Demo
#
# Build system for the Infineon PSoC Edge E82 + CYW55512 LE Audio project.
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
# SPDX-License-Identifier: Apache-2.0
#-------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)

#-------------------------------------------------------------------------------
# Project Definition
#-------------------------------------------------------------------------------

project(infineon_le_audio
    VERSION 1.0.0
    DESCRIPTION "PSoC Edge E82 + CYW55512 LE Audio Musical Instrument Demo"
    LANGUAGES C ASM
)

#-------------------------------------------------------------------------------
# Build Options
#-------------------------------------------------------------------------------

option(ENABLE_LE_AUDIO "Enable LE Audio (BAP, PACS, ASCS)" ON)
option(ENABLE_AURACAST "Enable Auracast Broadcast (BIS)" ON)
option(ENABLE_MIDI "Enable MIDI over BLE and USB" ON)
option(ENABLE_USB "Enable USB Device support" ON)
option(ENABLE_CDC "Enable USB CDC/ACM AT command interface" ON)
option(ENABLE_WIFI "Enable Wi-Fi data bridge" ON)
option(ENABLE_DEBUG "Enable debug output" ON)
option(ENABLE_STATS "Enable runtime statistics" ON)
option(BUILD_TESTS "Build unit tests" OFF)

#-------------------------------------------------------------------------------
# Target Configuration
#-------------------------------------------------------------------------------

# PSoC Edge E82 - Cortex-M55
set(MCU_FAMILY "PSE82" CACHE STRING "MCU family")
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
    CY_MCU_PSE82
    __CORTEX_M55
    ARM_MATH_CM55

    # Features
    $<$<BOOL:${ENABLE_LE_AUDIO}>:ENABLE_LE_AUDIO=1>
    $<$<BOOL:${ENABLE_AURACAST}>:ENABLE_AURACAST=1>
    $<$<BOOL:${ENABLE_MIDI}>:ENABLE_MIDI=1>
    $<$<BOOL:${ENABLE_USB}>:ENABLE_USB=1>
    $<$<BOOL:${ENABLE_CDC}>:ENABLE_CDC=1>
    $<$<BOOL:${ENABLE_WIFI}>:ENABLE_WIFI=1>
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
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc
    ${CMAKE_CURRENT_SOURCE_DIR}/source/usb
    ${CMAKE_CURRENT_SOURCE_DIR}/source/wifi
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
# BTStack Library (Infineon AIROC BTSTACK)
#-------------------------------------------------------------------------------

set(BTSTACK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/btstack)
set(BTSTACK_INTEGRATION_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/btstack-integration)

# BTStack include directories (Infineon pre-built library headers)
set(BTSTACK_INCLUDE_DIRS
    ${BTSTACK_DIR}/wiced_include
    ${BTSTACK_DIR}/platform/include
)

# Pre-built BTStack library path (CM55 for PSoC Edge)
# Note: The actual library path depends on the BSP and toolchain
# For now, we create an interface library until the proper BSP is integrated
set(BTSTACK_LIB_PATH ${BTSTACK_DIR}/stack/COMPONENT_WICED_BLE/COMPONENT_CM55/TOOLCHAIN_GCC_ARM/libwiced_ble.a)

if(EXISTS ${BTSTACK_LIB_PATH})
    add_library(btstack STATIC IMPORTED)
    set_target_properties(btstack PROPERTIES
        IMPORTED_LOCATION ${BTSTACK_LIB_PATH}
    )
    message(STATUS "Found BTStack library: ${BTSTACK_LIB_PATH}")
else()
    # Create interface library as placeholder until BSP integration
    add_library(btstack INTERFACE)
    message(STATUS "BTStack: Using interface library (pre-built lib not found)")
endif()

# BTStack include interface
add_library(btstack-includes INTERFACE)
target_include_directories(btstack-includes INTERFACE ${BTSTACK_INCLUDE_DIRS})

#-------------------------------------------------------------------------------
# BTStack Integration (HCI-UART Porting Layer for PSoC Edge + CYW55512)
#-------------------------------------------------------------------------------

# btstack-integration provides the platform adaptation layer
set(BTSTACK_INTEGRATION_INCLUDE_DIRS
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/include
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/freertos
)

# HCI-UART porting layer sources
set(BTSTACK_INTEGRATION_SOURCES
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_hci_rx_task.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_hci_tx_task.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_host_stack_platform_interface.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_patchram_download.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_platform_main.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_platform_task.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_platform_trace.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_prm.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/common/cybt_serialize.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/freertos/cybt_platform_freertos.c
    ${BTSTACK_INTEGRATION_DIR}/COMPONENT_HCI-UART/platform_hal_wrappers/platform_hal_wrapper.c
)

add_library(btstack-integration STATIC ${BTSTACK_INTEGRATION_SOURCES})
target_include_directories(btstack-integration PUBLIC ${BTSTACK_INTEGRATION_INCLUDE_DIRS})
target_include_directories(btstack-integration PUBLIC ${BTSTACK_INCLUDE_DIRS})
target_link_libraries(btstack-integration PUBLIC freertos)
target_compile_definitions(btstack-integration PUBLIC
    CY_USING_HAL
    COMPONENT_HCI_UART
    CYBT_PLATFORM_TRACE_ENABLE=1
)

#-------------------------------------------------------------------------------
# USB Device Library (Segger emUSB-Device for PSoC Edge - USB High-Speed)
#-------------------------------------------------------------------------------

if(ENABLE_USB)
    set(EMUSB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/emusb-device)
    set(EMUSB_USBD_DIR ${EMUSB_DIR}/USBD)

    # Pre-built library path for CM55 + GCC (PSoC Edge E82/E84)
    set(EMUSB_LIB_PATH ${EMUSB_USBD_DIR}/COMPONENT_USBD_BASE/COMPONENT_PSE84/CONFIG_Debug/COMPONENT_CM55/COMPONENT_HARDFP/TOOLCHAIN_GCC_ARM/gcc.a)


    if(EXISTS ${EMUSB_LIB_PATH})
        # Create imported library for pre-built emUSB-Device
        add_library(emusb-device STATIC IMPORTED)
        set_target_properties(emusb-device PROPERTIES
            IMPORTED_LOCATION ${EMUSB_LIB_PATH}
        )

        # Create interface library for includes
        add_library(emusb-device-includes INTERFACE)
        target_include_directories(emusb-device-includes INTERFACE
            ${EMUSB_USBD_DIR}
            ${EMUSB_DIR}/OS
        )

        message(STATUS "Found emUSB-Device library (USB High-Speed): ${EMUSB_LIB_PATH}")
    else()
        message(WARNING "emUSB-Device library not found at: ${EMUSB_LIB_PATH}")
        # Create interface library as placeholder
        add_library(emusb-device INTERFACE)
        add_library(emusb-device-includes INTERFACE)
        target_include_directories(emusb-device-includes INTERFACE ${EMUSB_USBD_DIR})
    endif()
endif()

#-------------------------------------------------------------------------------
# Wi-Fi Host Driver Library (WHD for CYW55512)
#-------------------------------------------------------------------------------

if(ENABLE_WIFI)
    set(WHD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/wifi-host-driver)

    # WHD include directories
    set(WHD_INCLUDE_DIRS
        ${WHD_DIR}/WiFi_Host_Driver/inc
        ${WHD_DIR}/WiFi_Host_Driver/src
        ${WHD_DIR}/WiFi_Host_Driver/src/include
        ${WHD_DIR}/WiFi_Host_Driver/resources/firmware/COMPONENT_55500
    )

    # WHD core sources (minimal set for SDIO transport)
    file(GLOB WHD_CORE_SOURCES
        "${WHD_DIR}/WiFi_Host_Driver/src/*.c"
        "${WHD_DIR}/WiFi_Host_Driver/src/bus_protocols/whd_bus_sdio_protocol.c"
    )

    if(EXISTS "${WHD_DIR}/WiFi_Host_Driver/inc/whd.h")
        add_library(whd STATIC ${WHD_CORE_SOURCES})
        target_include_directories(whd PUBLIC ${WHD_INCLUDE_DIRS})
        target_compile_definitions(whd PUBLIC
            WHD_SDIO_BUS
            CYBSP_WIFI_CAPABLE
        )
        target_link_libraries(whd PUBLIC freertos)
        message(STATUS "Found Wi-Fi Host Driver (WHD): ${WHD_DIR}")
    else()
        message(WARNING "Wi-Fi Host Driver not found at: ${WHD_DIR}")
        # Create interface library as placeholder
        add_library(whd INTERFACE)
        target_include_directories(whd INTERFACE ${WHD_INCLUDE_DIRS})
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
    ${CMAKE_CURRENT_SOURCE_DIR}/source/bluetooth/bt_platform_config.c
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

# Wi-Fi module sources
# Note: SDIO communication uses cyhal_sdio via WHD library, not custom driver
set(WIFI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/wifi/wifi_bridge.c
)

# USB composite device sources
set(USB_COMPOSITE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/usb/usb_composite.c
)

# CDC/ACM AT command interface sources
set(CDC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/cdc_acm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/at_parser.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/at_system_cmds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/at_bt_cmds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/at_leaudio_cmds.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/cdc/at_wifi_cmds.c
)

# Main application
set(APP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/source/main_non_mtb.c
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

if(ENABLE_WIFI)
    list(APPEND ALL_SOURCES ${WIFI_SOURCES})
endif()

if(ENABLE_USB)
    list(APPEND ALL_SOURCES ${USB_COMPOSITE_SOURCES})
endif()

if(ENABLE_CDC)
    list(APPEND ALL_SOURCES ${CDC_SOURCES})
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
    btstack-includes
    btstack-integration
    $<$<BOOL:${ENABLE_USB}>:emusb-device>
    $<$<BOOL:${ENABLE_USB}>:emusb-device-includes>
    $<$<BOOL:${ENABLE_WIFI}>:whd>
    m  # Math library
)

# Set linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/cmake/psoc_edge_e82.ld)
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
message(STATUS "  CDC/ACM:         ${ENABLE_CDC}")
message(STATUS "  Wi-Fi:           ${ENABLE_WIFI}")
message(STATUS "  Debug:           ${ENABLE_DEBUG}")
message(STATUS "  Statistics:      ${ENABLE_STATS}")
message(STATUS "")
message(STATUS "Libraries:")
message(STATUS "  FreeRTOS:            ${FREERTOS_DIR}")
message(STATUS "  liblc3:              ${LC3_DIR}")
message(STATUS "  BTStack:             ${BTSTACK_DIR}")
message(STATUS "  BTStack-Integration: ${BTSTACK_INTEGRATION_DIR} (HCI-UART)")
if(ENABLE_USB)
    message(STATUS "  emUSB-Device:        ${EMUSB_DIR} (USB High-Speed)")
endif()
if(ENABLE_WIFI)
    message(STATUS "  WHD:                 ${WHD_DIR} (SDIO)")
endif()
message(STATUS "")
message(STATUS "==============================================")
message(STATUS "")
