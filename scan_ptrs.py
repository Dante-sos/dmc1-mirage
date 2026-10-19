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
print("load save context")
dump(0x255B40, 128)
print("init session")
dump(0x3F9CE0, 80)

needles = {
    "mission_1E64": bytes.fromhex("64 1E 00 00"),
    "event_1CA4": bytes.fromhex("A4 1C 00 00"),
    "mode_near": bytes.fromhex("66 1E 00 00"),
}
for name, pat in needles.items():
    found = []
    start = 0
    while len(found) < 20:
        i = blob.find(pat, start)
        if i < 0:
            break
        found.append(va + i - 3)
