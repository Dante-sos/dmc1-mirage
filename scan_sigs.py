import struct

path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1.exe"
data = open(path, "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
opt = pe + 24
optsz = struct.unpack_from("<H", data, pe + 20)[0]
nsec = struct.unpack_from("<H", data, pe + 6)[0]
dd_off = opt + 112
import_rva = struct.unpack_from("<I", data, dd_off + 8)[0]
sec_off = opt + optsz
sections = []
for i in range(nsec):
    s = sec_off + i * 40
    name = data[s : s + 8].split(b"\x00")[0].decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, s + 8)
    sections.append((name, va, max(vsz, rsz), raw))


def r2o(rva):
    for name, va, sz, raw in sections:
        if va <= rva < va + sz:
            return raw + (rva - va)
    return None


off = r2o(import_rva)
while off:
    ilt, tstamp, fwd, name_rva, iat = struct.unpack_from("<IIIII", data, off)
    if name_rva == 0 and ilt == 0:
        break
    no = r2o(name_rva)
    name = data[no : data.find(b"\x00", no)].decode()
    if name.lower() == "xinput9_1_0.dll":
        thunk = r2o(ilt or iat)
        print("XInput thunk", hex(thunk))
        while True:
            val = struct.unpack_from("<Q", data, thunk)[0]
            if val == 0:
                break
