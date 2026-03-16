################################################################################
# \file Makefile
# \version 1.0
#
# \brief
# Makefile for configuring the Cortex-M33 core project
#
################################################################################
# \copyright
# (c) 2023-2025, Infineon Technologies AG, or an affiliate of Infineon Technologies AG.
# SPDX-License-Identifier: Apache-2.0
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

include ../common.mk

################################################################################
# Basic Configuration
################################################################################

# Name of application (used to derive name of final linked file).
#
# If APPNAME is edited, ensure to update or regenerate launch
# configurations for your IDE.
APPNAME=proj_cm33_ns

# If set to "true" or "1", display full command-lines when building.
VERBOSE=

# Set BT feature baudrate to 3M 
DEFINES+=CYBSP_BT_PLATFORM_CFG_BAUD_FEATURE=3000000 

################################################################################
# Advanced Configuration
################################################################################

# Enable optional code that is ordinarily disabled by default.
#
# Available components depend on the specific targeted hardware and firmware
# in use. In general, if you have
#
#    COMPONENTS=foo bar
#
# ... then code in directories named COMPONENT_foo and COMPONENT_bar will be
# added to the build
#
COMPONENTS+=FREERTOS RTOS_AWARE WICED_BLE WIFI6 WIFI_INTERFACE_SDIO USBD_BASE 

# Like COMPONENTS, but disable optional code that was enabled by default.
DISABLE_COMPONENTS=

CORE=CM33
CORE_NAME=CM33_0

# By default the build system automatically looks in the Makefile's directory
# tree for source code and builds it. The SOURCES variable can be used to
# manually add source code to the build process from a location not searched
# by default, or otherwise not found by the build system.
#
# Architecture:
#   CM33: BT stack, USB, Wi-Fi, MIDI control (this project)
#   CM55: LC3 codec, I2S audio DSP (proj_cm55)
#
# Exclude the original example main.c (renamed to main_example.c)
CY_IGNORE+=source/main_example.c

# CM33 Control-Plane Sources
#
# NOTE: Audio DSP sources (LC3, I2S) are built in proj_cm55, not here.
#
# Bluetooth Sources:
#   - source/bluetooth/ files have been refactored to integrate MTB patterns:
#     - bt_init.c: BT Configurator integration, bonding support
#     - gatt_db.c: Lookup table pattern, link.c integration
#     - link.c: Connection state tracking (adapted from app_bt/link.c)
#   - We use our refactored source/bluetooth files instead of app_bt/
#
# EXCLUDED FILES (audio DSP - built on CM55):
#   - source/audio/audio_buffers.c
#   - source/audio/audio_task.c
#   - source/audio/i2s_stream.c
#   - source/audio/lc3_wrapper.c
#
SOURCES+= \
    source/app_le_audio.c \
    ../../../source/bluetooth/bt_init.c \
    ../../../source/bluetooth/bt_platform_config.c \
    ../../../source/bluetooth/gatt_db.c \
    ../../../source/bluetooth/gap_config.c \
    ../../../source/bluetooth/link.c \
    ../../../source/bluetooth/hci_isoc.c \
    ../../../source/le_audio/bap_broadcast.c \
    ../../../source/le_audio/bap_unicast.c \
    ../../../source/le_audio/isoc_handler.c \
    ../../../source/le_audio/le_audio_manager.c \
    ../../../source/le_audio/pacs.c \
    ../../../source/midi/midi_ble_service.c \
    ../../../source/midi/midi_router.c \
    ../../../source/midi/midi_usb.c \
    ../../../source/wifi/wifi_bridge.c \
    ../../../source/wifi/whd_buffer_impl.c \
    ../../../source/ipc/audio_ipc.c \
    ../../../libs/wifi-host-driver/External/hal/cyhal_sdio.c

# Exclude app_bt files since we're using our refactored source/bluetooth/ files
CY_IGNORE+= \
    source/app_bt/bt.c \
    source/app_bt/gatt.c \
    source/app_bt/link.c

# Like SOURCES, but for include directories. Value should be paths to
# directories (without a leading -I).
#
# Custom LE Audio include directories
# NOTE: "." must be first to ensure project's FreeRTOSConfig.h is found
#       before the template in mtb_shared/freertos/COMPONENT_CM33
INCLUDES+= \
    . \
    ../../../source \
    ../../../source/audio \
    ../../../source/bluetooth \
    ../../../source/le_audio \
    ../../../source/midi \
    ../../../source/wifi \
    ../../../source/ipc \
    ../../../config \
    ../../../libs/liblc3/include \
    ../../../libs/wifi-host-driver/WHD/COMPONENT_WIFI6/inc \
    ../../../libs/wifi-host-driver/WHD/COMPONENT_WIFI6/src/include \
    ../../../libs/wifi-host-driver/External/hal \
    ../../../libs/emusb-device/USBD \
    ../../../libs/emusb-device/USBD/COMPONENT_USBD_BASE/COMPONENT_PSE84/CONFIG_Debug

# Add additional defines to the build process (without a leading -D).
DEFINES+=CY_RETARGET_IO_CONVERT_LF_TO_CRLF

# Select softfp or hardfp floating point. Default is softfp.
VFP_SELECT+=

# Additional / custom C compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CFLAGS+=

# Additional / custom C++ compiler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
CXXFLAGS+=

# Additional / custom assembler flags.
#
# NOTE: Includes and defines should use the INCLUDES and DEFINES variable
# above.
ASFLAGS+=

# Additional / custom linker flags.
LDFLAGS+=

# Additional / custom libraries to link in to the application.
LDLIBS+=

# Path to the linker script to use (if empty, use the default linker script).
LINKER_SCRIPT=

# Custom pre-build commands to run.
PREBUILD=

# Custom post-build commands to run.
POSTBUILD=

################################################################################
# Paths
################################################################################

# Relative path to the project directory (default is the Makefile's directory).
#
# This controls where automatic source code discovery looks for code.
CY_APP_PATH=

# Relative path to the shared repo location.
#
# All .mtb files have the format, <URI>#<COMMIT>#<LOCATION>. If the <LOCATION> 
# field begins with $$ASSET_REPO$$, then the repo is deposited in the path 
# specified by the CY_GETLIBS_SHARED_PATH variable. The default location is one 
# directory level above the current app directory.
# This is used with CY_GETLIBS_SHARED_NAME variable, which specifies the 
# directory name.
CY_GETLIBS_SHARED_PATH=../..

# Directory name of the shared repo location.
#
CY_GETLIBS_SHARED_NAME=mtb_shared

include $(CY_TOOLS_DIR)/make/start.mk
