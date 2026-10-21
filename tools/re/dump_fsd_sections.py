#!/usr/bin/env python3
"""
Inspect all 35 sections inside any DMC1 .fsd room archive.
Usage:
    python dump_fsd_sections.py [room_name]
Example:
    python dump_fsd_sections.py r10c
    python dump_fsd_sections.py r121
"""

import struct
import sys
import zipfile
from dmc1_env import get_nbz_path

def inspect_fsd(room_name: str):
    if not room_name.startswith("r") and not room_name.startswith("Fsd/"):
        room_name = f"r{room_name}"
    if not room_name.endswith(".fsd"):
        room_name = f"{room_name}.fsd"
    if not room_name.startswith("Fsd/"):
        room_name = f"Fsd/{room_name}"

    nbz_path = get_nbz_path()
    zf = zipfile.ZipFile(nbz_path)

    if room_name not in zf.namelist():
        print(f"Error: '{room_name}' not found in {nbz_path.name}")
        return

    data = zf.read(room_name)
    num_entries = struct.unpack('<I', data[:4])[0]
    offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]

    print(f"=== {room_name}: {num_entries} Sections (Total {len(data)} Bytes) ===\n")
    print(f"{'Section':<10} {'Offset':<12} {'Size (Bytes)':<14} {'Magic / Header':<20}")
    print("-" * 56)

    for i in range(num_entries):
        off = offsets[i]
        if off == 0:
            print(f"Sec {i:2d}:    0x000000     0              (empty)")
            continue

        next_off = len(data)
        for no in sorted(offsets):
            if no > off:
                next_off = no
                break
        size = next_off - off
        magic = data[off:off+8]
        magic_hex = magic.hex()
        magic_ascii = "".join(chr(b) if 32 <= b < 127 else "." for b in magic)
        print(f"Sec {i:2d}:    0x{off:06X}   {size:<14} {magic_hex:<18} [{magic_ascii}]")

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "r10c"
    inspect_fsd(target)
