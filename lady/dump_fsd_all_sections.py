import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

def dump_sections(name):
    data = zf.read(f"Fsd/{name}.fsd")
    num_entries = struct.unpack('<I', data[:4])[0]
    offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]
    print(f"\n=== {name}.fsd: {num_entries} sections, total len = {len(data)} ===")
    for i in range(num_entries):
        off = offsets[i]
        next_off = len(data)
        for no in sorted(offsets):
            if no > off:
                next_off = no
                break
        size = next_off - off if off > 0 else 0
        magic = data[off:off+8] if off > 0 else b""
        if off > 0 or size > 0:
            print(f"  Sec {i:2d}: off=0x{off:06x} size={size:8d} magic={magic.hex()}")

dump_sections("r121")
dump_sections("r120")
dump_sections("r11d")
