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
}

bool process_is_dmc1() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    return _wcsicmp(name, L"dmc1.exe") == 0;
}

}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user, XInputState* state) {
    if (!real_get_state) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    DWORD result = real_get_state(user, state);
    if (g_dmc1) {
        mod_on_get_state(user, result == ERROR_SUCCESS ? state : nullptr);
    }
    return result;
}

extern "C" __declspec(dllexport) DWORD WINAPI XInputSetState(DWORD user, void* vibration) {
