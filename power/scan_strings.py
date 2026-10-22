path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1.exe"
data = open(path, "rb").read()
needles = [
    b"Curse of",
    b"Mission",
    b"Alastor",
    b"Force Edge",
    b"Nightmare",
    b"Mundus",
    b"MISSION",
]
for n in needles:
    i = data.find(n)
    print(n, hex(i) if i >= 0 else "no")

# utf-16 mission-like names
for n in [b"C\x00u\x00r\x00s\x00e\x00", b"M\x00i\x00s\x00s\x00i\x00o\x00n\x00"]:
    i = data.find(n)
    print("utf16", n[:8], hex(i) if i >= 0 else "no")
