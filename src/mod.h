#pragma once

#include <Windows.h>
#include <cstdint>

struct XInputGamepad {
    WORD buttons;
    BYTE leftTrigger;
    BYTE rightTrigger;
    SHORT leftX;
    SHORT leftY;
    SHORT rightX;
    SHORT rightY;
};

struct XInputState {
    DWORD packet;
    XInputGamepad gamepad;
};

using XInputGetStateFn = DWORD(WINAPI*)(DWORD user, XInputState* state);
using XInputSetStateFn = DWORD(WINAPI*)(DWORD user, void* vibration);

extern XInputGetStateFn real_get_state;
