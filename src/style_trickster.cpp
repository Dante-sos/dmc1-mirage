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
constexpr uintptr_t kEnemyTargetRva = 0x1C18;
constexpr uintptr_t kShootTimerRva = 0x1C05;

constexpr int kDashFrames = 10;
constexpr int kDashCooldownFrames = 12;
constexpr int kWarpCooldownFrames = 20;

int g_dash_timer = 0;
int g_cooldown = 0;
int g_iframe_timer = 0;
uint16_t g_protected_hp = 0;

float g_dash_dx = 0.0f;
float g_dash_dz = 0.0f;
float g_last_dash_x = 0.0f;
float g_last_dash_z = 0.0f;
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

float get_dash_speed(int step) {
    // Punchy burst that eases out smoothly.
    // Peak at 38 units/frame, safely below Dante's 64-unit collision capsule radius
    // so DMC1's room collision engine resolves boundaries and never tunnels through pillars/walls.
    if (step <= 3) {
        return 38.0f;
    }
    if (step <= 7) {
        return 28.0f;
    }
    return 16.0f;
}

}  // namespace

void trickster_init() {
    g_dash_timer = 0;
    g_cooldown = 0;
    g_iframe_timer = 0;
    g_protected_hp = 0;
    g_dash_dx = 0.0f;
    g_dash_dz = 0.0f;
    g_last_dash_x = 0.0f;
    g_last_dash_z = 0.0f;
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

void trickster_on_action(XInputState* state) {
    if (g_dash_timer > 0 || g_cooldown > 0) {
        return;
    }

    uint8_t* body = mod_actor();
    if (!body) {
        return;
    }

    // Immediately cancel any active shooting animation
    clear_shoot_state(body);

    float facing = read_f32(body + kFacingRva);
    float visual_y = read_f32(body + kPosVisualRva + 4);
    float base_y = read_f32(body + kPosBaseRva + 4);
    bool airborne = (visual_y - base_y) > 35.0f;

    bool lock_on = false;
    SHORT stick_y = 0;
    SHORT stick_x = 0;

    if (state) {
        WORD buttons = state->gamepad.buttons;
        lock_on = (buttons & 0x0200) != 0; // RB in XInput is 0x0200 (Right Shoulder)
        stick_y = state->gamepad.leftY;
        stick_x = state->gamepad.leftX;
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

    // 1. Air Trick (Forward Warp) -> Lock-on + Forward + Style Action
    if (lock_on && stick_y > 10000) {
        float cur_x = read_f32(body + kPosBaseRva);
        float cur_y = read_f32(body + kPosBaseRva + 4);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        // If an enemy is locked on, warp directly to the front of the enemy
        auto* enemy = *reinterpret_cast<uint8_t**>(body + kEnemyTargetRva);
        if (enemy) {
            float ex = read_f32(enemy + kPosBaseRva);
            float ey = read_f32(enemy + kPosBaseRva + 4);
            float ez = read_f32(enemy + kPosBaseRva + 8);
            float edx = ex - cur_x;
            float edz = ez - cur_z;
            float edist = std::sqrt(edx * edx + edz * edz);
            if (edist > 80.0f && edist < 1400.0f) {
                float stop_dist = edist - 110.0f;
                float target_x = cur_x + (edx / edist) * stop_dist;
                float target_z = cur_z + (edz / edist) * stop_dist;
                float target_y = ey;

                write_coords(body, target_x, target_y, target_z);
                write_f32(body + kFacingRva, std::atan2(edx, edz));
                g_iframe_timer = 12;
                g_cooldown = kWarpCooldownFrames;
                g_active_action_name = "AIR TRICK";
                mod_log("trickster: Air Trick target warp to enemy dist=%.1f", edist);
                return;
            }
        }

        // Safe unconstrained warp distance (320 units avoids overshooting into room mesh)
        float warp_dist = 320.0f;
        float target_x = cur_x + std::sin(facing) * warp_dist;
        float target_z = cur_z + std::cos(facing) * warp_dist;

        write_coords(body, target_x, cur_y, target_z);
        g_iframe_timer = 10;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "AIR TRICK";
        mod_log("trickster: Air Trick forward warp %.1f", warp_dist);
        return;
    }

    // 2. Trick Down (Retreat Warp) -> Lock-on + Back + Style Action
    if (lock_on && stick_y < -10000) {
        float cur_x = read_f32(body + kPosBaseRva);
        float cur_y = read_f32(body + kPosBaseRva + 4);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        // Safe retreat distance (260 units avoids embedding into rear walls)
        float warp_dist = 260.0f;
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
        if (g_sky_star_used) {
            return;
        }
        g_sky_star_used = true;
        g_is_air_dash = true;
        g_dash_timer = 8;
        g_iframe_timer = 10;
        g_cooldown = kDashCooldownFrames;

        g_dash_dx = std::sin(facing);
        g_dash_dz = std::cos(facing);
        g_last_dash_x = read_f32(body + kPosBaseRva);
        g_last_dash_z = read_f32(body + kPosBaseRva + 8);
        g_active_action_name = "SKY STAR";
        mod_log("trickster: Sky Star air dash facing=%.2f", facing);
        return;
    }

    // 4. Ground Dash -> Standard Style Action
    g_is_air_dash = false;
    g_dash_timer = kDashFrames;
    g_iframe_timer = kDashFrames + 2;
    g_cooldown = kDashCooldownFrames;

    g_dash_dx = std::sin(facing);
    g_dash_dz = std::cos(facing);
    g_last_dash_x = read_f32(body + kPosBaseRva);
    g_last_dash_z = read_f32(body + kPosBaseRva + 8);
    g_active_action_name = "DASH";
    mod_log("trickster: Dash started facing=%.2f", facing);
}

void trickster_update() {
    if (g_cooldown > 0) {
        g_cooldown--;
    }

    uint8_t* body = mod_actor();
    if (!body) {
        g_dash_timer = 0;
        g_iframe_timer = 0;
        return;
    }

    // Ground check to reset Sky Star
    float visual_y = read_f32(body + kPosVisualRva + 4);
    float base_y = read_f32(body + kPosBaseRva + 4);
    if ((visual_y - base_y) <= 15.0f) {
        g_sky_star_used = false;
    }

    // Active Dash Motion
    if (g_dash_timer > 0) {
        // Continuously suppress any gun shooting animation
        clear_shoot_state(body);

        float cur_x = read_f32(body + kPosBaseRva);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        // Wall / Pillar contact detection: if previous frame movement was blocked by geometry,
        // stop the dash immediately to prevent pushing inside solid meshes.
        if (g_dash_timer < kDashFrames) {
            float moved_dx = cur_x - g_last_dash_x;
            float moved_dz = cur_z - g_last_dash_z;
            float actual_move = std::sqrt(moved_dx * moved_dx + moved_dz * moved_dz);
            if (actual_move < 1.5f) {
                // Dante made contact with solid geometry; end dash cleanly
                g_dash_timer = 0;
                g_is_air_dash = false;
                return;
            }
        }

        int step = (kDashFrames - g_dash_timer) + 1;
        float speed = get_dash_speed(step);

        float next_x = cur_x + g_dash_dx * speed;
        float next_z = cur_z + g_dash_dz * speed;

        g_last_dash_x = next_x;
        g_last_dash_z = next_z;

        if (g_is_air_dash) {
            // Keep height during Sky Star to create the hover/glide feel
            write_horizontal_coords(body, next_x, next_z);
            write_f32(body + kPosVisualRva + 4, visual_y);
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
