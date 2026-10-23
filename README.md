# DMC1 Mirage

Style switching, weapon switching, training arena (The Void), and mission select for Devil May Cry 1 in the HD Collection (inspired by DMC3 Crimson).

A short press of a trigger changes weapons. Holding the trigger keeps the original action: the left trigger still activates Devil Trigger, and the right trigger still shoots. From the title screen, Mission Select lists every mission and difficulty, then starts the one you chose.

The mod loads only with `dmc1.exe`, through the `XINPUT9_1_0.dll` the game already imports. It does not edit `dmc1.exe`, `dinput8.dll`, `Crimson.dll`, or `d3d11.dll`.

## Install

Close the game, then run `ApplyDMC1Mirage.exe` from the release. It finds the Steam copy of Devil May Cry 1 and places the mod beside `dmc1.exe`. If Steam is installed somewhere unusual, choose `dmc1.exe` when asked.

Launch Devil May Cry 1 from Steam after that.

## Play

On the title screen, Mission Select sits under Load Data. Pick a mission and a difficulty, then confirm. The mission starts from that mission's opening room.

During a fight, tap the left trigger to cycle melee weapons and the right trigger to cycle guns. Hold either trigger to use Devil Trigger or shoot, as in the original game. The tap window defaults to 180 ms and can be changed in `dmc1-mirage.ini` (or `dmc1-switch.ini`) next to the game:

```
[switch]
tap_ms=180
trigger_threshold=30
```

Reset on the pause menu returns to the title screen.

## Styles

Switch styles during gameplay using the D-Pad or keyboard keys **Z, X, C, V**:
- **D-Pad Up** / **Z**: **Trickster**
- **D-Pad Right** / **X**: **Swordmaster** *(coming soon)*
- **D-Pad Left** / **C**: **Gunslinger** *(coming soon)*
- **D-Pad Down** / **V**: **Royal Guard** *(coming soon)*

### Trickster Actions (B / Circle Button or L Key)
- **Ground Dash**: Tap **B** or **L** to dash forward or in the direction of movement with invulnerability frames (i-frames).
- **Sky Star (Air Dash)**: Tap **B** or **L** mid-air to dash horizontally and reset vertical fall.
- **Air Trick (Forward Warp)**: Hold **Lock-On (RB / Space)** + **Forward (W)** + **B / L** to instantly warp forward directly into attack range.
- **Trick Down (Retreat Warp)**: Hold **Lock-On (RB / Space)** + **Back (S)** + **B / L** to warp backward and escape enemy attacks.

## The Void (Training Arena)

Select **The Void** from the title screen Mission Select menu to enter the infinite practice room.

### Arena Controls & Settings
- **Insert Key**: Toggle the floating practice configuration menu.
- **Monochrome High-Transparency Menu**: Sleek black-and-white glass interface with atomic mouse click registration.
- **Enemy Dummies**: Spawn training dummies (Marionette, Sin Scissors, Plasma, Shadow, Frost, Nobody, Fetish, Blade) with customizable AI states:
  - **Stand Still**: Immobile dummy for combo routing and juggle testing.
  - **Guard**: Constant defense for hit-reaction checks.
  - **Attack**: Hostile AI for counter-timing rehearsal.
  - **Evade**: Evasive maneuvers for chase down tracking.
- **Dummy Count**: Dynamically scale between 1 and 4 dummies with automatic circular formation spacing.
- **Real-Time Telemetry HUD**: Live combo counter, total damage, juggle height tracking, and air-time measurement.
- **Reset Formation**: Tap **Reset Position** or press **Home** to instantly teleport dummies 300 units in front of Dante with zeroed velocity and refreshed health.

## Build

The proxy is built with g++ from the `dmc1-mirage` directory:

```powershell
g++ -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -I src -I src/dante -I src/trish -I src/lady -I src/power -I src/jackpot -o build\XINPUT9_1_0.dll src\jackpot\xinput_proxy.cpp src\power\mod.cpp src\jackpot\ui.cpp src\dante\style.cpp src\dante\style_trickster.cpp src\lady\the_void.cpp src\lady\the_void_menu.cpp src\trish\coordinates.cpp xinput.def -ld3d11 -ldxgi -ld3dcompiler -lgdiplus -lgdi32 -lole32 -luuid -luser32
```

`tools/build_installer.py` then packs that DLL into `ApplyDMC1Mirage.exe`.

This project is not affiliated with Capcom.

