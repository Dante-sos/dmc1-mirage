import struct

exe_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1.exe"
with open(exe_path, "rb") as f:
    exe_data = f.read()

# Let's inspect the room descriptor table around 0x5163E8
# In 64-bit PE, find section headers or file offsets
# Let's write a function to map RVA to file offset
def rva_to_offset(rva):
    # Read PE header
    pe_off = struct.unpack_from("<I", exe_data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", exe_data, pe_off + 6)[0]
    opt_header_size = struct.unpack_from("<H", exe_data, pe_off + 20)[0]
    sec_table_off = pe_off + 24 + opt_header_size
    for i in range(num_sections):
        sec_off = sec_table_off + i * 40
        sec_name = exe_data[sec_off:sec_off+8].rstrip(b'\x00').decode('ascii', errors='ignore')
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", exe_data, sec_off + 8)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return roff + (rva - vaddr)
    return None

print("Checking RVAs...")
for name, rva in [
    ("kMissionTableRva", 0x560170),
    ("kRoomTableEntry22Rva", 0x5165F8),
    ("kRoomTableEntry24Rva", 0x516628),
    ("kStage1Room22FileRva", 0x5A45BC),
    ("Stage 1 file table start", 0x5A4590),
    ("Room Table start", 0x5163E8)
]:
    fo = rva_to_offset(rva)
    print(f"{name}: RVA=0x{rva:X} FileOffset={hex(fo) if fo else 'None'}")

# Let's dump the room descriptor table entries around 0x5163E8
rt_fo = rva_to_offset(0x5163E8)
if rt_fo:
    print("\n--- Room Descriptor Table (24 bytes per entry) ---")
    for i in range(30):
        entry_fo = rt_fo + i * 24
        chunk = exe_data[entry_fo:entry_fo+24]
        # print first 24 bytes in hex and some uint16/uint32s
        u32s = struct.unpack_from("<6I", chunk)
        print(f"Room {i:2d} (RVA 0x{0x5163E8 + i*24:X}): {chunk.hex()} | {u32s}")

# Let's dump Stage 1 file index table at 0x5A4590
stg1_fo = rva_to_offset(0x5A4590)
if stg1_fo:
    print("\n--- Stage 1 File Index Table (uint16 per entry) ---")
    for i in range(30):
        val = struct.unpack_from("<H", exe_data, stg1_fo + i * 2)[0]
        print(f"Room {i:2d} (RVA 0x{0x5A4590 + i*2:X}): file_id={val}")
