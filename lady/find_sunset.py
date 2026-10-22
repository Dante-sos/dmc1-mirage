import zipfile, io
from PIL import Image
import numpy as np

nbz_path = r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\data\dmc1\dmc1-0.nbz"
zf = zipfile.ZipFile(nbz_path)

# In media_1792587936423.jpg, the sunset sky has prominent orange/red colors:
# RGB roughly (200, 100, 50) or (180, 80, 40)
# Let's search all textures in all FSD files for this sunset sky!
print("Searching for orange/sunset skybox texture in all FSD files...")
sunset_rooms = []
for name in sorted(zf.namelist()):
    if name.startswith("Fsd/r") and name.endswith(".fsd"):
        raw = zf.read(name)
        idx = 0
        tex_i = 0
        while True:
            idx = raw.find(b"DDS ", idx)
            if idx == -1: break
            next_idx = raw.find(b"DDS ", idx + 4)
            if next_idx == -1: next_idx = len(raw)
            chunk = raw[idx:next_idx]
            try:
                im = Image.open(io.BytesIO(chunk)).convert('RGB')
                arr = np.array(im)
                # Check for sunset colors: high red, medium green, low blue
                r = arr[:, :, 0].mean()
                g = arr[:, :, 1].mean()
                b = arr[:, :, 2].mean()
                if r > 100 and g > 50 and b < 50 and (r - b) > 60:
                    print(f"Found sunset texture in {name} (tex {tex_i}, size {im.size}, R={r:.1f} G={g:.1f} B={b:.1f})")
                    sunset_rooms.append((name, tex_i, im.size))
            except:
                pass
            tex_i += 1
            idx += 4

print("\nAll sunset rooms:", sunset_rooms)
