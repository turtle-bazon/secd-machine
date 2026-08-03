#!/usr/bin/env python3
"""Convert SECD bytecode binary to C header for embedding in firmware."""

import sys
import os
import argparse

def convert(input_path, output_path, var_name="embedded_bytecode"):
    with open(input_path, "rb") as f:
        data = f.read()

    with open(output_path, "w") as f:
        f.write(f"/* Auto-generated from {os.path.basename(input_path)} — do not edit */\n")
        f.write(f"#ifndef SECD_EMBEDDED_BYTECODE_H\n")
        f.write(f"#define SECD_EMBEDDED_BYTECODE_H\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"static const uint8_t {var_name}[] = {{\n")

        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_vals},\n")

        f.write(f"}};\n\n")
        f.write(f"static const unsigned int {var_name}_size = {len(data)};\n\n")
        f.write(f"#endif /* SECD_EMBEDDED_BYTECODE_H */\n")

    print(f"Wrote {len(data)} bytes to {output_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert bytecode binary to C header")
    parser.add_argument("input", help="Input binary file")
    parser.add_argument("-o", "--output", help="Output header file (default: <input>.h)")
    parser.add_argument("-n", "--name", default="embedded_bytecode", help="Variable name (default: embedded_bytecode)")
    args = parser.parse_args()

    out = args.output or os.path.splitext(args.input)[0] + ".h"
    convert(args.input, out, args.name)
