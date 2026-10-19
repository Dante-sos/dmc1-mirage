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
