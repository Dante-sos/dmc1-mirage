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

