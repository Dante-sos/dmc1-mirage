#include "the_void.h"
#include "mod.h"

#include <atomic>
#include <cstring>
#include <initializer_list>

namespace {

constexpr uintptr_t kMissionTableRva      = 0x560170;
constexpr uintptr_t kEnemyCountRva        = 0x4CB6BA0;
constexpr uintptr_t kEnemyListRva         = 0x4CB6BB0;
constexpr uintptr_t kBriefingJumpRva      = 0x24FE5E;
constexpr uintptr_t kRoomTableEntry22Rva  = 0x5165F8;
constexpr uintptr_t kRoomTableEntry24Rva  = 0x516628;
constexpr uintptr_t kStage1Room22FileRva  = 0x5A45BC;

// Mission 8 rooftop arena configuration:
// DMC1 checks at 0x140255D12: "cmp dword ptr [rcx + 0x274], 0x16" (room == 22) to recognize Mission 8.
// By keeping room = 22 and remapping Room 22 to File 240 (r121.fsd) in Table 2
// and Entry 24 (r121.fsd Rooftop Arena / Phantom boss descriptor) in Table 1,
// the engine natively runs Mission 8, loads the rooftop arena, spawns Phantom, and never falls back to Mission 1.
constexpr uint32_t kVoidStage = 1;
constexpr uint32_t kVoidRoom  = 22;
constexpr float kVoidSpawnPos[4] = {0.0f, 10.0f, 0.0f, 3.01f};

// Native Mission 8 starting configuration in r11d.fsd (Room 22 / 0x16)
constexpr uint32_t kOrigStage = 1;
constexpr uint32_t kOrigRoom  = 22;
constexpr float kOrigSpawnPos[4] = {5000.0f, 150.0f, -2750.0f, -1.72f};

// At RVA 0x24FE5E in dmc1.exe:
// Original: 0F 84 B7 00 00 00  (je 0x14024ff1b)
// Patched:  E9 B8 00 00 00 90  (jmp 0x14024ff1b; nop)
const uint8_t kOrigBriefingJump[6]    = {0x0F, 0x84, 0xB7, 0x00, 0x00, 0x00};
const uint8_t kPatchedBriefingJump[6] = {0xE9, 0xB8, 0x00, 0x00, 0x00, 0x90};

uint8_t  g_saved_room_entry_22[24] = {};
bool     g_room_entry_saved = false;
uint16_t g_saved_stage1_file_22 = 0;
bool     g_stage1_file_saved = false;

std::atomic<bool> g_void_active{false};
std::atomic<int>  g_void_mode{3};
bool g_prev_f5 = false;
bool g_prev_back_button = false;

void write_f32(uint8_t* address, float value) {
    std::memcpy(address, &value, sizeof(value));
}

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
    mod_log("the_void: Dante position reset to arena center (0, 10, 0)");
}

void void_patch_room_tables(bool enable) {
    uintptr_t base = mod_base();
    if (!base) return;

    // 1. Table 1: Room Descriptor Table (RVA 0x5163E8)
    // Entry 22 at base + 0x5165F8 (24 bytes) - originally r11d.fsd
    // Entry 24 at base + 0x516628 (24 bytes) - r121.fsd rooftop arena & Phantom boss descriptor
    auto* entry22 = reinterpret_cast<uint8_t*>(base + kRoomTableEntry22Rva);
    const auto* entry24 = reinterpret_cast<const uint8_t*>(base + kRoomTableEntry24Rva);
    if (entry22 && entry24) {
        DWORD old_prot = 0;
        if (VirtualProtect(entry22, 24, PAGE_EXECUTE_READWRITE, &old_prot)) {
            if (enable) {
                if (!g_room_entry_saved) {
                    std::memcpy(g_saved_room_entry_22, entry22, 24);
                    g_room_entry_saved = true;
                }
                std::memcpy(entry22, entry24, 24);
                mod_log("the_void: patched Room Table Entry 22 with Entry 24 (r121.fsd rooftop arena descriptor)");
            } else if (g_room_entry_saved) {
                std::memcpy(entry22, g_saved_room_entry_22, 24);
                mod_log("the_void: restored Room Table Entry 22");
            }
            VirtualProtect(entry22, 24, old_prot, &old_prot);
        }
    }

    // 2. Table 2: Stage 1 File Index Table (RVA 0x5A4590)
    // Index 22 at base + 0x5A45BC (uint16_t) - originally 233 (r116.fsd)
    // Needs to be 240 (r121.fsd) so the collision & geometry load properly
    auto* file_idx22 = reinterpret_cast<uint16_t*>(base + kStage1Room22FileRva);
    if (file_idx22) {
        DWORD old_prot = 0;
        if (VirtualProtect(file_idx22, sizeof(uint16_t), PAGE_EXECUTE_READWRITE, &old_prot)) {
            if (enable) {
                if (!g_stage1_file_saved) {
                    g_saved_stage1_file_22 = *file_idx22;
                    g_stage1_file_saved = true;
                }
                *file_idx22 = 240;
                mod_log("the_void: patched Stage 1 Room 22 file index to 240 (r121.fsd)");
            } else if (g_stage1_file_saved) {
                *file_idx22 = g_saved_stage1_file_22;
                mod_log("the_void: restored Stage 1 Room 22 file index to %u", g_saved_stage1_file_22);
            }
            VirtualProtect(file_idx22, sizeof(uint16_t), old_prot, &old_prot);
        }
    }
}

}  // namespace

void void_init() {
    g_void_active.store(false);
    g_void_mode.store(3);
    g_prev_f5 = false;
    g_prev_back_button = false;
}

void void_arm(int mode) {
    g_void_active.store(true);
    g_void_mode.store(mode);
    void_patch_mission_table(true);
    void_patch_briefing_skip(true);
    mod_log("the_void: armed practice arena (mission 8, r121.fsd Rooftop Arena, mode %d)", mode);
}

void void_disarm() {
    if (g_void_active.load()) {
        void_patch_mission_table(false);
        void_patch_briefing_skip(false);
    }
    g_void_active.store(false);
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
            mod_log("the_void: mission table entry 8 patched: stage=%u room=%u (r121.fsd)", stage, room);
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

    // Check Back / Select button (0x0020)
    bool back_pressed = (state->gamepad.buttons & 0x0020) != 0;
    if (back_pressed && !g_prev_back_button) {
        void_reset_player();
    }
    g_prev_back_button = back_pressed;

    // Mask out Back button while in The Void so the map/status screen does not pop up
    if (back_pressed) {
        state->gamepad.buttons &= ~0x0020;
    }
}

void void_update_frame() {
    if (!g_void_active.load() || !mod_in_game()) return;

    uint8_t* player = mod_actor();
    if (player) {
        // 1. Maintain full HP for Dante (God Mode in The Void)
        // Strictly at 0x1BCE (current HP), never touching 0x1BC8 or 0x1C00
        uint16_t max_hp = *reinterpret_cast<uint16_t*>(player + 0x1BD0);
        if (max_hp == 0 || max_hp > 30000) {
            max_hp = 3000;
        }
        *reinterpret_cast<uint16_t*>(player + 0x1BCE) = max_hp;

        // 2. F5 key shortcut to reset position to arena center
        bool key_f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
        if (key_f5 && !g_prev_f5) {
            void_reset_player();
        }
        g_prev_f5 = key_f5;
    }

    // 3. Maintain target / enemy HP so practice session never ends
    uintptr_t base = mod_base();
    if (base) {
        int32_t count = *reinterpret_cast<int32_t*>(base + kEnemyCountRva);
        auto** enemy_list = reinterpret_cast<uint8_t**>(base + kEnemyListRva);
        if (count > 0 && count <= 64 && enemy_list) {
            for (int i = 0; i < count; ++i) {
                uint8_t* e = enemy_list[i];
                if (!e || e == player) continue;
                auto ptr_val = reinterpret_cast<uintptr_t>(e);
                if (ptr_val < 0x10000 || ptr_val > 0x7FFFFFFFFFFFull || (ptr_val & 7) != 0) continue;

                uint16_t* ehp = reinterpret_cast<uint16_t*>(e + 0x1BCE);
                uint16_t* emax = reinterpret_cast<uint16_t*>(e + 0x1BD0);
                if (*ehp > 0 && *ehp < 3000) {
                    *ehp = (*emax > 0 && *emax <= 30000) ? *emax : 5000;
                }
            }
        }
    }
}
