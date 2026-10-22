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

# In an FSD file:
# Header has:
# uint32_t num_sections (e.g. 35)
# uint32_t unk
# uint32_t offsets[35] or uint64_t? (each offset was 8 bytes apart: off + 8, off + 16, etc.)
# Let's search for how the FSD table is indexed: e.g. [reg + 0xa0] (section 20 is 20 * 8 = 160 = 0xA0!)
# Section 21 is 21 * 8 = 168 = 0xA8!
# Section 18 is 18 * 8 = 144 = 0x90!
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
# Let's search for instructions with + 0xa8] or + 0xa0] or + 0x90] near fsd loading
print("Searching for FSD section index 21 (+0xa8) or 20 (+0xa0)...")
for i in range(0, 0x400000):
    if exe_data[i:i+4] == b"\xa8\x00\x00\x00" or exe_data[i:i+4] == b"\xa0\x00\x00\x00":
        for back in range(1, 6):
            off = i - back
            rva = off + 0xc00
            for inst in md.disasm(exe_data[off:off+10], 0x140000000 + rva):
                if "+ 0xa8]" in inst.op_str or "+ 0xa0]" in inst.op_str:
                    print(f"RVA 0x{rva:X}: {inst.mnemonic:8s} {inst.op_str}")
                    break
