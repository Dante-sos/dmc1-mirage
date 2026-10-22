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

fo = rva_to_offset(0x255D00)
print(f"RVA 0x255D00 -> FileOffset {hex(fo) if fo else 'None'}")
if fo:
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    code = exe_data[fo:fo+0x100]
    base_va = 0x140255D00
    for inst in md.disasm(code, base_va):
        print(f"0x{inst.address:X}:  {inst.mnemonic:8s}  {inst.op_str}")
