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
    // 10 frame burst: 48 -> 36 -> 22 units/frame (total ~354 units).
    // Fast, responsive, stylish dash that bursts noticeably ahead of running speed,
    // while reliably staying safely under capsule radius (64 units) per frame.
    if (step <= 3) {
        return 48.0f;
    }
    if (step <= 7) {
        return 36.0f;
    }
    return 22.0f;
}

void trickster_execute_action() {
    uint8_t* body = mod_actor();
    if (!body) {
        return;
    }

    // Immediately cancel any active shooting animation
    clear_shoot_state(body);

    float facing = read_f32(body + kFacingRva);
    float cur_x = read_f32(body + kPosBaseRva);
    float cur_y = read_f32(body + kPosBaseRva + 4);
    float cur_z = read_f32(body + kPosBaseRva + 8);

    // Record HP for i-frames invulnerability
    g_protected_hp = *reinterpret_cast<uint16_t*>(body + kHpRva);

    // Collect directional input from either Gamepad Left Stick or Keyboard WASD / Arrows
    float in_x = 0.0f;
    float in_z = 0.0f;

    // 1. Gamepad Left Stick
    if (g_has_buffered_pad) {
        SHORT sx = g_buffered_pad_state.gamepad.leftX;
        SHORT sy = g_buffered_pad_state.gamepad.leftY;
        if (std::abs(sx) > 7000 || std::abs(sy) > 7000) {
            in_x = static_cast<float>(sx) / 32767.0f;
            in_z = static_cast<float>(sy) / 32767.0f;
        }
    }

    // 2. Keyboard WASD and Arrow Keys
    // In DMC1 camera/world space:
    // W / Up is +Z
    // S / Down is -Z
    // D / Right is +X
    // A / Left is -X
    float key_x = 0.0f;
    float key_z = 0.0f;
    if ((GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState(VK_UP) & 0x8000) != 0) {
        key_z += 1.0f;
    }
    if ((GetAsyncKeyState('S') & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) {
        key_z -= 1.0f;
    }
    if ((GetAsyncKeyState('D') & 0x8000) != 0 || (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
        key_x += 1.0f;
    }
    if ((GetAsyncKeyState('A') & 0x8000) != 0 || (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) {
        key_x -= 1.0f;
    }

    if (key_x != 0.0f || key_z != 0.0f) {
        float klen = std::hypot(key_x, key_z);
        in_x = key_x / klen;
        in_z = key_z / klen;
    }

    float input_mag = std::hypot(in_x, in_z);
    bool has_direction = input_mag > 0.25f;
    if (has_direction) {
        in_x /= input_mag;
        in_z /= input_mag;
    }

    // Dante's forward unit vector: (sin(facing), cos(facing))
    float f_dx = std::sin(facing);
    float f_dz = std::cos(facing);

    // Relative alignment: dot product between input direction and Dante's facing direction
    // dot > 0.35: Input is in the direction Dante is facing (FORWARD relative to Dante)
    // dot < -0.35: Input is in the direction OPPOSITE to Dante's facing (BACKWARD relative to Dante)
    float dot = has_direction ? (in_x * f_dx + in_z * f_dz) : 0.0f;

    // 1. Warp Dash (Forward Warp / Air Trick) -> Forward relative to Dante's facing + Style
    if (has_direction && dot > 0.35f) {
        uint8_t* enemy = find_target_enemy(body);
        if (enemy) {
            float ex = read_f32(enemy + kPosBaseRva);
            float ey = read_f32(enemy + kPosBaseRva + 4);
            float ez = read_f32(enemy + kPosBaseRva + 8);
            float edx = ex - cur_x;
            float edz = ez - cur_z;
            float edist = std::hypot(edx, edz);
            if (edist > 60.0f) {
                float stop_dist = edist > 85.0f ? (edist - 85.0f) : 0.0f;
                float target_x = cur_x + (edx / edist) * stop_dist;
                float target_z = cur_z + (edz / edist) * stop_dist;

                write_coords(body, target_x, ey, target_z);
                write_f32(body + kFacingRva, std::atan2(edx, edz));
                g_iframe_timer = 12;
                g_cooldown = kWarpCooldownFrames;
                g_active_action_name = "WARP DASH";
                mod_log("trickster: Warp Dash to enemy dist=%.1f", edist);
                return;
            }
        }

        // Forward warp across the room in Dante's facing direction
        float warp_dist = 750.0f;
        float target_x = cur_x + f_dx * warp_dist;
        float target_z = cur_z + f_dz * warp_dist;

        write_coords(body, target_x, cur_y, target_z);
        g_iframe_timer = 10;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "WARP DASH";
        mod_log("trickster: Warp Dash forward %.1f", warp_dist);
        return;
    }

    // 2. Backdash (Retreat Warp / Trick Down) -> Opposite to Dante's facing + Style
    if (has_direction && dot < -0.35f) {
        float warp_dist = 450.0f;
        float target_x = cur_x - f_dx * warp_dist;
        float target_z = cur_z - f_dz * warp_dist;

        write_coords(body, target_x, cur_y, target_z);
        g_iframe_timer = 12;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "BACKDASH";
        mod_log("trickster: Backdash retreat %.1f", warp_dist);
        return;
    }

    // 3. Ground Dash -> Neutral Style OR Strafe/Side Dash in input direction
    g_dash_timer = kDashFrames;
    g_iframe_timer = kDashFrames + 2;
    g_cooldown = kDashCooldownFrames;

    if (has_direction) {
        // Dash in the direction pressed (e.g. strafe dash)
        g_dash_dx = in_x;
        g_dash_dz = in_z;
        write_f32(body + kFacingRva, std::atan2(in_x, in_z));
    } else {
        // Neutral dash forward
        g_dash_dx = f_dx;
        g_dash_dz = f_dz;
    }

    g_active_action_name = "DASH";
    mod_log("trickster: Dash started dx=%.2f dz=%.2f facing=%.2f", g_dash_dx, g_dash_dz, read_f32(body + kFacingRva));
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

        write_horizontal_coords(body, next_x, next_z);

        g_dash_timer--;
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
