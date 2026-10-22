import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

# Let's search inside Fsd files for enemy identifiers or check room descriptors in dmc1.exe
# In dmc1.exe: Room Descriptor Table starts at 0x5163E8
# Each entry is 24 bytes:
# struct RoomDesc {
#    uint8_t* sub_tbl1; // 8 bytes
#    uint32_t count1;   // 4 bytes
#    uint32_t unk;      // 4 bytes
#    uint8_t* sub_tbl2; // 8 bytes
# };
exe_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1.exe"
with open(exe_path, "rb") as f:
    exe_data = f.read()

def rva_to_offset(rva):
    pe_off = struct.unpack_from("<I", exe_data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", exe_data, pe_off + 6)[0]
    opt_header_size = struct.unpack_from("<H", exe_data, pe_off + 20)[0]
    sec_table_off = pe_off + 24 + opt_header_size
    for i in range(num_sections):
        sec_off = sec_table_off + i * 40
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", exe_data, sec_off + 8)
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return roff + (rva - vaddr)
    return None

rt_fo = rva_to_offset(0x5163E8)
print("Inspecting all room descriptors:")
for i in range(35):
    entry = exe_data[rt_fo + i*24 : rt_fo + (i+1)*24]
    ptr1, count1, unk, ptr2 = struct.unpack_from("<QIIQ", entry)
    rva1 = ptr1 - 0x140000000 if ptr1 else 0
    rva2 = ptr2 - 0x140000000 if ptr2 else 0
    fo1 = rva_to_offset(rva1) if rva1 else None
    fo2 = rva_to_offset(rva2) if rva2 else None
    print(f"Room {i:2d}: count={count1} unk={unk} ptr1=0x{ptr1:X} (fo={hex(fo1) if fo1 else 'None'}) ptr2=0x{ptr2:X} (fo={hex(fo2) if fo2 else 'None'})")
    if fo1 and count1 > 0 and count1 < 20:
        # dump first few bytes of subtable 1
        sub1 = exe_data[fo1:fo1+count1*16]
        print(f"    Sub1: {sub1.hex()[:64]}")
