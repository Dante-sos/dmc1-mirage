#include "mod.h"

#include <cwchar>

XInputGetStateFn real_get_state = nullptr;
XInputSetStateFn real_set_state = nullptr;

namespace {

bool g_dmc1 = false;

void load_real() {
    wchar_t path[MAX_PATH]{};
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH - 20) {
        return;
    }
    wcscat_s(path, L"\\XINPUT9_1_0.dll");
    HMODULE library = LoadLibraryW(path);
    if (!library) {
        return;
    }
    real_get_state = reinterpret_cast<XInputGetStateFn>(GetProcAddress(library, "XInputGetState"));
    real_set_state = reinterpret_cast<XInputSetStateFn>(GetProcAddress(library, "XInputSetState"));
