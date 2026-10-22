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
    }
    struct State {
        DWORD packet;
        WORD buttons;
        BYTE lt;
        BYTE rt;
        SHORT lx, ly, rx, ry;
    } state{};
    using GetFn = DWORD(WINAPI*)(DWORD, State*);
    DWORD result = reinterpret_cast<GetFn>(get_state)(0, &state);
    std::printf("XInputGetState -> %lu packet=%lu\n", result, state.packet);
    return 0;
}
