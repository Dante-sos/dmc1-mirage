import zipfile

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

# Let's search inside all FSD files for glass textures or words like "dome" or check which room has glass
# Or let's check which FSD has the glass floor
fsd_names = [n for n in zf.namelist() if n.startswith("Fsd/r") and n.endswith(".fsd")]
print("Searching for strings in FSD files...")
for name in fsd_names:
    data = zf.read(name)
    # Check if there are door names or strings
    # Find all ascii strings >= 5 chars
    found_glass = False
    for kw in [b"glass", b"dome", b"roof", b"phantom"]:
        if kw in data.lower():
            print(f"Match '{kw.decode()}' in {name}")
