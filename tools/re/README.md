# DMC1 Reverse Engineering Toolkit

This directory contains standalone Python tools used to reverse-engineer Capcom's **Devil May Cry 1 HD (PC x64)** engine, room structures, collision architecture, and mission flow.

---

## Tool Overview

| Script | Purpose |
|---|---|
| `dump_missions.py` | Dumps the complete **Mission Table** (`0x560170`), showing all missions (0–24), initial stage, room ID, and player spawn coordinates `(X, Y, Z, Facing)`. |
| `dump_stages.py` | Dumps the **Stage Room Tables** (Stages 0–5) and maps every room index to its `.fsd` archive file. |
| `dump_room_descriptors.py` | Dumps the **Room Descriptor Table** (`0x5163E8`, 24 bytes/entry), showing room files and per-mission enemy setup scripts (`ptr2`). |
| `dump_file_string_table.py` | Dumps the master **File Table** (`0x4D8000`, 16 bytes/entry) that maps integer IDs to archive filenames. |
| `scan_fsd_collisions.py` | Scans all 106 `.fsd` archives in `dmc1-0.nbz` to verify whether they have Section 20 (Navmesh) and Section 21 (Player Floor Collision Mesh). |
| `dump_fsd_sections.py` | Dumps all 35 sections of any given `.fsd` file (offsets, sizes, and section magic numbers). |
| `inspect_collision_mesh.py` | Inspects Section 21 (Player Floor Collision Mesh) of a specified room and reports vertex count, coordinate bounds, and ground height ($Y$). |
| `find_code_refs.py` | Searches `dmc1.exe` for RIP-relative x64 instructions referencing a target RVA or VA. |

---

## Key Reverse-Engineering Findings for DMC1

### 1. The FSD Room Container Format
Every room in DMC1 is packaged as an `.fsd` file inside `data/dmc1/dmc1-0.nbz`.
An `.fsd` container has a header containing up to **35 section offsets**:
* **Section 14 & 17**: 3D visual models & geometry.
* **Section 19**: 3D mesh vertices.
* **Section 20**: Navmesh (enemy & player pathfinding).
* **Section 21**: **Player Floor Collision Mesh** (BSP/poly tree).
* **Section 22–29**: Camera boundary boxes, cutscene triggers, and door transitions.
* **Section 34**: Textures (`T32` / `DDS`).

> **Note on Cutscene Rooms**:
> Rooms such as `r121.fsd` (rooftop cutscene) have Section 20 and 21 set to `0x0` because Capcom only built them for non-interactive cutscenes. Spawning the player in a room with 0 collision bytes causes an immediate fall into negative infinity ($Y \to -\infty$) as soon as movement input is received. Combat arenas (e.g. `r10c.fsd` Cathedral) have full Section 20 & 21 collision meshes.

### 2. Critical Memory RVAs in `dmc1.exe` (HD Collection x64)

* **Mission Table**: `0x560170` (32 bytes per entry; `stage` at `+0x0`, `room` at `+0x4`, spawn coordinates at `+0x10`).
* **Room Descriptor Table**: `0x5163E8` (24 bytes per entry; `ptr1` at `+0x0`, `count1` at `+0x8`, `ptr2` at `+0x10`).
* **Stage 1 File Index Table**: `0x5A4590` (`uint16_t` per room).
* **Title Briefing / Shop Skip Site**: `0x24FE5E` (Patch `0F 84` -> `E9` to skip mission briefing & divinity statue shop).
* **Enemy Count**: `0x4CB6BA0` (`int32_t`).
* **Enemy List Pointer**: `0x4CB6BB0` (`uint8_t**[64]`).
* **Player Actor Struct**:
  * Position: `player + 0x1AC0` (X: `+0x1AC0`, Y: `+0x1AC4`, Z: `+0x1AC8`).
  * Facing: `player + 0x124`.
  * Current HP: `player + 0x1BCE`.
  * Max HP: `player + 0x1BD0`.
