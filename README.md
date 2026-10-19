# DMC1 Switch

Weapon switching and mission select for Devil May Cry 1 in the HD Collection.

A short press of a trigger changes weapons. Holding the trigger keeps the original action: the left trigger still activates Devil Trigger, and the right trigger still shoots. From the title screen, Mission Select lists every mission and difficulty, then starts the one you chose.

The mod loads only with `dmc1.exe`, through the `XINPUT9_1_0.dll` the game already imports. It does not edit `dmc1.exe`, `dinput8.dll`, `Crimson.dll`, or `d3d11.dll`.

## Install

Close the game, then run `ApplyDMC1Switch.exe` from the release. It finds the Steam copy of Devil May Cry 1 and places the mod beside `dmc1.exe`. If Steam is installed somewhere unusual, choose `dmc1.exe` when asked.

Launch Devil May Cry 1 from Steam after that.

## Play

On the title screen, Mission Select sits under Load Data. Pick a mission and a difficulty, then confirm. The mission starts from that mission's opening room.

During a fight, tap the left trigger to cycle melee weapons and the right trigger to cycle guns. Hold either trigger to use Devil Trigger or shoot, as in the original game. The tap window defaults to 180 ms and can be changed in `dmc1-switch.ini` next to the game:

```
[switch]
tap_ms=180
trigger_threshold=30
```

Reset on the pause menu returns to the title screen.

## Build

The proxy is built with g++ from the `dmc1-switch` directory:

```
g++ -shared -O2 -std=c++17 -static -static-libgcc -static-libstdc++ -o build\XINPUT9_1_0.dll src\xinput_proxy.cpp src\mod.cpp src\ui.cpp xinput.def -ld3d11 -ldxgi -ld3dcompiler -lgdiplus -lgdi32 -lole32 -luuid -luser32
```

`tools/build_installer.py` then packs that DLL into `ApplyDMC1Switch.exe`.

This project is not affiliated with Capcom.
