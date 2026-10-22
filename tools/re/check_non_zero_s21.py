import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

data = zf.read("Fsd/r10c.fsd")
num_entries = struct.unpack('<I', data[:4])[0]
offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]

s21_off = offsets[21]
s21_size = offsets[22] - s21_off

# Find actual non-zero points in Section 21
pts = []
for i in range(0, s21_size - 12, 4):
    x, y, z = struct.unpack_from("<3f", data, s21_off + i)
    if -30000 < x < 30000 and -1000 < y < 2000 and -30000 < z < 30000:
        if abs(x) > 50 and abs(z) > 50:
            pts.append((x, y, z))

print(f"Total non-zero collision points in Sec 21: {len(pts)}")
for p in pts[:25]:
    print(f"  ({p[0]:8.1f}, {p[1]:6.1f}, {p[2]:8.1f})")

xs = [p[0] for p in pts]
ys = [p[1] for p in pts]
zs = [p[2] for p in pts]
print(f"\nNon-zero bounds:")
print(f"  X: [{min(xs):.1f}, {max(xs):.1f}]")
print(f"  Y: [{min(ys):.1f}, {max(ys):.1f}]")
print(f"  Z: [{min(zs):.1f}, {max(zs):.1f}]")
