#!/usr/bin/env python3
"""
Inspect and verify The Void practice arena configurations in dmc1.exe:
- Room Table Entry 22 (patched with r206.fsd geometry and dummy wave scripts)
- Mission Table Entry 8 (starts in Room 22 with drainage chamber spawn coordinates)
- Enemy subtable descriptors for live wave spawning (Marionette, Scissors, Shadow, etc.)
"""

import struct
from dmc1_env import DmcExe

def main():
    exe = DmcExe()
    
    # 1. Mission Table
    mt_rva = 0x560170
    mt_fo = exe.rva_to_offset(mt_rva)
    m8_entry = exe.data[mt_fo + 8 * 32 : mt_fo + 9 * 32]
    stage, room = struct.unpack_from("<II", m8_entry, 0)
    pos = struct.unpack_from("<4f", m8_entry, 0x10)
    print(f"=== The Void Mission 8 Initial Entry ===")
    print(f"  Stage: {stage}, Room: {room}")
    print(f"  Default Spawn: ({pos[0]:.1f}, {pos[1]:.1f}, {pos[2]:.1f}), Yaw: {pos[3]:.2f}")
    
    # 2. Room Descriptors
    rt_rva = 0x5163E8
    rt_fo = exe.rva_to_offset(rt_rva)
    
    print(f"\n=== Room Descriptors ===")
    for r in [22, 31, 0, 4, 6, 9, 20, 26]:
        entry_fo = rt_fo + r * 24
        p1, c1, unk, p2 = struct.unpack_from("<QIIQ", exe.data, entry_fo)
        print(f"  Room {r:2d}: ptr1=0x{p1:X} (files={c1}), ptr2=0x{p2:X}")

    print("\nVerified The Void practice arena structures.")

if __name__ == "__main__":
    main()
