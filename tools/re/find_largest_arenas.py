import sys
sys.path.append('tools/re')
from dmc1_env import get_nbz_path
import zipfile, struct

zf = zipfile.ZipFile(get_nbz_path())
fsd_files = sorted([f for f in zf.namelist() if f.startswith('Fsd/') and f.endswith('.fsd')])

print("=== Analyzing Arena Floor Sizes across DMC1 ===\n")
print(f"{'Room':<14} {'Coll Size':<10} {'X Width':<10} {'Z Length':<10} {'Area (k m^2)':<14}")
print("-" * 62)

arenas = []

for name in fsd_files:
    data = zf.read(name)
    num_entries = struct.unpack('<I', data[:4])[0]
    offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]
    s21 = offsets[21] if len(offsets) > 21 else 0
    if s21 == 0: continue
    
    next_off = len(data)
    for no in sorted(offsets):
        if no > s21:
            next_off = no
            break
    s21_bytes = data[s21:next_off]
    if len(s21_bytes) < 500: continue
    
    xs, ys, zs = [], [], []
    for j in range(0, len(s21_bytes) - 12, 4):
        x, y, z = struct.unpack('<fff', s21_bytes[j:j+12])
        if -50000 < x < 50000 and -10000 < y < 10000 and -50000 < z < 50000:
            if abs(x) > 10 or abs(z) > 10:
                xs.append(x)
                ys.append(y)
                zs.append(z)
    if len(xs) > 20:
        w_x = max(xs) - min(xs)
        l_z = max(zs) - min(zs)
        area = (w_x / 100.0) * (l_z / 100.0) # in meters squared
        arenas.append((area, name, len(s21_bytes), w_x, l_z, min(xs), max(xs), min(zs), max(zs)))

arenas.sort(reverse=True)
for area, name, sz, wx, lz, min_x, max_x, min_z, max_z in arenas[:25]:
    print(f"{name:<14} {sz:<10} {wx:<10.0f} {lz:<10.0f} {area:<14.0f}")
