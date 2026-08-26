#!/usr/bin/env bash
#
# Flash the SECD Machine nRF52840 firmware via a Black Magic Probe.
#
# The firmware .elf already carries the correct load address (0x26000, the
# Adafruit-nRF52-bootloader app slot), so `load` writes ONLY the app region.
# We never mass-erase -- that would wipe the bootloader + SoftDevice.
#
# Usage:
#   tools/flash_nrf52840_bmp.sh [ELF] [BMP_GDB_PORT]
#
# Defaults: ELF=/tmp/firmware.elf  BMP_GDB_PORT=/dev/ttyBmpGdb

set -euo pipefail

ELF="${1:-/tmp/firmware.elf}"
BMP="${2:-/dev/ttyBmpGdb}"

if [ ! -f "$ELF" ]; then
    echo "error: firmware not found: $ELF" >&2
    echo "build it with: make build/nrf52840/firmware.uf2" >&2
    exit 1
fi

if [ ! -c "$BMP" ]; then
    echo "error: BMP GDB port not found: $BMP" >&2
    echo "check 'ls /dev/ttyBmp*' or pass the port as \$2" >&2
    exit 1
fi

if ! command -v arm-none-eabi-gdb >/dev/null 2>&1; then
    echo "error: arm-none-eabi-gdb not found in PATH" >&2
    exit 1
fi

echo ">> flashing $ELF via $BMP"
arm-none-eabi-gdb -batch \
    -ex "set confirm off" \
    -ex "target extended-remote $BMP" \
    -ex "monitor swdp_scan" \
    -ex "attach 1" \
    -ex "load" \
    -ex "monitor reset" \
    -ex "detach" \
    -ex "quit" \
    "$ELF"

echo ">> done. board reset; bootloader should jump to the app (USB: 'SECD Machine')."
