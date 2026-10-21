#!/usr/bin/env python3
"""
Scan all DMC1 .fsd room archives in data/dmc1/dmc1-0.nbz for player floor collision and navmesh.
In Capcom's DMC1 engine:
- Section 20 = Navmesh
- Section 21 = Player Floor Collision Mesh (BSP/poly tree)
- Section 22 = Room boundary / camera triggers
Rooms with 0 bytes in Section 21 will cause Dante to fall out of bounds upon movement!
"""

import struct
import zipfile
from dmc1_env import get_nbz_path

def main():
    nbz_path = get_nbz_path()
    zf = zipfile.ZipFile(nbz_path)

    fsd_files = [f for f in zf.namelist() if f.startswith("Fsd/") and f.endswith(".fsd")]
    print(f"=== Scanning {len(fsd_files)} .fsd Room Files in {nbz_path.name} ===\n")
    print(f"{'Room File':<20} {'Size (Bytes)':<14} {'Sec 20 (Navmesh)':<18} {'Sec 21 (Collision)':<18} {'Status':<12}")
    print("-" * 82)

    has_collision_count = 0
    no_collision_count = 0

    for name in sorted(fsd_files):
        data = zf.read(name)
        num_entries = struct.unpack('<I', data[:4])[0]
        offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]

        s20 = offsets[20] if len(offsets) > 20 else 0
        s21 = offsets[21] if len(offsets) > 21 else 0

        # Calculate section 21 size if present
        s21_size = 0
        if s21 > 0:
            next_off = len(data)
            for no in sorted(offsets):
                if no > s21:
                    next_off = no
                    break
            s21_size = next_off - s21

        s20_str = hex(s20) if s20 else "0"
        s21_str = f"{hex(s21)} ({s21_size}B)" if s21 else "0"
        status = "Walkable" if s21 > 0 else "NO COLLISION"

        if s21 > 0:
            has_collision_count += 1
        else:
            no_collision_count += 1

        print(f"{name:<20} {len(data):<14} {s20_str:<18} {s21_str:<18} {status:<12}")

    print("-" * 82)
    print(f"Total Rooms: {len(fsd_files)} | Walkable (Solid): {has_collision_count} | No Collision (Cutscenes): {no_collision_count}")

if __name__ == "__main__":
    main()
