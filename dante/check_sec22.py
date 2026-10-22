import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

data = zf.read("Fsd/r11d.fsd")
# Sec 22: off=0x3c3990
sec22_off = 0x3c3990
sec22_data = data[sec22_off:sec22_off+512]
print(f"Sec 22 hex: {sec22_data[:64].hex()}")

# Print any uint32s or floats in sec 22
for i in range(0, min(len(sec22_data), 256), 16):
    u32s = struct.unpack_from("<4I", sec22_data, i)
    floats = struct.unpack_from("<4f", sec22_data, i)
    print(f"  +0x{i:02X}: u32={u32s} floats=({floats[0]:.1f}, {floats[1]:.1f}, {floats[2]:.1f}, {floats[3]:.1f})")
