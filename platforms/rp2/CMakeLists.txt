# SECD Machine for RP2040
# Auto-downloads pico-sdk if not found

cmake_minimum_required(VERSION 3.13)

# Set PICO_SDK_PATH if not set
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk")
endif()

# Download pico-sdk if not present
if(NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(STATUS "Downloading pico-sdk...")
    include(FetchContent)
    FetchContent_Declare(
        pico-sdk
        GIT_REPOSITORY https://github.com/raspberrypi/pico-sdk.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(pico-sdk)
    set(PICO_SDK_PATH "${pico-sdk_SOURCE_DIR}")
endif()

# Include pico-sdk
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(secd-machine-rp2040-pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Disable exceptions and RTTI for smaller binary
add_compile_options(-fno-exceptions -fno-rtti)

# Firmware variant:
#   ON  (debug)  - print machine info over serial on startup, wait for host.
#   OFF (release)- start bytecode immediately, no serial output, no waits.
option(SECD_DEBUG_BUILD "Debug firmware: serial info on startup with waits" ON)
if(SECD_DEBUG_BUILD)
    message(STATUS "SECD firmware: DEBUG (serial info + startup waits)")
else()
    message(STATUS "SECD firmware: RELEASE (no serial, start immediately)")
endif()

# Board features (peripherals linked into the firmware), comma-separated.
# The chip HAL offers capabilities; the board selects which are enabled here.
# E.g. a bare chip board builds with -DSECD_FEATURES= to omit GPIO/UART code.
set(SECD_FEATURES "gpio,uart,i2c" CACHE STRING
    "Board features to enable (comma-separated): gpio,uart,...")
message(STATUS "SECD features: ${SECD_FEATURES}")

# Byte-vector/bignum arena (factorial-class workloads need headroom).
set(SECD_BYTEVEC_ARENA_SIZE 196608 CACHE STRING "")
add_compile_definitions(SECD_BYTEVEC_ARENA_SIZE=${SECD_BYTEVEC_ARENA_SIZE})

# CMake lists use ';' separators; the cache value is comma-separated
string(REPLACE "," ";" _SECD_FEATURES "${SECD_FEATURES}")

# Initialize pico-sdk
pico_sdk_init()

# CherryUSB device stack (vendored). CDC-ACM console + HID keyboard.
set(SOURCES
    main.cpp
    usb.cpp
    ../../src/firmware/secd_boot.cpp
    ../../src/core/heap.cpp
    ../../src/core/gc.cpp
    ../../src/core/machine.cpp
    ../../src/core/bytecode.cpp
    ../../src/core/primitives.cpp
    ../../src/core/symbols.cpp
    ../../src/hal/rp2040.cpp
    ../../third_party/cherryusb/core/usbd_core.c
    ../../third_party/cherryusb/class/cdc/usbd_cdc_acm.c
    ../../third_party/cherryusb/class/hid/usbd_hid.c
    ../../third_party/cherryusb/port/rp2040/usb_dc_rp2040.c
)

# Create executable
add_executable(secd-machine ${SOURCES})
target_compile_definitions(secd-machine PRIVATE SECD_DEBUG_BUILD=$<BOOL:${SECD_DEBUG_BUILD}>)
# The vendored CherryUSB composite (CDC console + HID keyboard) is always
# linked into this firmware, so advertise the HID primitive.
target_compile_definitions(secd-machine PRIVATE SECD_FEATURE_HID=1)
# I2C master on I2C0 (pins chosen by the Lisp program at runtime).
target_compile_definitions(secd-machine PRIVATE SECD_FEATURE_I2C=1)

# Machine version + enabled features, surfaced to the debug serial banner.
# Keep SECD_MACHINE_VERSION in sync with secd-lisp/secd-lisp.asd :version.
set(SECD_MACHINE_VERSION "0.0.1.0" CACHE STRING "SECD Machine version (reported over serial in debug builds)")
target_compile_definitions(secd-machine PRIVATE SECD_MACHINE_VERSION="${SECD_MACHINE_VERSION}")
target_compile_definitions(secd-machine PRIVATE SECD_FEATURES_STR="${SECD_FEATURES}")
target_compile_definitions(secd-machine PRIVATE SECD_PLATFORM_NAME="RP2040-Pico")

# Enable per-feature defines and collect peripheral libraries
foreach(_f ${_SECD_FEATURES})
    string(TOUPPER "${_f}" _F)
    target_compile_definitions(secd-machine PRIVATE SECD_FEATURE_${_F}=1)
    if("${_f}" STREQUAL "gpio")
        list(APPEND SECD_FEATURE_LIBS hardware_gpio)
    elseif("${_f}" STREQUAL "uart")
        list(APPEND SECD_FEATURE_LIBS hardware_uart)
    elseif("${_f}" STREQUAL "i2c")
        list(APPEND SECD_FEATURE_LIBS hardware_i2c)
    endif()
endforeach()

# Include directories
target_include_directories(secd-machine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_BINARY_DIR}/cxx-shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../../third_party/cherryusb
    ../../third_party/cherryusb/common
    ../../third_party/cherryusb/core
    ../../third_party/cherryusb/port
    ../../third_party/cherryusb/class/cdc
    ../../third_party/cherryusb/class/hid
)

# Link libraries
target_link_libraries(secd-machine
    pico_stdlib
    ${SECD_FEATURE_LIBS}
    hardware_flash
    hardware_timer
    pico_time
)

# Skip C++ stdlib - our code is pure C, pico-sdk C++ only needs malloc/free from libc
# GCC 9.3.0 doesn't support -nostdlib++, use dummy libstdc++.a instead
target_link_directories(secd-machine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# USB is provided by the vendored CherryUSB stack (composite CDC console +
# HID keyboard); the pico-sdk stdio USB (TinyUSB) layer is NOT used.
pico_enable_stdio_uart(secd-machine 0)
pico_enable_stdio_usb(secd-machine 0)

# Create UF2 file
pico_add_uf2_output(secd-machine)
