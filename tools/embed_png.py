#!/usr/bin/env python3
#
# embed_png.py — Convert a PNG file into a C header with a byte array.
#
# Usage: ./tools/embed_png.py assets/controls.png src/controls_png.h
#

import sys
import os

if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <input.png> <output.h>", file=sys.stderr)
    sys.exit(1)

input_path = sys.argv[1]
output_path = sys.argv[2]

data = open(input_path, "rb").read()

with open(output_path, "w") as out:
    out.write("/* Auto-generated from controls.png — do not edit */\n\n")
    out.write("static const unsigned char controls_png_data[] = {\n")
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        out.write("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",\n")
    out.write("};\n\n")
    out.write(f"static const unsigned int controls_png_size = {len(data)};\n")

print(f"{input_path} -> {output_path} ({len(data)} bytes)")
