#!/usr/bin/env python3
"""
Find RIP-relative code references (mov, lea, cmp, jmp) in dmc1.exe to a target RVA or VA.
Usage:
    python find_code_refs.py [target_hex_address]
Example:
    python find_code_refs.py 0x560170      (Mission Table)
    python find_code_refs.py 0x5163E8      (Room Descriptor Table)
    python find_code_refs.py 0x1405A4590   (Stage 1 File Index Table)
"""

import struct
import sys
from dmc1_env import DmcExe

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    arg = sys.argv[1]
    target_addr = int(arg, 16) if arg.startswith("0x") or arg.startswith("0X") else int(arg, 16)
    if target_addr >= 0x140000000:
        target_rva = target_addr - 0x140000000
    else:
        target_rva = target_addr

    exe = DmcExe()
    print(f"=== Searching dmc1.exe .text for references to RVA 0x{target_rva:X} (VA 0x{0x140000000 + target_rva:X}) ===\n")

    # .text section search
    found_count = 0
    # In x64: 4-byte displacement where: target_rva = inst_rva + 4 + disp
    for i in range(0, 0x400000):
        rva = exe.offset_to_rva(i)
        if rva is None:
            continue
        disp = struct.unpack_from("<i", exe.data, i)[0]
        if rva + 4 + disp == target_rva:
            # Let's inspect opcode before displacement (1 to 4 bytes back)
            print(f"Found reference at file offset 0x{i:06X} (RVA 0x{rva:06X}, VA 0x{0x140000000+rva:X})")
            found_count += 1

    print(f"\nDone. Found {found_count} matching instructions.")

if __name__ == "__main__":
    main()
