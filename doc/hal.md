# Hardware Abstraction Layer (HAL)

## Overview

The HAL provides a uniform interface for accessing hardware-specific features across different microcontrollers. This allows the SECD machine to run on multiple platforms with minimal code changes.

## HAL Interface

```c
// hal.h
#ifndef SECD_HAL_H
#define SECD_HAL_H

#include <stdint.h>
#include <stddef.h>

// Memory management
void* hal_malloc(size_t size);
void  hal_free(void* ptr);
void* hal_realloc(void* ptr, size_t size);

// Timing
uint32_t hal_millis(void);
void     hal_delay(uint32_t ms);

// I/O
void hal_gpio_init(uint8_t pin, uint8_t mode);
void hal_gpio_write(uint8_t pin, uint8_t value);
uint8_t hal_gpio_read(uint8_t pin);

// Serial
void hal_serial_init(uint32_t baud);
void hal_serial_write(uint8_t byte);
uint8_t hal_serial_read(void);
int  hal_serial_available(void);

// Flash
uint32_t hal_flash_size(void);
int      hal_flash_read(uint32_t addr, uint8_t* buf, size_t len);
int      hal_flash_write(uint32_t addr, const uint8_t* buf, size_t len);

// System
void hal_reset(void);
void hal_enter_sleep(void);

#endif // SECD_HAL_H
```

## RP2040 Implementation

### Memory Map

```
FLASH: 2MB
├── Bootloader: 256KB
├── Application Code: 1MB
├── Bytecode Storage: 512KB
└── Reserved: 256KB

SRAM: 264KB
├── System Stack: 8KB
├── SECD Heap: 200KB
├── SECD Stack: 16KB
├── Symbol Table: 32KB
└── Free: 8KB
```

### GPIO Mapping

```c
// RP2040 GPIO assignments
#define GPIO_LED     25  // Built-in LED
#define GPIO_BUTTON  0   // User button
#define GPIO_TX      0   // UART TX
#define GPIO_RX      1   // UART RX

// I2C (for sensors)
#define GPIO_I2C_SDA 4
#define GPIO_I2C_SCL 5

// SPI (for displays)
#define GPIO_SPI_MOSI 19
#define GPIO_SPI_MISO 16
#define GPIO_SPI_SCK  18
#define GPIO_SPI_CS   17
```

### Clock Configuration

```c
// Default clock: 125MHz (from 12MHz crystal)
#define SYS_CLK_HZ   125000000
#define UART_CLK_HZ   125000000
#define SPI_CLK_HZ    10000000
#define I2C_CLK_HZ    400000
```

### Memory Allocation

```c
// RP2040 uses heap in SRAM
#define HEAP_START    0x20001000  // After system stack
#define HEAP_SIZE     200 * 1024  // 200KB

static uint8_t heap_memory[HEAP_SIZE];

void* hal_malloc(size_t size) {
    // Simple bump allocator for now
    static size_t offset = 0;
    if (offset + size > HEAP_SIZE) {
        return NULL;  // Out of memory
    }
    void* ptr = &heap_memory[offset];
    offset += size;
    return ptr;
}

void hal_free(void* ptr) {
    // No-op for bump allocator
    // Will implement proper GC later
}
```

### UART Configuration

```c
// UART0: 115200 baud, 8N1
#define UART_BAUD    115200
#define UART_DATA_BITS 8
#define UART_STOP_BITS 1
#define UART_PARITY    0

void hal_serial_init(uint32_t baud) {
    uart_init(uart0, baud);
    gpio_set_function(GPIO_TX, UART_FUNCSEL);
    gpio_set_function(GPIO_RX, UART_FUNCSEL);
}
```

### Flash Storage

```c
// Flash layout for bytecode storage
#define FLASH_BYTECODE_START  0x100000  // 1MB offset
#define FLASH_BYTECODE_SIZE   0x80000   // 512KB

int hal_flash_write(uint32_t addr, const uint8_t* buf, size_t len) {
    // RP2040 flash requires sector erase (4KB) before write
    // Implementation uses pico-sdk flash functions
    return flash_range_program(addr, buf, len);
}
```

## ESP32 Implementation (Future)

### Memory Map

```
FLASH: 4MB
├── Bootloader: 64KB
├── Application Code: 1.5MB
├── Bytecode Storage: 1MB
├── NVS: 24KB
└── Reserved: Rest

SRAM: 520KB
├── System Stack: 16KB
├── SECD Heap: 400KB
├── SECD Stack: 32KB
├── Symbol Table: 64KB
└── Free: 8KB
```

### GPIO Mapping

```c
// ESP32 GPIO assignments
#define GPIO_LED     2   // Built-in LED
#define GPIO_BUTTON  0   // User button
#define GPIO_TX      1   // UART TX
#define GPIO_RX      3   // UART RX

// I2C (for sensors)
#define GPIO_I2C_SDA 21
#define GPIO_I2C_SCL 22

// SPI (for displays)
#define GPIO_SPI_MOSI 23
#define GPIO_SPI_MISO 19
#define GPIO_SPI_SCK  18
#define GPIO_SPI_CS   5
```

### WiFi (Future Feature)

```c
// WiFi configuration for OTA updates
#define WIFI_SSID     "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"
#define OTA_SERVER    "http://your-server.com/firmware.bin"
```

## Cross-Compilation

### CMake Toolchain Files

#### RP2040 (arm-none-eabi-gcc)

```cmake
# cmake/rp2040.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR cortex-m0)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-m0 -mthumb -Os -ffunction-sections -fdata-sections")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--gc-sections -specs=nosys.specs")

# Pico SDK path
set(PICO_SDK_PATH "/path/to/pico-sdk")
```

#### ESP32 (xtensa-esp32-elf-gcc)

```cmake
# cmake/esp32.cmake
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa)

set(CMAKE_C_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_ASM_COMPILER xtensa-esp32-elf-gcc)
set(CMAKE_C_FLAGS_INIT "-mlongcalls -mtext-section-literals -Os")

# ESP-IDF path
set(IDF_PATH "/path/to/esp-idf")
```

### Build Commands

#### RP2040

```bash
# Install toolchain
brew install gcc-arm-embedded  # macOS
sudo apt install gcc-arm-none-eabi  # Linux

# Build
mkdir build-rp2040
cd build-rp2040
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/rp2040.cmake ..
make

# Flash (using picotool)
picotool load secd-machine.uf2
```

#### ESP32

```bash
# Install ESP-IDF
git clone https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh
source export.sh

# Build
mkdir build-esp32
cd build-esp32
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/esp32.cmake ..
make

# Flash
esptool.py write_flash 0 secd-machine.bin
```

## Platform-Specific Features

### RP2040

- **PIO**: Programmable I/O for custom protocols
- **DMA**: Direct memory access for fast transfers
- **Dual Core**: Can run SECD on one core, I/O on other
- **USB**: Native USB for programming/debugging

### ESP32

- **WiFi**: Wireless connectivity for OTA updates
- **Bluetooth**: BLE for configuration
- **Touch**: Capacitive touch sensing
- **ADC**: Analog-to-digital conversion

## Testing

### Host Testing (Native)

```bash
# Build for host (x86/x64)
mkdir build-host
cd build-host
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Run tests
./tests/test_secd
```

### Hardware Testing

```bash
# Build for RP2040
cd build-rp2040
make

# Flash and monitor
picotool load secd-machine.uf2
picotool reboot -a

# Monitor serial output
screen /dev/tty.usbmodem* 115200
```

## Debugging

### Serial Debug Output

```c
#define DEBUG_PRINTF(fmt, ...) \
    hal_serial_printf("[SECD] " fmt "\n", ##__VA_ARGS__)

// Usage
DEBUG_PRINTF("Heap: %d/%d bytes used", heap_used, heap_size);
DEBUG_PRINTF("Instruction: 0x%02X", instruction);
```

### LED Indicators

```c
// Status LED patterns
#define LED_IDLE       0x00  // Off
#define LED_RUNNING    0x01  // Solid on
#define LED_GC         0x02  // Fast blink
#define LED_ERROR      0x04  // Slow blink
#define LED_RECEIVED   0x08  // Single blink
```

### Core Dump Analysis

```c
// Save machine state to flash on crash
void hal_save_core_dump(secd_machine_t* machine) {
    // Save registers, stack, heap to flash
    // Can be analyzed with debugger
}
```