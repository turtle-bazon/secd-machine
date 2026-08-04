#!/usr/bin/env python3
"""Wrap a raw firmware .bin into a UF2 file.

Usage: uf2.py <input.bin> <output.uf2> <base-addr> <family-id>

UF2 block layout (512 bytes): 32-byte header, 256-byte payload,
220 reserved bytes, 4-byte end magic. The family id lives at header
offset 28 (flags bit 13 must be set).
"""
import struct
import sys

MAGIC0 = 0x0A324655
MAGIC1 = 0x9E5D5157
END_MAGIC = 0x0AB16F30
FLAG_FAMILY = 0x2000


def main() -> None:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    _, in_path, out_path, base_str, family_str = sys.argv
    base = int(base_str, 0)
    family = int(family_str, 0)

    with open(in_path, "rb") as f:
        data = f.read()

    chunks = [data[i * 256:(i + 1) * 256] for i in range((len(data) + 255) // 256)]
    total = len(chunks)
    blocks = []
    for i, chunk in enumerate(chunks):
        header = struct.pack(
            "<IIIIIIII",
            MAGIC0, MAGIC1, FLAG_FAMILY, base + i * 256, len(chunk), i, total, family,
        )
        block = header + chunk.ljust(256, b"\xff") + bytes(220) + struct.pack("<I", END_MAGIC)
        blocks.append(block)

    with open(out_path, "wb") as f:
        f.write(b"".join(blocks))

    print(f"UF2: {total} blocks, {len(data)} bytes firmware -> {out_path}")


if __name__ == "__main__":
    main()
