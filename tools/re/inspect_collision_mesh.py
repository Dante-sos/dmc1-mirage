#!/usr/bin/env python3
"""
Inspect Section 21 (Player Floor Collision Mesh) of a DMC1 .fsd room archive.
Extracts collision vertices, coordinates bounding box, and ground height (Y).
Usage:
    python inspect_collision_mesh.py [room_name]
Example:
    python inspect_collision_mesh.py r10c
    python inspect_collision_mesh.py r11d
"""

import struct
import sys
import zipfile
from dmc1_env import get_nbz_path

def inspect_collision(room_name: str):
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

    s21_off = offsets[21] if len(offsets) > 21 else 0
    if s21_off == 0:
        print(f"=== {room_name}: NO Section 21 collision mesh (Size 0) ===")
        print("Dante will fall through the floor upon moving in this room!")
        return

    next_off = len(data)
    for no in sorted(offsets):
        if no > s21_off:
            next_off = no
            break
    s21_size = next_off - s21_off

    print(f"=== {room_name} Section 21 Collision Mesh (Offset 0x{s21_off:X}, Size {s21_size} Bytes) ===\n")

    # Header inspection
    u32s = struct.unpack_from("<8I", data, s21_off)
    print(f"Header: {data[s21_off:s21_off+32].hex()}")
    print(f"UInt32s: {u32s}")

    # Extract valid float vertices
    points = []
    for i in range(0, s21_size - 12, 4):
        x, y, z = struct.unpack_from("<3f", data, s21_off + i)
        if -50000 < x < 50000 and -10000 < y < 20000 and -50000 < z < 50000:
            if abs(x) > 1 or abs(z) > 1:
                points.append((x, y, z))

    print(f"\nFound {len(points)} candidate collision vertices:")
    if points:
        xs = [p[0] for p in points]
        ys = [p[1] for p in points]
        zs = [p[2] for p in points]
        print(f"  X Range: [{min(xs):.1f}, {max(xs):.1f}] (Center: {(min(xs)+max(xs))/2:.1f})")
        print(f"  Y Range: [{min(ys):.1f}, {max(ys):.1f}]")
        print(f"  Z Range: [{min(zs):.1f}, {max(zs):.1f}] (Center: {(min(zs)+max(zs))/2:.1f})")

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "r10c"
    inspect_collision(target)
