#!/usr/bin/env python3
"""
Dump DMC1 Mission Table at RVA 0x560170.
Extracts mission index, initial stage, initial room, extra flags, and spawn XYZ/Facing coordinates.
"""

import struct
from dmc1_env import DmcExe

def main():
    exe = DmcExe()
    mt_rva = 0x560170
    mt_fo = exe.rva_to_offset(mt_rva)
    if mt_fo is None:
        raise ValueError(f"Could not resolve RVA 0x{mt_rva:X}")

    print(f"=== DMC1 Mission Table (RVA 0x{mt_rva:X}, FileOffset 0x{mt_fo:X}) ===\n")
    print(f"{'Mission':<8} {'Stage':<6} {'Room':<6} {'Facing':<8} {'Spawn (X, Y, Z)':<32}")
    print("-" * 65)

    for m in range(25):
        entry_fo = mt_fo + m * 32
        chunk = exe.data[entry_fo:entry_fo+32]
        stage, room = struct.unpack_from("<II", chunk, 0)
        pos = struct.unpack_from("<4f", chunk, 0x10)
        pos_str = f"({pos[0]:9.1f}, {pos[1]:7.1f}, {pos[2]:9.1f})"
        print(f"M{m:<7} {stage:<6} {room:<6} {pos[3]:<8.2f} {pos_str:<32}")

if __name__ == "__main__":
    main()
