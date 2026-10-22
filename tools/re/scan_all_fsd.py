import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

fsd_files = [f for f in zf.namelist() if f.startswith("Fsd/") and f.endswith(".fsd")]
print(f"Total FSD files: {len(fsd_files)}")

results = []
for name in sorted(fsd_files):
    data = zf.read(name)
    num_entries = struct.unpack('<I', data[:4])[0]
    offsets = []
    for i in range(num_entries):
        off = struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0]
        offsets.append(off)
    
    sec20 = offsets[20] if len(offsets) > 20 else 0
    sec21 = offsets[21] if len(offsets) > 21 else 0
    sec22 = offsets[22] if len(offsets) > 22 else 0
    results.append((name, len(data), num_entries, sec20, sec21, sec22))

print(f"{'Filename':20s} {'Size':10s} {'Sec20':10s} {'Sec21':10s} {'Sec22':10s}")
print("-" * 65)
for name, size, num, s20, s21, s22 in results:
    s20_str = hex(s20) if s20 else "0"
    s21_str = hex(s21) if s21 else "0"
    s22_str = hex(s22) if s22 else "0"
    if s20 or s21:
        print(f"{name:20s} {size:10d} {s20_str:10s} {s21_str:10s} {s22_str:10s}")
