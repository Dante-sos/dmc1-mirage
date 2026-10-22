import zipfile, struct

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

data = zf.read("Fsd/r10c.fsd")
num_entries = struct.unpack('<I', data[:4])[0]
offsets = [struct.unpack('<I', data[8 + i*8 : 8 + i*8 + 4])[0] for i in range(num_entries)]

# In PS2 DMC1:
# Section 22: Room connections / doors
# Section 23: Triggers
# Section 24, 25, 26, 27: Camera definitions!
# Let's inspect Section 24 in r10c.fsd (off=0x41A840)
s24_off = offsets[24]
s25_off = offsets[25]
s26_off = offsets[26]

# Let's print all 4-float vectors in Section 24 and 26
for s_name, off in [("Sec 24", s24_off), ("Sec 26", s26_off)]:
    print(f"\n=== {s_name} (0x{off:X}) ===")
    chunk = data[off:off+400]
    for i in range(0, len(chunk)-16, 16):
        floats = struct.unpack_from("<4f", chunk, i)
        u32s = struct.unpack_from("<4I", chunk, i)
        print(f"  +{hex(i)}: f=({floats[0]:8.1f}, {floats[1]:8.1f}, {floats[2]:8.1f}, {floats[3]:8.1f}) | u={u32s}")
