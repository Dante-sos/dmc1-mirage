#include "the_void.h"
#include "the_void_menu.h"
#include "mod.h"

#include <atomic>
#include <cstring>
#include <algorithm>
#include <initializer_list>

namespace {

constexpr uintptr_t kMissionTableRva      = 0x560170;
constexpr uintptr_t kEnemyCountRva        = 0x4CB6BA0;
constexpr uintptr_t kEnemyListRva         = 0x4CB6BB0;
constexpr uintptr_t kBriefingJumpRva      = 0x24FE5E;
constexpr uintptr_t kRoomTableEntry22Rva  = 0x5165F8;
constexpr uintptr_t kRoomTableEntry31Rva  = 0x5166D0;
constexpr uintptr_t kStage1Room22FileRva  = 0x5A45BC;
constexpr uintptr_t kBladeMissionCheckRva = 0x36E4DE;
constexpr uintptr_t kWaveSpawnFnRva       = 0x247020;
constexpr uintptr_t kBladeWaveDescRva     = 0x58ED00;

constexpr uint32_t kVoidStage = 1;
constexpr uint32_t kVoidRoom  = 22;
constexpr float kVoidSpawnPos[4] = {7927.5f, -120.5f, -6064.5f, 1.05f};

// Native Mission 8 starting configuration in r11d.fsd (Room 22 / 0x16)
constexpr uint32_t kOrigStage = 1;
constexpr uint32_t kOrigRoom  = 22;
constexpr float kOrigSpawnPos[4] = {5000.0f, 150.0f, -2750.0f, -1.72f};

// At RVA 0x24FE5E in dmc1.exe:
// Original: 0F 84 B7 00 00 00  (je 0x14024ff1b)
// Patched:  E9 B8 00 00 00 90  (jmp 0x14024ff1b; nop)
const uint8_t kOrigBriefingJump[6]    = {0x0F, 0x84, 0xB7, 0x00, 0x00, 0x00};
const uint8_t kPatchedBriefingJump[6] = {0xE9, 0xB8, 0x00, 0x00, 0x00, 0x90};

// At RVA 0x36E4DE in dmc1.exe:
// Original: 72 7A  (jb 0x14036e55a; skips Blade wave spawn if Mission < 11)
// Patched:  90 90  (nop; nop; allows Blade wave spawn in Mission 8)
const uint8_t kOrigBladeCheck[2]    = {0x72, 0x7A};
const uint8_t kPatchedBladeCheck[2] = {0x90, 0x90};

uint8_t  g_saved_room_entry_22[24] = {};
bool     g_room_entry_saved = false;
uint16_t g_saved_stage1_file_22 = 0;
bool     g_stage1_file_saved = false;

std::atomic<bool> g_void_active{false};
std::atomic<int>  g_void_mode{3};
std::atomic<int>  g_initial_spawn_counter{0};
bool g_prev_f5 = false;
bool g_prev_back_button = false;
bool g_prev_sticks = false;
uint16_t s_last_enemy_hp[64] = {};

void write_f32(uint8_t* address, float value) {
    std::memcpy(address, &value, sizeof(value));
}

}  // namespace

void void_reset_player() {
    uint8_t* player = mod_actor();
    if (!player) return;

    const float rx = kVoidSpawnPos[0];
    const float ry = kVoidSpawnPos[1];
    const float rz = kVoidSpawnPos[2];
    const float rfacing = kVoidSpawnPos[3];

    for (uintptr_t offset : {uintptr_t{0x1AC0}, uintptr_t{0x1E0}, uintptr_t{0x170}}) {
        write_f32(player + offset, rx);
        write_f32(player + offset + 4, ry);
        write_f32(player + offset + 8, rz);
    }
    write_f32(player + 0x124, rfacing);

    // Clear movement momentum so Dante doesn't slide into walls
    for (uintptr_t offset : {uintptr_t{0x1AC0 + 0x10}, uintptr_t{0x1E0 + 0x10}}) {
        write_f32(player + offset, 0.0f);
        write_f32(player + offset + 4, 0.0f);
        write_f32(player + offset + 8, 0.0f);
    }

    uintptr_t base = mod_base();
    if (base) {
        float* gspawn = reinterpret_cast<float*>(base + 0x27E82A0);
        gspawn[0] = rx;
        gspawn[1] = ry;
        gspawn[2] = rz;
        gspawn[3] = rfacing;

        // Also update RoomSrc slot at RVA 0x60AF18 if allocated
        auto* src_slot = reinterpret_cast<uint8_t**>(base + 0x60AF18);
        uint8_t* src = src_slot ? *src_slot : nullptr;
        if (src) {
            write_f32(src + 0x1E8, rx);
            write_f32(src + 0x1EC, ry);
            write_f32(src + 0x1F0, rz);
            write_f32(src + 0x1F4, rfacing);
        }
    }

    mod_log("the_void: Dante position reset to arena floor (%.1f, %.1f, %.1f)", rx, ry, rz);
}

uintptr_t void_get_subtable_for_type(DummyType type) {
    uintptr_t base = mod_base();
    if (!base) return 0;
    // Room 31 native subtable (0x5116E0) provides rr206_1 which safely instantiates Blade enemies in r206.fsd chamber
    return base + 0x5116E0;
}

void void_apply_dummy_subtable(DummyType type) {
    uintptr_t base = mod_base();
    if (!base) return;
    auto* entry22 = reinterpret_cast<uint8_t*>(base + kRoomTableEntry22Rva);
    const auto* entry31 = reinterpret_cast<const uint8_t*>(base + kRoomTableEntry31Rva);
    if (!entry22 || !entry31) return;
    uintptr_t sub_tbl = *reinterpret_cast<const uint64_t*>(entry31 + 16);
    DWORD old_prot = 0;
    if (VirtualProtect(entry22 + 16, sizeof(uint64_t), PAGE_EXECUTE_READWRITE, &old_prot)) {
        *reinterpret_cast<uint64_t*>(entry22 + 16) = sub_tbl;
        VirtualProtect(entry22 + 16, sizeof(uint64_t), old_prot, &old_prot);
        mod_log("the_void: applied dummy subtable %p for type %d", reinterpret_cast<void*>(sub_tbl), static_cast<int>(type));
    }
}

void void_patch_room_tables(bool enable) {
    uintptr_t base = mod_base();
    if (!base) return;

    // 1. Table 1: Room Descriptor Table (RVA 0x5163E8)
    // Entry 22 at base + 0x5165F8 (24 bytes) - originally r11d.fsd
    // Entry 31 at base + 0x5166D0 (24 bytes) - r206.fsd Rusty Key Chamber descriptor
    auto* entry22 = reinterpret_cast<uint8_t*>(base + kRoomTableEntry22Rva);
    const auto* entry31 = reinterpret_cast<const uint8_t*>(base + kRoomTableEntry31Rva);
    if (entry22 && entry31) {
        DWORD old_prot = 0;
        if (VirtualProtect(entry22, 24, PAGE_EXECUTE_READWRITE, &old_prot)) {
            if (enable) {
                if (!g_room_entry_saved) {
                    std::memcpy(g_saved_room_entry_22, entry22, 24);
                    g_room_entry_saved = true;
                }
                // Copy authentic Room 31 descriptor (r206.fsd geometry + r207.fsd Blade subtable)
                std::memcpy(entry22, entry31, 24);
                mod_log("the_void: patched Room Table Entry 22 with Entry 31 descriptor (files=r206.fsd, subtable=%p)",
                        reinterpret_cast<void*>(*reinterpret_cast<uint64_t*>(entry22 + 16)));
            } else if (g_room_entry_saved) {
                std::memcpy(entry22, g_saved_room_entry_22, 24);
                mod_log("the_void: restored Room Table Entry 22");
            }
            VirtualProtect(entry22, 24, old_prot, &old_prot);
        }
    }

    // 2. Table 2: Stage 1 File Index Table (RVA 0x5A4590)
    // Index 22 at base + 0x5A45BC (uint16_t)
    // Needs to be 239 (r206.fsd) so the collision & geometry load properly
    auto* file_idx22 = reinterpret_cast<uint16_t*>(base + kStage1Room22FileRva);
    if (file_idx22) {
        DWORD old_prot = 0;
        if (VirtualProtect(file_idx22, sizeof(uint16_t), PAGE_EXECUTE_READWRITE, &old_prot)) {
            if (enable) {
                if (!g_stage1_file_saved) {
                    g_saved_stage1_file_22 = *file_idx22;
                    g_stage1_file_saved = true;
                }
                *file_idx22 = 239;
                mod_log("the_void: patched Stage 1 Room 22 file index to 239 (r206.fsd Rusty Key Chamber)");
            } else if (g_stage1_file_saved) {
                *file_idx22 = g_saved_stage1_file_22;
                mod_log("the_void: restored Stage 1 Room 22 file index to %u", g_saved_stage1_file_22);
            }
            VirtualProtect(file_idx22, sizeof(uint16_t), old_prot, &old_prot);
        }
    }
}

void void_init() {
    g_void_active.store(false);
    g_void_mode.store(3);
    g_initial_spawn_counter.store(0);
    g_prev_f5 = false;
    g_prev_back_button = false;
    g_prev_sticks = false;
    std::memset(s_last_enemy_hp, 0, sizeof(s_last_enemy_hp));
}

void void_arm(int mode) {
    g_void_active.store(true);
    g_void_mode.store(mode);
    g_initial_spawn_counter.store(15);
    g_prev_sticks = false;
    std::memset(s_last_enemy_hp, 0, sizeof(s_last_enemy_hp));
    void_patch_mission_table(true);
    void_patch_briefing_skip(true);
    void_patch_spawn_check(true);
    mod_log("the_void: armed practice arena (mission 8, r206.fsd Rusty Key Drainage Chamber, mode %d)", mode);
}

void void_disarm() {
    if (g_void_active.load()) {
        void_patch_mission_table(false);
        void_patch_briefing_skip(false);
        void_patch_spawn_check(false);
    }
    g_void_active.store(false);
    g_initial_spawn_counter.store(0);
    mod_log("the_void: disarmed");
}

bool void_is_active() {
    return g_void_active.load();
}

uint32_t void_get_stage() {
    return kVoidStage;
}

uint32_t void_get_room() {
    return kVoidRoom;
}

void void_get_spawn_pos(float out_pos[4]) {
    std::memcpy(out_pos, kVoidSpawnPos, sizeof(kVoidSpawnPos));
}

void void_patch_mission_table(bool enable) {
    uintptr_t base = mod_base();
    if (!base) return;

    uint8_t* table = reinterpret_cast<uint8_t*>(base + kMissionTableRva);
    if (!table) return;

    uint8_t* entry8 = table + 8 * 0x20;
    DWORD old_prot = 0;
    if (VirtualProtect(entry8, 0x20, PAGE_EXECUTE_READWRITE, &old_prot)) {
        if (enable) {
            uint32_t stage = kVoidStage;
            uint32_t room  = kVoidRoom;
            std::memcpy(entry8, &stage, sizeof(stage));
            std::memcpy(entry8 + 4, &room, sizeof(room));
            std::memcpy(entry8 + 0x10, kVoidSpawnPos, sizeof(kVoidSpawnPos));
            mod_log("the_void: mission table entry 8 patched: stage=%u room=%u (r206.fsd Rusty Key Drainage Chamber)", stage, room);
        } else {
            uint32_t stage = kOrigStage;
            uint32_t room  = kOrigRoom;
            std::memcpy(entry8, &stage, sizeof(stage));
            std::memcpy(entry8 + 4, &room, sizeof(room));
            std::memcpy(entry8 + 0x10, kOrigSpawnPos, sizeof(kOrigSpawnPos));
            mod_log("the_void: mission table entry 8 restored: stage=%u room=%u", stage, room);
        }
        VirtualProtect(entry8, 0x20, old_prot, &old_prot);
    }

    void_patch_room_tables(enable);
}

void void_patch_briefing_skip(bool enable) {
    uintptr_t base = mod_base();
    if (!base) return;

    uint8_t* site = reinterpret_cast<uint8_t*>(base + kBriefingJumpRva);
    DWORD old_prot = 0;
    if (VirtualProtect(site, sizeof(kOrigBriefingJump), PAGE_EXECUTE_READWRITE, &old_prot)) {
        if (enable) {
            std::memcpy(site, kPatchedBriefingJump, sizeof(kPatchedBriefingJump));
            mod_log("the_void: patched briefing jump at %p to unconditional jmp (bypasses briefing/shop)", site);
        } else {
            std::memcpy(site, kOrigBriefingJump, sizeof(kOrigBriefingJump));
            mod_log("the_void: restored briefing jump at %p to je", site);
        }
        VirtualProtect(site, sizeof(kOrigBriefingJump), old_prot, &old_prot);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(kOrigBriefingJump));
    }
}

void void_patch_spawn_check(bool enable) {
    uintptr_t base = mod_base();
    if (!base) return;

    uint8_t* site = reinterpret_cast<uint8_t*>(base + kBladeMissionCheckRva);
    DWORD old_prot = 0;
    if (VirtualProtect(site, sizeof(kOrigBladeCheck), PAGE_EXECUTE_READWRITE, &old_prot)) {
        if (enable) {
            std::memcpy(site, kPatchedBladeCheck, sizeof(kPatchedBladeCheck));
            mod_log("the_void: patched Blade mission check at %p to NOPs (allows spawn in mission 8)", site);
        } else {
            std::memcpy(site, kOrigBladeCheck, sizeof(kOrigBladeCheck));
            mod_log("the_void: restored Blade mission check at %p to jb", site);
        }
        VirtualProtect(site, sizeof(kOrigBladeCheck), old_prot, &old_prot);
        FlushInstructionCache(GetCurrentProcess(), site, sizeof(kOrigBladeCheck));
    }
}

void void_on_menu_loop(uint8_t* config) {
    if (!config || !g_void_active.load()) return;

    // If the game enters Title State 6 (Mission Briefing / Shop menu),
    // instantly advance it to State 7 so no briefing card or shop screen ever appears!
    if (config[0] == 6) {
        config[0x0D] = 0; // 0 = New Game (Direct Launch, never load save screen)
        config[0x10] = 8; // Mission 8
        config[0x0B] = static_cast<uint8_t>(g_void_mode.load());
        config[1] = 0;
        *reinterpret_cast<uint32_t*>(config) = 7;
        mod_log("the_void: bypassed title state 6 -> direct state 7 launch");
    }
}

void void_on_menu_launch(uint8_t* config) {
    if (!config) return;

    // Launch directly to State 7 with config[0x0D] = 0 (New Game launch, bypasses briefing/shop/load)
    config[0x10] = 8; // Mission 8
    config[0x0B] = static_cast<uint8_t>(g_void_mode.load());
    config[0x0D] = 0; // Crucial: 0 = direct launch, 1 = save load picker
    config[1] = 0;
    *reinterpret_cast<uint32_t*>(config) = 7;
    mod_log("the_void: launched directly into State 7 (mission 8, mode %d)", g_void_mode.load());
}

void void_on_pad(XInputState* state) {
    if (!g_void_active.load() || !mod_in_game() || !state) return;

    // 1. Check L3 + R3 (Thumbstick clicks: 0x0040 | 0x0080) to toggle Void Menu
    bool sticks_pressed = (state->gamepad.buttons & (0x0040 | 0x0080)) == (0x0040 | 0x0080);
    if (sticks_pressed && !g_prev_sticks) {
        void_menu_toggle();
    }
    g_prev_sticks = sticks_pressed;
    if (sticks_pressed) {
        state->gamepad.buttons &= ~(0x0040 | 0x0080);
    }

    // 2. Check Back / Select button (0x0020) for Quick Dummy Reset
    bool back_pressed = (state->gamepad.buttons & 0x0020) != 0;
    if (back_pressed && !g_prev_back_button) {
        void_menu_reset_dummy();
    }
    g_prev_back_button = back_pressed;

    // Mask out Back button while in The Void so the map/status screen does not pop up
    if (back_pressed) {
        state->gamepad.buttons &= ~0x0020;
    }

    // 3. While Void Menu is open, block gameplay actions
    if (void_menu_is_open()) {
        state->gamepad.buttons &= (0x0040 | 0x0080 | 0x0020);
        state->gamepad.leftX = 0;
        state->gamepad.leftY = 0;
        state->gamepad.rightX = 0;
        state->gamepad.rightY = 0;
        state->gamepad.leftTrigger = 0;
        state->gamepad.rightTrigger = 0;
    }
}

void void_update_frame() {
    if (!g_void_active.load() || !mod_in_game()) return;

    // Update telemetry timer expiration
    void_menu_update();

    uint8_t* player = mod_actor();
    if (player) {
        // Enforce spawn position continuously during the first 15 frames of entering the arena
        int remaining = g_initial_spawn_counter.load();
        if (remaining > 0) {
            g_initial_spawn_counter.store(remaining - 1);
            void_reset_player();
            if (remaining == 1) {
                uintptr_t base_c = mod_base();
                if (base_c) {
                    int32_t c = *reinterpret_cast<int32_t*>(base_c + kEnemyCountRva);
                    if (c <= 0) {
                        typedef void (__fastcall* WaveSpawnFn)(void* wave_desc);
                        auto wave_fn = reinterpret_cast<WaveSpawnFn>(base_c + kWaveSpawnFnRva);
                        wave_fn(reinterpret_cast<void*>(base_c + kBladeWaveDescRva));
                        c = *reinterpret_cast<int32_t*>(base_c + kEnemyCountRva);
                        mod_log("the_void: stabilized frame fallback spawned wave (count=%d)", c);
                    }
                    if (c > 0) {
                        void_menu_reset_dummy();
                    }
                }
            }
        }

        // 1. Maintain full HP for Dante (God Mode in The Void)
        if (g_void_settings.godmode_player) {
            uint16_t max_hp = *reinterpret_cast<uint16_t*>(player + 0x1BD0);
            if (max_hp == 0 || max_hp > 30000) {
                max_hp = 3000;
            }
            *reinterpret_cast<uint16_t*>(player + 0x1BCE) = max_hp;
        }

        // 2. Maintain Infinite Devil Trigger (Full 10 Runes)
        if (g_void_settings.infinite_dt) {
            uint16_t max_dt = *reinterpret_cast<uint16_t*>(player + 0x1BD4);
            if (max_dt < 1000) {
                max_dt = 1000;
                *reinterpret_cast<uint16_t*>(player + 0x1BD4) = 1000; // Unlock all 10 DT runes
            }
            *reinterpret_cast<uint16_t*>(player + 0x1BD2) = max_dt;
        }

        // 3. F5 / Home key shortcut to quick-reset dummy position in front of Dante
        bool key_f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (key_f5 && !g_prev_f5) {
            void_menu_reset_dummy();
        }
        g_prev_f5 = key_f5;
    }

    // 4. Enemy Dummy Management (Damage Telemetry, Immortal HP, Stand Still AI, Infinite Juggle)
    uintptr_t base = mod_base();
    if (base) {
        int32_t count = *reinterpret_cast<int32_t*>(base + kEnemyCountRva);
        auto** enemy_list = reinterpret_cast<uint8_t**>(base + kEnemyListRva);
        if (count > 0 && count <= 64 && enemy_list) {
            int max_dummies = std::clamp(g_void_settings.dummy_count, 1, 3);
            for (int i = 0; i < count; ++i) {
                uint8_t* e = enemy_list[i];
                if (!e || e == player) continue;
                auto ptr_val = reinterpret_cast<uintptr_t>(e);
                if (ptr_val < 0x10000 || ptr_val > 0x7FFFFFFFFFFFull || (ptr_val & 7) != 0) continue;

                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery(e, &mbi, sizeof(mbi)) != sizeof(mbi) ||
                    mbi.State != MEM_COMMIT ||
                    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
                    continue;
                }

                if (i >= max_dummies) {
                    // Suppress extra dummy beyond selected count
                    *reinterpret_cast<uint16_t*>(e + 0x1BCE) = 0;
                    *reinterpret_cast<uint16_t*>(e + 0x17A2) = 0;
                    continue;
                }

                uint16_t* ehp = reinterpret_cast<uint16_t*>(e + 0x1BCE);
                uint16_t* emax = reinterpret_cast<uint16_t*>(e + 0x1BD0);
                uint16_t alive_flag = *reinterpret_cast<uint16_t*>(e + 0x17A2);

                // A. Damage Telemetry tracking
                if (s_last_enemy_hp[i] > 0 && *ehp < s_last_enemy_hp[i]) {
                    float dmg = static_cast<float>(s_last_enemy_hp[i] - *ehp) * g_void_settings.damage_mult;
                    void_menu_on_enemy_damage(dmg);
                }

                // B. Immortal Dummy HP lock (only if enemy is alive: alive_flag != 0)
                if (g_void_settings.immortal_dummy && alive_flag != 0) {
                    uint16_t target_hp = static_cast<uint16_t>(g_void_settings.dummy_hp);
                    if (*ehp < 1000 || *ehp == 0) {
                        *ehp = target_hp;
                        if (*emax < target_hp) *emax = target_hp;
                    }
                }
                s_last_enemy_hp[i] = *ehp;

                // C. Stand Still AI (Punching Bag / Sandbag)
                float* pos_y = reinterpret_cast<float*>(e + 0x1AC4);
                if (g_void_settings.dummy_ai == DummyAiMode::StandStill) {
                    // Freeze horizontal wander when on the arena floor (NEVER touch 0x170/0x180!)
                    if (*pos_y <= -110.0f) {
                        *reinterpret_cast<float*>(e + 0x1AC0 + 0x10) = 0.0f; // vel X
                        *reinterpret_cast<float*>(e + 0x1AC0 + 0x18) = 0.0f; // vel Z
                        *reinterpret_cast<float*>(e + 0x1E0 + 0x10) = 0.0f;
                        *reinterpret_cast<float*>(e + 0x1E0 + 0x18) = 0.0f;
                    }
                }

                // D. Infinite Juggle / Floatier Air Physics
                if (g_void_settings.infinite_juggle) {
                    if (*pos_y > -110.0f) { // Airborne
                        float* vel_y = reinterpret_cast<float*>(e + 0x1AC0 + 0x14);
                        float* phys_vel_y = reinterpret_cast<float*>(e + 0x1E0 + 0x14);
                        if (*vel_y < -3.5f) { // Falling fast
                            *vel_y = -3.5f; // Cushion gravity for extended aerial juggles
                            *phys_vel_y = -3.5f;
                        }
                    }
                }
            }
        }
    }
}
