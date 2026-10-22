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

def get_file_name(file_idx):
    if file_idx == 0:
        return "(none)"
    off = 0x4d8000 + file_idx * 16
    if 0 <= off < len(exe_data):
        return exe_data[off:off+16].split(b'\x00')[0].decode('ascii', errors='ignore')
    return f"unknown_{file_idx}"

stage_ptrs = [
    (0, 0x5A4488),
    (1, 0x5A4590),
    (2, 0x5A44E8),
    (3, 0x5A4530),
    (4, 0x5A44C8),
    (5, 0x5A44A8),
]

for stg_id, stg_rva in stage_ptrs:
    fo = rva_to_offset(stg_rva)
    print(f"\n=== STAGE {stg_id} (RVA 0x{stg_rva:X}) ===")
    for room_id in range(35):
        val = struct.unpack_from("<H", exe_data, fo + room_id * 2)[0]
        fname = get_file_name(val)
        if val != 0:
            print(f"  Room {room_id:2d}: file_id={val:3d} ({fname})")
