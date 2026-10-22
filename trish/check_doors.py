import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

# Let's inspect Section 22 / doors in r11d.fsd
data = zf.read("Fsd/r11d.fsd")
num_entries = struct.unpack('<I', data[:4])[0]
offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]

print(f"r11d.fsd has {num_entries} sections")
for i, off in enumerate(offsets):
    if off > 0:
        magic = data[off:off+8]
        next_off = offsets[i+1] if i+1 < len(offsets) and offsets[i+1] > 0 else len(data)
        size = next_off - off
        print(f"Sec {i:2d}: off={hex(off)} size={size} magic={magic.hex()}")

# Let's inspect strings in r11d.fsd (doors usually have names like dr11d_... or target room names)
for i in range(0, len(data)-10):
    if data[i:i+2] == b"dr" or data[i:i+2] == b"rr":
        s = data[i:i+16].split(b'\x00')[0]
        if len(s) >= 4:
            print(f"String at {hex(i)}: {s}")
