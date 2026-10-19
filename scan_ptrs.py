import struct

path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1.exe"
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt = pe + 24
optsz = struct.unpack_from("<H", data, pe + 20)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
sec_off = opt + optsz
sections = []
for i in range(nsec):
    s = sec_off + i * 40
    name = data[s : s + 8].split(b"\x00")[0].decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, s + 8)
    sections.append((name, va, max(vsz, rsz), raw, rsz))


def r2o(rva):
    for name, va, sz, raw, rsz in sections:
        if va <= rva < va + sz:
            return raw + (rva - va)
    return None


text = next(s for s in sections if s[0] == ".text")
_, va, sz, raw, rsz = text
blob = data[raw : raw + rsz]


def dump(rva, n=48):
    off = r2o(rva)
    print(hex(rva), data[off : off + n].hex(" "))


print("expertise context")
dump(0x2C3C80, 64)
