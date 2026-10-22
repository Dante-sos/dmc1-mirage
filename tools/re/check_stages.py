import struct

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

def offset_to_rva(off):
    pe_off = struct.unpack_from("<I", exe_data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", exe_data, pe_off + 6)[0]
    opt_header_size = struct.unpack_from("<H", exe_data, pe_off + 20)[0]
    sec_table_off = pe_off + 24 + opt_header_size
    for i in range(num_sections):
        sec_off = sec_table_off + i * 40
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", exe_data, sec_off + 8)
        if roff <= off < roff + rsize:
            return vaddr + (off - roff)
    return None

# Let's inspect Stage tables. Where are stage tables defined?
# Usually there is an array of pointers to stage tables, or a table of tables.
# Let's search for pointers to 0x1405A4590 or 0x5A4590
target_va = 0x1405A4590
target_rva = 0x5A4590
print("Searching for references to Stage 1 table...")
for i in range(0, len(exe_data)-8, 4):
    u64 = struct.unpack_from("<Q", exe_data, i)[0]
    if u64 == target_va:
        print(f"Found VA pointer at file offset {hex(i)} RVA {hex(offset_to_rva(i))}")
    u32 = struct.unpack_from("<I", exe_data, i)[0]
    if u32 == target_rva:
        print(f"Found RVA pointer at file offset {hex(i)} RVA {hex(offset_to_rva(i))}")

# Also let's check what is around 0x5A4590
fo = rva_to_offset(0x5A4590)
print(f"\nHex dump around 0x5A4590 (file offset {hex(fo)}):")
for s in range(-2, 10):
    sub_fo = fo + s * 64
    vals = [struct.unpack_from("<H", exe_data, sub_fo + j*2)[0] for j in range(32)]
    print(f"Offset {hex(sub_fo)} (RVA {hex(offset_to_rva(sub_fo))}): {vals[:16]}")
