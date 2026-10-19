import pathlib
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parents[1]
dll = root / "build" / "XINPUT9_1_0.dll"
background = pathlib.Path(r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection\dmc1-mission-bg.jpg")
if not background.exists():
    background = root / "assets" / "dmc1-mission-bg.jpg"
payload = root / "payload.h"
out = root / "build" / "ApplyDMC1Switch.exe"


def bytes_of(name, data):
    lines = [f"static const unsigned char {name}[] = {{"]
    for index in range(0, len(data), 20):
        chunk = ", ".join(str(byte) for byte in data[index : index + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append(f"static const unsigned {name}Size = {len(data)};")
    return "\n".join(lines)


def main():
    if not dll.exists():
        sys.exit("build the proxy dll first")
    text = "#pragma once\n" + bytes_of("kDll", dll.read_bytes()) + "\n"
    if background.exists():
        text += bytes_of("kBackground", background.read_bytes()) + "\n"
    else:
