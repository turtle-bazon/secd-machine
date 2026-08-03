# Building for RP2040

## Prerequisites

### 1. Install ARM Toolchain

**macOS:**
```bash
brew install gcc-arm-embedded
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi
```

**Windows:**
Download from: https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm

### 2. Install pico-sdk

```bash
cd ~/
git clone https://github.com/raspberrypi/pico-sdk.git
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=~/pico-sdk
```

### 3. Install picotool

```bash
cd ~/
git clone https://github.com/raspberrypi/picotool.git
cd picotool
mkdir build && cd build
cmake ..
make
sudo make install
```

## Building secd-machine for RP2040

### Option 1: Standalone (without pico-sdk)

For testing on PC first:

```bash
cd secd-machine
make
./run_tests
```

### Option 2: With pico-sdk (for real RP2040)

```bash
cd secd-machine

# Set pico-sdk path
export PICO_SDK_PATH=~/pico-sdk

# Create CMakeLists.txt for RP2040
# (See rp2040/ subdirectory)

mkdir build-rp2040
cd build-rp2040
cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/rp2040.cmake ..
make

# Output: secd-machine.uf2
```

## Flashing to RP2040

### Method 1: BOOTSEL button

1. Hold BOOTSEL button on RP2040
2. Connect USB
3. RP2040 appears as USB drive
4. Copy .uf2 file:
   ```bash
   cp secd-machine.uf2 /media/RPI-RP2/
   ```

### Method 2: picotool

```bash
picotool load secd-machine.uf2
picotool reboot
```

## Testing

After flashing:

1. Open serial terminal (115200 baud):
   ```bash
   screen /dev/ttyACM0 115200
   # or
   minicom -D /dev/ttyACM0 -b 115200
   ```

2. You should see:
   ```
   SECD Machine v0.1.0
   Ready.
   ```

3. LED should blink (if running blink example)
