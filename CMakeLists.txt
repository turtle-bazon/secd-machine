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

project(secd-machine-rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Disable exceptions and RTTI for smaller binary
add_compile_options(-fno-exceptions -fno-rtti)

# Initialize pico-sdk
pico_sdk_init()

# Source files
set(SOURCES
    src/rp2040/main.cpp
    src/core/heap.cpp
    src/core/gc.cpp
    src/core/machine.cpp
    src/core/bytecode.cpp
    src/core/primitives.cpp
    src/core/symbols.cpp
    src/hal/rp2040.cpp
)

# Create executable
add_executable(secd-machine ${SOURCES})

# Include directories
target_include_directories(secd-machine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/build-rp2040/cxx-shim
)

# Link libraries
target_link_libraries(secd-machine
    pico_stdlib
    hardware_uart
    hardware_gpio
    hardware_flash
    hardware_timer
    pico_time
)

# Skip C++ stdlib - our code is pure C, pico-sdk C++ only needs malloc/free from libc
# GCC 9.3.0 doesn't support -nostdlib++, use dummy libstdc++.a instead
target_link_directories(secd-machine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# Enable USB output, disable UART output
pico_enable_stdio_usb(secd-machine 1)
pico_enable_stdio_uart(secd-machine 0)

# Create UF2 file
pico_add_uf2_output(secd-machine)
