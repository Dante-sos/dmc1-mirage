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

ptr_fo = 0x48bfc8
print("Pointers around 0x48bfc8:")
for i in range(-5, 15):
    fo = ptr_fo + i * 8
    ptr = struct.unpack_from("<Q", exe_data, fo)[0]
    rva = ptr - 0x140000000
    target_fo = rva_to_offset(rva)
    print(f"Stage {i:2d} ptr at {hex(fo)}: VA=0x{ptr:X} RVA=0x{rva:X} target_fo={hex(target_fo) if target_fo else 'None'}")
