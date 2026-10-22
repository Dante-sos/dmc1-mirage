#pragma once

#include <cstdint>
#include <windows.h>

struct XInputState;

enum class StyleType : int {
    None = 0,
    Trickster = 1,
    RoyalGuard = 2,
    Swordmaster = 3,
    Gunslinger = 4
};

void style_init();
void style_on_pad(XInputState* state);
void style_update();
void style_notify_key_action();
StyleType style_current();
const char* style_name(StyleType type);
void style_set(StyleType type);
bool style_in_action();
const char* style_action_name();
