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
constexpr uintptr_t kEnterMissionRva = 0x24F9C7;
constexpr uintptr_t kEventLoopRva = 0x24F54A;
constexpr uintptr_t kMenuLoopRva = 0x3F7912;
constexpr uintptr_t kMenuConfigPtrRva = 0x60B018;
constexpr uintptr_t kRoomSrcPtrRva = 0x60AF18;
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
    for (int i = 0; i < 6; ++i) {
        emit(cursor, {0x0F, 0x29, static_cast<uint8_t>(0x44 + (i << 3)), 0x24, static_cast<uint8_t>(0x20 + i * 16)});
    }
    emit(cursor, {0x48, 0xB9});
    emit_u64(cursor, reinterpret_cast<uint64_t>(fn));
    emit(cursor, {0xFF, 0xD1});
    for (int i = 0; i < 6; ++i) {
        emit(cursor, {0x0F, 0x28, static_cast<uint8_t>(0x44 + (i << 3)), 0x24, static_cast<uint8_t>(0x20 + i * 16)});
    }
    emit(cursor, {0x48, 0x89, 0xDC});
    emit(cursor, {0x41, 0x5F, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C});
    emit(cursor, {0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58});
    emit(cursor, {0x5F, 0x5E, 0x5D, 0x5B, 0x5A, 0x59, 0x58});
    emit(cursor, {0x9D});
    std::memcpy(cursor, stolen, stolen_len);
    cursor += stolen_len;
    emit(cursor, {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});
    emit_u64(cursor, reinterpret_cast<uint64_t>(ret));
    return cursor;
}

bool patch_jump(uint8_t* site, size_t len, uint8_t* dest) {
    intptr_t rel = dest - (site + 5);
    if (rel > INT32_MAX || rel < INT32_MIN) {
        log_line("jump from %p to %p does not fit", site, dest);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old)) {
        log_line("VirtualProtect failed at %p", site);
        return false;
    }
    site[0] = 0xE9;
    int32_t rel32 = static_cast<int32_t>(rel);
    std::memcpy(site + 1, &rel32, sizeof(rel32));
    for (size_t i = 5; i < len; ++i) {
        site[i] = 0x90;
    }
    VirtualProtect(site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    return true;
}

uint8_t* event_data() {
    auto slot = reinterpret_cast<uint8_t**>(g_base + kEventPtrRva);
    return *slot;
}

void write_u32(uint8_t* address, uint32_t value) {
    std::memcpy(address, &value, sizeof(value));
}

void write_f32(uint8_t* address, float value) {
    std::memcpy(address, &value, sizeof(value));
}

// The save copy and the session reset both run while a mission is starting, and
// whichever runs last was putting mission 1 back. Keep rewriting until that burst ends.
bool claim_pending(int& mission, int& mode) {
    mission = g_pending_mission.load();
    if (mission < 1 || g_applies_left.load() < 1) {
        return false;
    }
    mode = g_pending_mode.load();
    g_applies_left.fetch_sub(1);
    return true;
}

void write_mission_choice(int mission, int mode, uint8_t& previous) {
    uint8_t* data = session();
    if (data) {
        previous = data[kMissionOffset];
        data[kMissionOffset] = static_cast<uint8_t>(mission);
        data[kModeOffset] = static_cast<uint8_t>(mode);
    }
    auto slot = reinterpret_cast<uint8_t**>(g_base + kMenuConfigPtrRva);
    uint8_t* config = slot ? *slot : nullptr;
    if (config) {
        config[0x10] = static_cast<uint8_t>(mission);
        config[0x0B] = static_cast<uint8_t>(mode);
    }
}

void place_mission_spawn(int mission) {
    if (mission < 1 || mission > kMissionSlots) {
        return;
    }
    const uint8_t* entry = reinterpret_cast<uint8_t*>(g_base + kMissionTableRva) + static_cast<uintptr_t>(mission) * 0x20;
    uint32_t stage = 0;
    uint32_t room = 0;
    float pos[4] = {};
    std::memcpy(&stage, entry, sizeof(stage));
    std::memcpy(&room, entry + 4, sizeof(room));
    std::memcpy(pos, entry + 0x10, sizeof(pos));
    uint8_t* event = event_data();
    if (event) {
        write_u32(event + 0x270, stage);
        write_u32(event + 0x274, room);
        write_u32(event + 0x288, stage);
        write_u32(event + 0x28C, room);
    }
    std::memcpy(reinterpret_cast<void*>(g_base + kSpawnPosRva), pos, sizeof(pos));
    uint8_t* body = actor();
    if (body) {
        const float x = pos[0];
        const float y = pos[1];
        const float z = pos[2];
        const float facing = pos[3];
        for (uintptr_t offset : {uintptr_t{0x1AC0}, uintptr_t{0x1E0}, uintptr_t{0x170}}) {
            write_f32(body + offset, x);
        }
        for (uintptr_t offset : {uintptr_t{0x1AC4}, uintptr_t{0x1E4}, uintptr_t{0x174}}) {
            write_f32(body + offset, y);
        }
        for (uintptr_t offset : {uintptr_t{0x1AC8}, uintptr_t{0x1E8}, uintptr_t{0x178}}) {
            write_f32(body + offset, z);
        }
        write_f32(body + 0x124, facing);
    }
    log_line("mission spawn stage %u room %u", stage, room);
}

extern "C" void apply_pending_at_start() {
    int mission = 0;
    int mode = 0;
    if (!claim_pending(mission, mode)) {
        return;
    }
    uint8_t previous = 0;
    write_mission_choice(mission, mode, previous);
    g_stage_tick = GetTickCount();
    g_mission_staged.store(true);
    log_line("mission staged from %u to %d mode %d", previous, mission, mode);
}

extern "C" void apply_pending_at_load() {
    int mission = 0;
    int mode = 0;
    if (!claim_pending(mission, mode)) {
        return;
    }
    uint8_t previous = 0;
    write_mission_choice(mission, mode, previous);
    uint8_t* data = session();
    if (data) {
        uint32_t flags = 0;
        std::memcpy(&flags, data + kEventOffset, sizeof(flags));
        flags |= 0x800;
        std::memcpy(data + kEventOffset, &flags, sizeof(flags));
    }
    place_mission_spawn(mission);
    log_line("mission load overridden from %u to %d mode %d", previous, mission, mode);
}

const char* name_for(const uint8_t* ids, const char* const* names, size_t count, uint8_t id) {
    for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return names[i];
        }
    }
    return "unknown";
}

void switch_melee() {
    uint8_t* body = actor();
    if (!body) {
        return;
    }
    uint8_t current = body[kMeleeOffset];
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < std::size(kMeleeIds); ++i) {
        if (kMeleeIds[i] == current) {
            index = i;
            found = true;
            break;
        }
    }
    if (found) {
        index = (index + 1) % std::size(kMeleeIds);
    }
    uint8_t next = kMeleeIds[index];
    uint8_t form = (next == 3 || next == 2) ? kFormSparda : kFormDefault;
    body[kMeleeOffset] = next;
    body[kMeleeFormOffset] = form;
    log_line("melee %u -> %s", current, name_for(kMeleeIds, kMeleeNames, std::size(kMeleeIds), next));
    auto form_fn = reinterpret_cast<MeleeFormFn>(g_base + kUpdateMeleeFormRva);
    auto weapon_fn = reinterpret_cast<MeleeFn>(g_base + kUpdateMeleeRva);
    form_fn(form);
    weapon_fn(next, 0);
}

void switch_ranged() {
    uint8_t* body = actor();
    if (!body) {
        return;
    }
    uint8_t current = body[kRangedOffset];
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < std::size(kRangedIds); ++i) {
        if (kRangedIds[i] == current) {
            index = i;
            found = true;
            break;
        }
    }
    if (found) {
        index = (index + 1) % std::size(kRangedIds);
    }
    uint8_t next = kRangedIds[index];
    body[kRangedOffset] = next;
    log_line("gun %u -> %s", current, name_for(kRangedIds, kRangedNames, std::size(kRangedIds), next));
    auto weapon_fn = reinterpret_cast<RangedFn>(g_base + kUpdateRangedRva);
    weapon_fn(next);
}

void keep_chosen_mission() {
    int mission = g_pending_mission.load();
    if (mission < 1 || !g_mission_staged.load()) {
        return;
    }
    uint8_t* data = session();
    if (data && data[kMissionOffset] != static_cast<uint8_t>(mission) && g_mission_restores < 5) {
        int mode = g_pending_mode.load();
        uint8_t previous = 0;
        write_mission_choice(mission, mode, previous);
        if (data) {
            uint32_t flags = 0;
            std::memcpy(&flags, data + kEventOffset, sizeof(flags));
            flags |= 0x800;
            std::memcpy(data + kEventOffset, &flags, sizeof(flags));
        }
        place_mission_spawn(mission);
        g_mission_restores++;
        log_line("mission restored from %u to %d", previous, mission);
    }
    if (in_game() && g_stage_tick != 0 && static_cast<int32_t>(GetTickCount() - g_stage_tick) > 8000) {
        g_pending_mission.store(-1);
        g_mission_staged.store(false);
        log_line("mission override finished");
    }
}

extern "C" void apply_weapon_requests() {
    keep_chosen_mission();
    if (!in_game()) {
        g_want_melee.store(false);
        g_want_ranged.store(false);
        return;
    }
    if (g_want_melee.exchange(false)) {
        switch_melee();
    }
    if (g_want_ranged.exchange(false)) {
        switch_ranged();
    }
}

void watch_trigger(BYTE& trigger, TriggerWatch& watch, std::atomic<bool>& request) {
    bool pressed = trigger >= static_cast<BYTE>(g_trigger_threshold);
    DWORD now = GetTickCount();
    if (pressed) {
        if (!watch.down) {
            watch.down = true;
            watch.tick = now;
            watch.passed = false;
        }
        if (!watch.passed && static_cast<int>(now - watch.tick) >= g_tap_ms) {
            watch.passed = true;
        }
        if (!watch.passed) {
            trigger = 0;
        }
        return;
    }
    if (watch.down && !watch.passed && in_game()) {
        request.store(true);
    }
    watch.down = false;
    watch.passed = false;
}

bool expect_bytes(void* address, const uint8_t* bytes, size_t len, const char* label) {
    if (std::memcmp(address, bytes, len) == 0) {
        return true;
    }
    log_line("%s bytes do not match, hook skipped", label);
    return false;
}

void install_hooks() {
    const uint8_t main_bytes[] = {0x41, 0x8B, 0x93, 0x9C, 0x00, 0x00, 0x00};
    const uint8_t load_bytes[] = {0x66, 0x89, 0x81, 0xCE, 0x1B, 0x00, 0x00};
    const uint8_t start_bytes[] = {0x0F, 0xB6, 0x41, 0x10, 0x88, 0x85, 0x64, 0x1E, 0x00, 0x00};
    const uint8_t melee_bytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08};
    const uint8_t ranged_bytes[] = {0x85, 0xC9};
    auto* main_site = reinterpret_cast<uint8_t*>(g_base + kMainLoopRva);
    auto* load_site = reinterpret_cast<uint8_t*>(g_base + kLoadSaveRva);
    auto* start_site = reinterpret_cast<uint8_t*>(g_base + kStartMissionRva);
    if (!expect_bytes(main_site, main_bytes, sizeof(main_bytes), "main loop") ||
        !expect_bytes(load_site, load_bytes, sizeof(load_bytes), "mission load") ||
        !expect_bytes(start_site, start_bytes, sizeof(start_bytes), "mission start") ||
        !expect_bytes(reinterpret_cast<void*>(g_base + kUpdateMeleeRva), melee_bytes, sizeof(melee_bytes), "melee update") ||
        !expect_bytes(reinterpret_cast<void*>(g_base + kUpdateRangedRva), ranged_bytes, sizeof(ranged_bytes), "gun update")) {
        return;
    }
    uint8_t* page = alloc_near(g_base + kMainLoopRva, 0x1000);
    if (!page) {
        log_line("could not allocate hook memory near dmc1.exe");
        return;
    }
    uint8_t* cursor = page;
    uint8_t* main_stub = cursor;
    cursor = write_stub(cursor, apply_weapon_requests, main_bytes, sizeof(main_bytes), main_site + sizeof(main_bytes));
    uint8_t* load_stub = cursor;
    cursor = write_stub(cursor, apply_pending_at_load, load_bytes, sizeof(load_bytes), load_site + sizeof(load_bytes));
    uint8_t* start_stub = cursor;
    cursor = write_stub(cursor, apply_pending_at_start, start_bytes, sizeof(start_bytes), start_site + sizeof(start_bytes));
    if (!patch_jump(main_site, sizeof(main_bytes), main_stub) || !patch_jump(load_site, sizeof(load_bytes), load_stub) ||
        !patch_jump(start_site, sizeof(start_bytes), start_stub)) {
        log_line("hook patch failed");
        return;
    }
    g_hooks_ready = true;
    log_line("hooks installed main=%p load=%p start=%p", main_stub, load_stub, start_stub);
}

}  // namespace

void mod_arm_mission(int mission, int mode) {
    g_mission_staged.store(false);
    g_stage_tick = 0;
    g_mission_restores = 0;
    g_pending_mode.store(mode);
    g_applies_left.store(8);
    g_pending_mission.store(mission);
    log_line("armed mission %d mode %d", mission, mode);
}

void mod_clear_mission() {
    g_pending_mission.store(-1);
    log_line("mission override cleared");
}

int mod_armed_mission() {
    return g_pending_mission.load();
}

int mod_armed_mode() {
    return g_pending_mode.load();
}

bool mod_in_game() {
    return in_game();
}

void mod_start() {
    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) {
        wcscpy(slash + 1, L"dmc1-switch.log");
    }
    g_log = _wfopen(path, L"a");
    read_ini();
    log_line("DMC1 switch loaded base=%p tap_ms=%d", reinterpret_cast<void*>(g_base), g_tap_ms);
    install_hooks();
    overlay_install();
}

void mod_on_get_state(DWORD user, XInputState* state) {
    if (user != 0) {
        return;
    }
    overlay_on_pad(state);
    if (!state || !g_hooks_ready) {
        return;
    }
    watch_trigger(state->gamepad.leftTrigger, g_left, g_want_melee);
    watch_trigger(state->gamepad.rightTrigger, g_right, g_want_ranged);
}
