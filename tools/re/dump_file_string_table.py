#!/usr/bin/env python3
"""
Dump DMC1 Internal Master File Table (String Table).
This table maps uint16 file indices to archive filenames (e.g. 217 -> r10c.fsd, 230 -> r11d.fsd).
"""

from dmc1_env import DmcExe

def main():
    exe = DmcExe()
    base_off = 0x4D8000

    print("=== DMC1 Internal Master File Table (16 bytes/entry) ===\n")
    print(f"{'File ID':<10} {'File Offset':<14} {'Filename':<20} {'Raw Hex':<34}")
    print("-" * 75)

    for i in range(350):
        off = base_off + i * 16
        if off + 16 > len(exe.data):
            break
        entry = exe.data[off:off+16]
        name = entry.split(b'\x00')[0].decode('ascii', errors='ignore')
        if name and not all(c == '\x00' for c in name):
            print(f"{i:<10} 0x{off:06X}       {name:<20} {entry.hex():<34}")

if __name__ == "__main__":
    main()
