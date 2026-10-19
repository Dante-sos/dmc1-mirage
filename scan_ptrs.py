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
