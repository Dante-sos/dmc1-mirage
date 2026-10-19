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
            if val & (1 << 63):
                print("  ordinal", val & 0xFFFF)
            else:
                fn_off = r2o(val & 0x7FFFFFFF)
                fn = data[fn_off + 2 : data.find(b"\x00", fn_off + 2)].decode()
                print(" ", fn)
            thunk += 8
    off += 20

patterns = {
    "or_rt": bytes.fromhex("66 41 83 08 02 49 C7 40 3A 00 00 00 00"),
    "taunt_state": bytes.fromhex("48 C7 01 01 42 00 00"),
    "magic_pts": bytes.fromhex("66 89 81 CE 1B 00 00"),
    "expertise": bytes.fromhex("41 89 B8 68 24 00 00"),
    "hp_byte": bytes.fromhex("41 0F B6 80 64 24 00 00"),
    "room_rcx": bytes.fromhex("89 81 74 02 00 00"),
    "room_rbx": bytes.fromhex("89 83 74 02 00 00"),
    "event_mov": bytes.fromhex("41 89 80 B4 1C 00 00"),
    "mainloop": bytes.fromhex("41 8B 93 9C 00 00 00"),
    "mov_rdx_then_rcx": bytes.fromhex("48 8B 15"),
}

text = next(s for s in sections if s[0] == ".text")
_, va, sz, raw = text
blob = data[raw : raw + sz]


def hits(pat, limit=12):
    found = []
    start = 0
    while len(found) < limit:
        i = blob.find(pat, start)
        if i < 0:
            break
        found.append(va + i)
        start = i + 1
    return found


for name, pat in patterns.items():
