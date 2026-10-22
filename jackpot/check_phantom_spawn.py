import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

# Earlier we saw in dmc1.exe that Mission 8 / Mission 3 enemy setup references rr10c_1
# Let's search for "rr10c_1" in dmc1.exe or in the files
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

# Find "rr10c_1\0"
idx = exe_data.find(b"rr10c_1\x00")
print(f"'rr10c_1' found at file offset {hex(idx)}")
if idx != -1:
    # Let's see what data surrounds rr10c_1 or references it
    va = 0x140000000 + (idx - 0x400 + 0x1000) # approximate
    print(f"Data around rr10c_1:")
    print(exe_data[idx:idx+128])
