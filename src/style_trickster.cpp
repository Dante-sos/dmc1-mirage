#include "style_trickster.h"
#include "mod.h"

#include <cmath>
#include <cstring>
#include <initializer_list>

namespace {

constexpr uintptr_t kPosBaseRva = 0x1E0;
constexpr uintptr_t kPosVisualRva = 0x170;
constexpr uintptr_t kPosTargetRva = 0x1AC0;
constexpr uintptr_t kFacingRva = 0x124;
constexpr uintptr_t kHpRva = 0x1BC8;
constexpr uintptr_t kHpDisplayRva = 0x17A2;
constexpr uintptr_t kEnemyActiveRva = 0x1C20;
constexpr uintptr_t kEnemyTargetRva = 0x1C18;
constexpr uintptr_t kEnemyCountRva = 0x4CB6BA0;
constexpr uintptr_t kEnemyArrayRva = 0x4CB6BB0;
constexpr uintptr_t kShootTimerRva = 0x1C05;

constexpr int kDashFrames = 10;
constexpr int kDashCooldownFrames = 0;
constexpr int kWarpCooldownFrames = 6;
constexpr int kActionBufferFrames = 8;

int g_dash_timer = 0;
int g_cooldown = 0;
int g_iframe_timer = 0;
uint16_t g_protected_hp = 0;

int g_action_buffer = 0;
bool g_has_buffered_pad = false;
XInputState g_buffered_pad_state{};

float g_dash_dx = 0.0f;
float g_dash_dz = 0.0f;

bool g_is_air_dash = false;
bool g_sky_star_used = false;
const char* g_active_action_name = "";

inline float read_f32(const uint8_t* address) {
    float val = 0.0f;
    std::memcpy(&val, address, sizeof(val));
    return val;
}

inline void write_f32(uint8_t* address, float val) {
    std::memcpy(address, &val, sizeof(val));
}

void write_coords(uint8_t* body, float x, float y, float z) {
    for (uintptr_t off : {kPosBaseRva, kPosVisualRva, kPosTargetRva}) {
        write_f32(body + off, x);
        write_f32(body + off + 4, y);
        write_f32(body + off + 8, z);
    }
}

void write_horizontal_coords(uint8_t* body, float x, float z) {
    for (uintptr_t off : {kPosBaseRva, kPosVisualRva, kPosTargetRva}) {
        write_f32(body + off, x);
        write_f32(body + off + 8, z);
    }
}

void clear_shoot_state(uint8_t* body) {
    if (body) {
        body[kShootTimerRva] = 0;
    }
}

bool is_valid_living_enemy(uint8_t* e, uint8_t* player) {
    if (!e || e == player) {
        return false;
    }
    auto ptr_val = reinterpret_cast<uintptr_t>(e);
    if (ptr_val < 0x10000 || ptr_val > 0x7FFFFFFFFFFFull || (ptr_val & 7) != 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(e, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    uint16_t hp = *reinterpret_cast<const uint16_t*>(e + kHpRva);
    if (hp == 0 || hp > 65000) {
        return false;
    }
    float x = read_f32(e + kPosBaseRva);
    float y = read_f32(e + kPosBaseRva + 4);
    float z = read_f32(e + kPosBaseRva + 8);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        return false;
    }
    return true;
}

uint8_t* find_target_enemy(uint8_t* body) {
    if (!body) {
        return nullptr;
    }

    // 1. Primary active lock-on target
    auto* target = *reinterpret_cast<uint8_t**>(body + kEnemyActiveRva);
    if (is_valid_living_enemy(target, body)) {
        return target;
    }

    // 2. Secondary attack target
    target = *reinterpret_cast<uint8_t**>(body + kEnemyTargetRva);
    if (is_valid_living_enemy(target, body)) {
        return target;
    }

    // 3. Scan the room's active enemy table for the closest living enemy in front of Dante
    uintptr_t base = mod_base();
    if (base) {
        int32_t count = *reinterpret_cast<int32_t*>(base + kEnemyCountRva);
        auto** enemy_list = reinterpret_cast<uint8_t**>(base + kEnemyArrayRva);
        if (count > 0 && count <= 64 && enemy_list) {
            float cur_x = read_f32(body + kPosBaseRva);
            float cur_z = read_f32(body + kPosBaseRva + 8);
            float facing = read_f32(body + kFacingRva);
            float f_dx = std::sin(facing);
            float f_dz = std::cos(facing);

            uint8_t* best = nullptr;
            float best_dist = 4000.0f;

            for (int i = 0; i < count; ++i) {
                uint8_t* e = enemy_list[i];
                if (!is_valid_living_enemy(e, body)) {
                    continue;
                }

                float ex = read_f32(e + kPosBaseRva);
                float ez = read_f32(e + kPosBaseRva + 8);
                float dx = ex - cur_x;
                float dz = ez - cur_z;
                float dist = std::hypot(dx, dz);
                if (dist < 40.0f || dist > 3500.0f) {
                    continue;
                }

                float dir_x = dx / dist;
                float dir_z = dz / dist;
                float dot = dir_x * f_dx + dir_z * f_dz;

                // Prioritize enemies roughly in front of Dante
                if (dot > -0.2f && dist < best_dist) {
                    best_dist = dist;
                    best = e;
                }
            }
            if (best) {
                return best;
            }
        }
    }
    return nullptr;
}

float get_dash_speed(int step) {
    // 10 frame burst: 38 -> 28 -> 18 units/frame (total ~280 units).
    // Fast, responsive, stylish dash that reliably stays safely under
    // Dante's capsule radius (64 units) per frame to guarantee no clipping.
    if (step <= 3) {
        return 38.0f;
    }
    if (step <= 7) {
        return 28.0f;
    }
    return 18.0f;
}

void trickster_execute_action() {
    uint8_t* body = mod_actor();
    if (!body) {
        return;
    }

    // Immediately cancel any active shooting animation
    clear_shoot_state(body);

    float facing = read_f32(body + kFacingRva);
    float cur_y = read_f32(body + kPosBaseRva + 4);
    float floor_y = read_f32(body + kPosTargetRva + 4);
    bool airborne = (cur_y - floor_y) > 65.0f;

    bool lock_on = false;
    SHORT stick_y = 0;
    SHORT stick_x = 0;

    if (g_has_buffered_pad) {
        WORD buttons = g_buffered_pad_state.gamepad.buttons;
        lock_on = (buttons & 0x0200) != 0; // RB in XInput is 0x0200 (Right Shoulder)
        stick_y = g_buffered_pad_state.gamepad.leftY;
        stick_x = g_buffered_pad_state.gamepad.leftX;
    }

    // Keyboard lock-on (Space / Shift) and direction (W/S/A/D or Arrows)
    if ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
        lock_on = true;
    }
    if ((GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState(VK_UP) & 0x8000) != 0) {
        stick_y = 25000;
    } else if ((GetAsyncKeyState('S') & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) {
        stick_y = -25000;
    }
    if ((GetAsyncKeyState('D') & 0x8000) != 0 || (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
        stick_x = 25000;
    } else if ((GetAsyncKeyState('A') & 0x8000) != 0 || (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) {
        stick_x = -25000;
    }

    // Record HP for i-frames invulnerability
    g_protected_hp = *reinterpret_cast<uint16_t*>(body + kHpRva);

    bool forward_pressed = stick_y > 15000;
    bool back_pressed = stick_y < -15000;

    // 1. Air Trick (Forward Warp) -> Lock-on + Forward + Style Action
    if (lock_on && forward_pressed) {
        float cur_x = read_f32(body + kPosBaseRva);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        // Find locked-on or nearest living enemy
        uint8_t* enemy = find_target_enemy(body);
        if (enemy) {
            float ex = read_f32(enemy + kPosBaseRva);
            float ey = read_f32(enemy + kPosBaseRva + 4);
            float ez = read_f32(enemy + kPosBaseRva + 8);
            float edx = ex - cur_x;
            float edz = ez - cur_z;
            float edist = std::hypot(edx, edz);
            if (edist > 60.0f) {
                // Warp directly in front of the enemy (85 units: point blank melee range)
                float stop_dist = edist > 85.0f ? (edist - 85.0f) : 0.0f;
                float target_x = cur_x + (edx / edist) * stop_dist;
                float target_z = cur_z + (edz / edist) * stop_dist;
                float target_y = ey;

                write_coords(body, target_x, target_y, target_z);
                write_f32(body + kFacingRva, std::atan2(edx, edz));
                g_iframe_timer = 12;
                g_cooldown = kWarpCooldownFrames;
                g_active_action_name = "AIR TRICK";
                mod_log("trickster: Air Trick warped directly to enemy dist=%.1f", edist);
                return;
            }
        }

        // If no enemy is targeted or nearby, warp forward 750 units across the room
        float warp_dist = 750.0f;
        float target_x = cur_x + std::sin(facing) * warp_dist;
        float target_z = cur_z + std::cos(facing) * warp_dist;

        write_coords(body, target_x, cur_y, target_z);
        g_iframe_timer = 10;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "AIR TRICK";
        mod_log("trickster: Air Trick forward warp %.1f (no target)", warp_dist);
        return;
    }

    // 2. Trick Down (Retreat Warp) -> Lock-on + Back + Style Action
    if (lock_on && back_pressed) {
        float cur_x = read_f32(body + kPosBaseRva);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        float warp_dist = 450.0f;
        float target_x = cur_x - std::sin(facing) * warp_dist;
        float target_z = cur_z - std::cos(facing) * warp_dist;

        write_coords(body, target_x, cur_y, target_z);
        g_iframe_timer = 12;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "TRICK DOWN";
        mod_log("trickster: Trick Down retreat %.1f", warp_dist);
        return;
    }

    // 3. Sky Star (Air Dash) -> In mid-air
    if (airborne) {
        if (!g_sky_star_used) {
            g_sky_star_used = true;
            g_is_air_dash = true;
            g_dash_timer = 8;
            g_iframe_timer = 10;
            g_cooldown = kDashCooldownFrames;

            g_dash_dx = std::sin(facing);
            g_dash_dz = std::cos(facing);
            g_active_action_name = "SKY STAR";
            mod_log("trickster: Sky Star air dash facing=%.2f", facing);
            return;
        }
        // If already used in this jump, only drop input if high in the air;
        // if close to ground, fall through to Ground Dash.
        if ((cur_y - floor_y) > 90.0f) {
            return;
        }
    }

    // 4. Ground Dash -> Standard Style Action
    g_is_air_dash = false;
    g_dash_timer = kDashFrames;
    g_iframe_timer = kDashFrames + 2;
    g_cooldown = kDashCooldownFrames;

    g_dash_dx = std::sin(facing);
    g_dash_dz = std::cos(facing);
    g_active_action_name = "DASH";
    mod_log("trickster: Dash started facing=%.2f", facing);
}

}  // namespace

void trickster_init() {
    g_dash_timer = 0;
    g_cooldown = 0;
    g_iframe_timer = 0;
    g_protected_hp = 0;
    g_action_buffer = 0;
    g_has_buffered_pad = false;
    std::memset(&g_buffered_pad_state, 0, sizeof(g_buffered_pad_state));
    g_dash_dx = 0.0f;
    g_dash_dz = 0.0f;
    g_is_air_dash = false;
    g_sky_star_used = false;
    g_active_action_name = "";
}

bool trickster_is_dashing() {
    return g_dash_timer > 0;
}

bool trickster_is_invulnerable() {
    return g_iframe_timer > 0;
}

const char* trickster_action_name() {
    if (g_dash_timer > 0 || g_iframe_timer > 0) {
        return g_active_action_name;
    }
    return "";
}

void trickster_request_action(const XInputState* state) {
    g_action_buffer = kActionBufferFrames;
    if (state) {
        g_buffered_pad_state = *state;
        g_has_buffered_pad = true;
    } else {
        g_has_buffered_pad = false;
    }

    if (g_dash_timer == 0 && g_cooldown == 0) {
        g_action_buffer = 0;
        trickster_execute_action();
    }
}

void trickster_on_action(XInputState* state) {
    trickster_request_action(state);
}

void trickster_update() {
    if (g_cooldown > 0) {
        g_cooldown--;
    }

    // Fire buffered action as soon as Dante becomes ready
    if (g_dash_timer == 0 && g_cooldown == 0 && g_action_buffer > 0) {
        g_action_buffer = 0;
        trickster_execute_action();
    }

    if (g_action_buffer > 0) {
        g_action_buffer--;
    }

    uint8_t* body = mod_actor();
    if (!body) {
        g_dash_timer = 0;
        g_iframe_timer = 0;
        g_action_buffer = 0;
        return;
    }

    // Ground check to reset Sky Star
    float cur_y = read_f32(body + kPosBaseRva + 4);
    float floor_y = read_f32(body + kPosTargetRva + 4);
    if ((cur_y - floor_y) <= 30.0f) {
        g_sky_star_used = false;
    }

    // Active Dash Motion
    if (g_dash_timer > 0) {
        // Continuously suppress any gun shooting animation
        clear_shoot_state(body);

        float cur_x = read_f32(body + kPosBaseRva);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        int step = (kDashFrames - g_dash_timer) + 1;
        float speed = get_dash_speed(step);

        float next_x = cur_x + g_dash_dx * speed;
        float next_z = cur_z + g_dash_dz * speed;

        if (g_is_air_dash) {
            // Keep height across all roots during Sky Star to create the hover/glide feel
            write_horizontal_coords(body, next_x, next_z);
            write_f32(body + kPosBaseRva + 4, cur_y);
            write_f32(body + kPosVisualRva + 4, cur_y);
            write_f32(body + kPosTargetRva + 4, cur_y);
        } else {
            // Ground dash only moves horizontal coordinates so floor collision adjusts Y cleanly
            write_horizontal_coords(body, next_x, next_z);
        }

        g_dash_timer--;
        if (g_dash_timer == 0) {
            g_is_air_dash = false;
        }
    }

    // Invulnerability Protection (i-frames)
    if (g_iframe_timer > 0) {
        g_iframe_timer--;
        uint16_t current_hp = *reinterpret_cast<uint16_t*>(body + kHpRva);
        if (current_hp < g_protected_hp) {
            // Restore HP negated during dash
            *reinterpret_cast<uint16_t*>(body + kHpRva) = g_protected_hp;
            *reinterpret_cast<uint16_t*>(body + kHpDisplayRva) = g_protected_hp;
        }
    }
}
