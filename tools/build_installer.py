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
