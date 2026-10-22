import os
import pathlib
import struct

def find_game_dir() -> pathlib.Path:
    env_dir = os.environ.get("DMC_DIR")
    if env_dir and pathlib.Path(env_dir).exists():
        return pathlib.Path(env_dir)
    # Check 2 levels up from tools/re/
    cand = pathlib.Path(__file__).resolve().parents[2]
    if (cand / "dmc1.exe").exists():
        return cand
    default_steam = pathlib.Path(r"c:\Program Files (x86)\Steam\steamapps\common\Devil May Cry HD Collection")
    if (default_steam / "dmc1.exe").exists():
        return default_steam
    raise FileNotFoundError("Could not locate Devil May Cry HD Collection directory containing dmc1.exe")

def get_exe_path() -> pathlib.Path:
    p = find_game_dir() / "dmc1.exe"
    if not p.exists():
        raise FileNotFoundError(f"dmc1.exe not found at {p}")
    return p

def get_nbz_path() -> pathlib.Path:
    p = find_game_dir() / "data" / "dmc1" / "dmc1-0.nbz"
    if not p.exists():
        raise FileNotFoundError(f"dmc1-0.nbz not found at {p}")
    return p

class DmcExe:
    def __init__(self, path: pathlib.Path = None):
        if path is None:
            path = get_exe_path()
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        self.sections = []
        self._parse_pe()

    def _parse_pe(self):
        pe_off = struct.unpack_from("<I", self.data, 0x3C)[0]
        num_sections = struct.unpack_from("<H", self.data, pe_off + 6)[0]
        opt_header_size = struct.unpack_from("<H", self.data, pe_off + 20)[0]
        sec_table_off = pe_off + 24 + opt_header_size
        for i in range(num_sections):
            sec_off = sec_table_off + i * 40
            sec_name = self.data[sec_off:sec_off+8].rstrip(b'\x00').decode('ascii', errors='ignore')
            vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", self.data, sec_off + 8)
            self.sections.append((sec_name, vaddr, max(vsize, rsize), roff, rsize))

    def rva_to_offset(self, rva: int):
        for name, vaddr, span, roff, rsize in self.sections:
            if vaddr <= rva < vaddr + span:
                return roff + (rva - vaddr)
        return None

    def offset_to_rva(self, off: int):
        for name, vaddr, span, roff, rsize in self.sections:
            if roff <= off < roff + rsize:
                return vaddr + (off - roff)
        return None

    def va_to_offset(self, va: int):
        return self.rva_to_offset(va - 0x140000000)
