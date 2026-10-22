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

for name, va in [
    ("Room 22 Mission 8 enemy setup", 0x1404D9E68),
    ("Room 24 Mission 8 enemy setup", 0x1404D9E50),
    ("Room 9 Mission 3 enemy setup", 0x1404D9F10),
]:
    rva = va - 0x140000000
    fo = rva_to_offset(rva)
    chunk = exe_data[fo:fo+64]
    print(f"\n=== {name} (VA 0x{va:X}, fo={hex(fo)}) ===")
    print(f"Hex: {chunk.hex()}")
    # Let's inspect strings or ints
    # Often enemy files are .anm or .eml or enemy IDs
    print(f"Decoded: {chunk}")
