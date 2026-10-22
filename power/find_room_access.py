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

# Search for pattern: [reg + 0x274]
# 0x274 in hex is 74 02 00 00
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

idx = 0
found = []
while True:
    idx = exe_data.find(b"\x74\x02\x00\x00", idx)
    if idx == -1 or idx > 0x400000:
        break
    # try disassembling 10 bytes before idx
    for back in range(1, 8):
        off = idx - back
        rva = off + 0xc00
        code = exe_data[off:off+15]
        for inst in md.disasm(code, 0x140000000 + rva):
            if "0x274" in inst.op_str:
                found.append((rva, inst.mnemonic, inst.op_str))
                break
    idx += 4

print(f"Found {len(found)} accesses to 0x274:")
for rva, m, op in found[:25]:
    print(f"  RVA 0x{rva:X}: {m:8s} {op}")
