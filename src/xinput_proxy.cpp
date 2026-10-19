#include "mod.h"

#include <cwchar>

XInputGetStateFn real_get_state = nullptr;
XInputSetStateFn real_set_state = nullptr;

namespace {

bool g_dmc1 = false;

void load_real() {
