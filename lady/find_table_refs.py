import capstone, struct

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

# Find where Room Table (0x5163E8) is accessed in code
# In x64: lea reg, [rip + disp] where target is 0x5163E8
target_rva = 0x5163E8
for i in range(0, 0x400000):
    disp = struct.unpack_from("<i", exe_data, i)[0]
    # RVA = i + 0xc00
    rva = i + 0xc00
    if rva + 4 + disp == target_rva:
        print(f"Found ref to Room Table (0x5163E8) at RVA 0x{rva:X} (file offset {hex(i)})")

# Find where Mission Table (0x560170) is accessed in code
target_rva2 = 0x560170
for i in range(0, 0x400000):
    disp = struct.unpack_from("<i", exe_data, i)[0]
    rva = i + 0xc00
    if rva + 4 + disp == target_rva2:
        print(f"Found ref to Mission Table (0x560170) at RVA 0x{rva:X} (file offset {hex(i)})")
