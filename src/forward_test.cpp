#include <Windows.h>
#include <cstdio>

int main() {
    HMODULE library = LoadLibraryW(L"XINPUT9_1_0.dll");
    if (!library) {
        std::printf("load failed %lu\n", GetLastError());
        return 1;
    }
    auto get_state = GetProcAddress(library, "XInputGetState");
    auto set_state = GetProcAddress(library, "XInputSetState");
    std::printf("get=%p set=%p\n", get_state, set_state);
    if (!get_state || !set_state) {
        return 1;
