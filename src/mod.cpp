#include "mod.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace {

constexpr uintptr_t kSessionPtrRva = 0x5EAB88;
constexpr uintptr_t kActorPtrRva = 0x60ACD0;
constexpr uintptr_t kEventPtrRva = 0x60B148;
constexpr uintptr_t kSpawnPosRva = 0x27E82A0;
constexpr uintptr_t kMissionTableRva = 0x560170;
constexpr uintptr_t kMainLoopRva = 0x251839;
constexpr uintptr_t kLoadSaveRva = 0x255BAE;
constexpr uintptr_t kStartMissionRva = 0x3F9CEF;
constexpr uintptr_t kMenuConfigPtrRva = 0x60B018;
constexpr int kMissionSlots = 23;
constexpr uintptr_t kUpdateMeleeFormRva = 0x2C4A00;
constexpr uintptr_t kUpdateMeleeRva = 0x2C99C0;
constexpr uintptr_t kUpdateRangedRva = 0x2C4C50;

constexpr uintptr_t kEventOffset = 0x1CA4;
constexpr uintptr_t kMissionOffset = 0x1E64;
constexpr uintptr_t kModeOffset = 0x1E66;
constexpr uintptr_t kMeleeOffset = 0x1BCC;
constexpr uintptr_t kMeleeFormOffset = 0x1C4C;
constexpr uintptr_t kRangedOffset = 0x1C48;
constexpr uint32_t kInGameFlag = 0x2000;

constexpr uint8_t kFormDefault = 0;
constexpr uint8_t kFormSparda = 1;

constexpr uint8_t kMeleeIds[] = {4, 0, 1, 3};
constexpr const char* kMeleeNames[] = {"Force Edge", "Alastor", "Ifrit", "Sparda"};
constexpr uint8_t kRangedIds[] = {1, 2, 3, 4};
constexpr const char* kRangedNames[] = {"Handgun", "Shotgun", "Grenadegun", "Nightmare-b"};

using MeleeFormFn = void(__fastcall*)(uint32_t form);
using MeleeFn = void(__fastcall*)(uint32_t weapon, uint32_t playAura);
using RangedFn = void(__fastcall*)(uint32_t weapon);

uintptr_t g_base = 0;
FILE* g_log = nullptr;
int g_tap_ms = 180;
int g_trigger_threshold = 30;

std::atomic<int> g_pending_mission{-1};
std::atomic<int> g_pending_mode{3};
std::atomic<int> g_applies_left{0};
std::atomic<bool> g_mission_staged{false};
DWORD g_stage_tick = 0;
int g_mission_restores = 0;

std::atomic<bool> g_want_melee{false};
std::atomic<bool> g_want_ranged{false};

struct TriggerWatch {
    bool down = false;
    DWORD tick = 0;
    bool passed = false;
};

TriggerWatch g_left;
TriggerWatch g_right;
bool g_hooks_ready = false;

void log_line(const char* fmt, ...) {
    if (!g_log) {
        return;
    }
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(g_log, "%02u:%02u:%02u ", st.wHour, st.wMinute, st.wSecond);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(g_log, fmt, args);
    va_end(args);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

uint8_t* session() {
    auto slot = reinterpret_cast<uint8_t**>(g_base + kSessionPtrRva);
    return *slot;
}

uint8_t* actor() {
    auto slot = reinterpret_cast<uint8_t**>(g_base + kActorPtrRva);
    return *slot;
}

bool in_game() {
    uint8_t* data = session();
    if (!data) {
        return false;
    }
    uint32_t event = 0;
    std::memcpy(&event, data + kEventOffset, sizeof(event));
    return (event & kInGameFlag) != 0;
}

void read_ini() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        wcscpy(slash + 1, L"dmc1-switch.ini");
    }
    g_tap_ms = GetPrivateProfileIntW(L"switch", L"tap_ms", 180, path);
    g_trigger_threshold = GetPrivateProfileIntW(L"switch", L"trigger_threshold", 30, path);
    if (g_tap_ms < 80) {
        g_tap_ms = 80;
    }
    if (g_tap_ms > 400) {
        g_tap_ms = 400;
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"switch", L"tap_ms", L"180", path);
        WritePrivateProfileStringW(L"switch", L"trigger_threshold", L"30", path);
    }
}

uint8_t* alloc_near(uintptr_t target, size_t size) {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t grain = info.dwAllocationGranularity;
    uintptr_t min = target > 0x40000000ull ? (target - 0x40000000ull) : grain;
    uintptr_t max = target + 0x40000000ull;
    min &= ~(grain - 1);
    for (uintptr_t addr = min; addr < max; addr += grain) {
        void* block = VirtualAlloc(reinterpret_cast<void*>(addr), size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (block) {
            return static_cast<uint8_t*>(block);
        }
    }
    return nullptr;
}

void emit(uint8_t*& cursor, std::initializer_list<uint8_t> bytes) {
    for (uint8_t byte : bytes) {
        *cursor++ = byte;
    }
}

void emit_u64(uint8_t*& cursor, uint64_t value) {
    std::memcpy(cursor, &value, sizeof(value));
    cursor += sizeof(value);
}

// Saves every general register and xmm0-5, calls fn, runs the stolen bytes, then jumps back.
uint8_t* write_stub(uint8_t* cursor, void (*fn)(), const uint8_t* stolen, size_t stolen_len, void* ret) {
    emit(cursor, {0x9C});
    emit(cursor, {0x50, 0x51, 0x52, 0x53, 0x55, 0x56, 0x57});
    emit(cursor, {0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53});
    emit(cursor, {0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57});
    emit(cursor, {0x48, 0x89, 0xE3});
    emit(cursor, {0x48, 0x83, 0xE4, 0xF0});
    emit(cursor, {0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00});
