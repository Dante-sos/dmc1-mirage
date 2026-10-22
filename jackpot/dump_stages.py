#!/usr/bin/env python3
"""
Dump DMC1 Stage Tables (Stage 0 to Stage 5).
Maps every room index within a stage to its corresponding .fsd file archive.
"""

import struct
from dmc1_env import DmcExe

def get_file_name(exe: DmcExe, file_idx: int) -> str:
    if file_idx == 0:
        return "(none)"
    off = 0x4D8000 + file_idx * 16
    if 0 <= off < len(exe.data):
        return exe.data[off:off+16].split(b'\x00')[0].decode('ascii', errors='ignore')
    return f"unknown_{file_idx}"

def main():
    exe = DmcExe()
    stage_ptrs = [
        (0, 0x5A4488),
        (1, 0x5A4590),
        (2, 0x5A44E8),
        (3, 0x5A4530),
        (4, 0x5A44C8),
        (5, 0x5A44A8),
    ]

    print("=== DMC1 Stage Room-to-File Tables ===\n")
    for stg_id, stg_rva in stage_ptrs:
        fo = exe.rva_to_offset(stg_rva)
        print(f"--- STAGE {stg_id} (Table RVA 0x{stg_rva:X}) ---")
        for room_id in range(35):
            val = struct.unpack_from("<H", exe.data, fo + room_id * 2)[0]
            fname = get_file_name(exe, val)
            if val != 0:
                print(f"  Room {room_id:2d}: FileID {val:3d} -> {fname}")
        print()

if __name__ == "__main__":
    main()
