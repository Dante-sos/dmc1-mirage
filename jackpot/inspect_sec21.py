import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

for r in ["r11d", "r10c"]:
    data = zf.read(f"Fsd/{r}.fsd")
    num_entries = struct.unpack('<I', data[:4])[0]
    offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]
    s21_off = offsets[21]
    s21_size = (offsets[22] if len(offsets) > 22 and offsets[22] > 0 else len(data)) - s21_off
    s21_data = data[s21_off:s21_off+s21_size]
    print(f"\n=== {r}.fsd Sec 21 (off=0x{s21_off:X}, size={s21_size}) ===")
    print(f"Header: {s21_data[:32].hex()}")
    # Unpack uint32s
    u32s = struct.unpack_from("<8I", s21_data, 0)
    print(f"u32s: {u32s}")
