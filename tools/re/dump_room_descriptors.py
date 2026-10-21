#!/usr/bin/env python3
"""
Dump DMC1 Room Descriptor Table at RVA 0x5163E8.
Inspects the 24-byte Room Descriptor entries, listing loaded room archives and enemy setup scripts per mission.
"""

import struct
from dmc1_env import DmcExe

def main():
    exe = DmcExe()
    rt_rva = 0x5163E8
    rt_fo = exe.rva_to_offset(rt_rva)
    if rt_fo is None:
        raise ValueError(f"Could not resolve RVA 0x{rt_rva:X}")

    print(f"=== DMC1 Room Descriptor Table (RVA 0x{rt_rva:X}, 24 bytes/entry) ===\n")
    for room_id in range(35):
        entry_fo = rt_fo + room_id * 24
        ptr1, count1, unk, ptr2 = struct.unpack_from("<QIIQ", exe.data, entry_fo)
        rva_entry = rt_rva + room_id * 24

        # Read sub-table 1 (archive file list)
        files = []
        if ptr1 != 0 and count1 > 0:
            fo1 = exe.va_to_offset(ptr1)
            if fo1 is not None:
                for c in range(count1):
                    fn = exe.data[fo1 + c*16 : fo1 + (c+1)*16].split(b'\x00')[0].decode('ascii', errors='ignore')
                    if fn:
                        files.append(fn)

        # Read sub-table 2 (mission enemy setups)
        setups = []
        if ptr2 != 0:
            fo2 = exe.va_to_offset(ptr2)
            if fo2 is not None:
                for i in range(0, 64, 16):
                    m_id, ptr_setup = struct.unpack_from("<QQ", exe.data, fo2 + i)
                    if ptr_setup != 0:
                        fo_s = exe.va_to_offset(ptr_setup)
                        if fo_s is not None:
                            sname = exe.data[fo_s:fo_s+16].split(b'\x00')[0].decode('ascii', errors='ignore')
                            if sname:
                                setups.append(f"M{m_id}:{sname}")

        files_str = ", ".join(files) if files else "(none)"
        setups_str = ", ".join(setups) if setups else "(none)"
        print(f"Room {room_id:2d} (RVA 0x{rva_entry:X}):")
        print(f"  Files ({count1}): {files_str}")
        print(f"  Enemy Setups: {setups_str}")

if __name__ == "__main__":
    main()
