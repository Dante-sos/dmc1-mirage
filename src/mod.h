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
extern XInputSetStateFn real_set_state;

void mod_start();
void mod_on_get_state(DWORD user, XInputState* state);
void mod_arm_mission(int mission, int mode);
void mod_begin_mission();
void mod_clear_mission();
int mod_armed_mission();
int mod_armed_mode();
bool mod_in_game();
uint8_t* mod_actor();
uintptr_t mod_base();
void mod_log(const char* fmt, ...);
void overlay_install();
void overlay_on_pad(XInputState* state);
