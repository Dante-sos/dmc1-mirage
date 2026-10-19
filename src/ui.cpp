#include "mod.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <gdiplus.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kMissionCount = 23;
const char* kMissions[kMissionCount] = {
    "1   Curse of the Bloody Puppets",
    "2   Judge of Death",
    "3   Destroyer of Ardor",
