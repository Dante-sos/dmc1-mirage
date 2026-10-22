import ctypes
import subprocess
import sys

PROCESS_ALL_ACCESS = 0x1F0FFF

def main():
    out = subprocess.check_output('tasklist /FI "IMAGENAME eq dmc1.exe" /FO CSV', shell=True).decode()
    lines = [l for l in out.strip().split('\n') if l]
    if len(lines) <= 1:
        print("dmc1.exe is NOT running")
        return

    pid = int(lines[1].split(',')[1].strip('"'))
    print(f"dmc1.exe PID: {pid}")

    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not handle:
        print("Failed to open process")
        return

    # In 64-bit Windows, get module base address
    import ctypes.wintypes as wt
    from ctypes import wintypes

    psapi = ctypes.windll.psapi
    hMods = (wintypes.HMODULE * 1024)()
    cbNeeded = wintypes.DWORD()
    psapi.EnumProcessModules(handle, hMods, ctypes.sizeof(hMods), ctypes.byref(cbNeeded))
    base = hMods[0]
    print(f"dmc1.exe Base: 0x{base:x}")

    def read_u64(addr):
        b = ctypes.c_uint64()
        kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), ctypes.byref(b), 8, None)
        return b.value

    def read_u32(addr):
        b = ctypes.c_uint32()
        kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), ctypes.byref(b), 4, None)
        return b.value

    def read_floats(addr, n):
        b = (ctypes.c_float * n)()
        kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), ctypes.byref(b), 4 * n, None)
        return list(b)

    # Actor
    actor_ptr = read_u64(base + 0x60ACD0)
    print(f"Actor pointer: 0x{actor_ptr:x}")
    if actor_ptr:
        pos = read_floats(actor_ptr + 0x1AC0, 4)
        print(f"Actor pos (0x1AC0): X={pos[0]:.1f}, Y={pos[1]:.1f}, Z={pos[2]:.1f}")
        phys_pos = read_floats(actor_ptr + 0x1E0, 4)
        print(f"Phys pos (0x1E0):   X={phys_pos[0]:.1f}, Y={phys_pos[1]:.1f}, Z={phys_pos[2]:.1f}")
        facing = read_floats(actor_ptr + 0x124, 1)[0]
        print(f"Facing (0x124):     {facing:.2f}")

    # Event
    event_ptr = read_u64(base + 0x60B148)
    print(f"Event pointer: 0x{event_ptr:x}")
    if event_ptr:
        stage = read_u32(event_ptr + 0x270)
        room = read_u32(event_ptr + 0x274)
        print(f"Stage: {stage}, Room: {room}")

    # Room Src pointer at 0x60AF18
    room_src = read_u64(base + 0x60AF18)
    print(f"Room Src pointer: 0x{room_src:x}")

    # Let's search memory for Camera structures!
    # In DMC1, the active camera matrix and Eye/Target are stored in memory.
    # Let's search for floats matching the Eye / Target we found in r10c.fsd:
    # Cam 0 Eye: (-1164.0, 1200.0, 7555.3) or Target: (-294.3, 700.0, 9413.4)
    # Search around base + 0x500000 to base + 0x5000000
    print("Searching for active camera in memory...")
    # Read regions
    for offset in range(0x400000, 0x5000000, 0x10000):
        buf = (ctypes.c_float * 1024)()
        kernel32.ReadProcessMemory(handle, ctypes.c_void_p(base + offset), ctypes.byref(buf), 4096, None)
        for i in range(1020):
            # Check if eye (-1164, 1200, 7555) or target (-294, 700, 9413) is present
            if abs(buf[i] - (-1164.0)) < 1.0 and abs(buf[i+1] - 1200.0) < 1.0 and abs(buf[i+2] - 7555.3) < 1.0:
                print(f"Found Cam 0 Eye at base + 0x{offset + i*4:x}: ({buf[i]:.1f}, {buf[i+1]:.1f}, {buf[i+2]:.1f})")
            if abs(buf[i] - (-294.3)) < 1.0 and abs(buf[i+1] - 700.0) < 1.0 and abs(buf[i+2] - 9413.4) < 1.0:
                print(f"Found Cam 0 Target at base + 0x{offset + i*4:x}: ({buf[i]:.1f}, {buf[i+1]:.1f}, {buf[i+2]:.1f})")

if __name__ == "__main__":
    main()
