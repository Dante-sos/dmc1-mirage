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
constexpr uintptr_t kMotionActiveRva = 0x1A54;
constexpr uintptr_t kMotionPrevRva = 0x1A50;
constexpr uintptr_t kAnimStructRva = 0x460;
constexpr uintptr_t kAnimTimerOffset = 0x110;

constexpr uintptr_t kPlayMotionRva = 0x3C3250;
constexpr uintptr_t kPlayEffectRva = 0x81D20;

// Dante motion IDs in DMC1
constexpr uint32_t kMotionWalk = 0x500;
constexpr uint32_t kMotionRun = 0x501;
constexpr uint32_t kMotionDash = 0x507;
constexpr uint32_t kMotionBackstep = 0x518;
constexpr uint32_t kMotionLanding = 0x50C;
constexpr uint32_t kMotionStinger = 0x523;

// Game audio/particle effect IDs
constexpr uintptr_t kMotionTrailRva = 0x56383B;
constexpr uint32_t kEffectDashWhoosh = 0x00440000;
constexpr uint32_t kEffectGroundDust = 0x00030000;
constexpr uint32_t kEffectSparkStreak = 0x00580021;
constexpr uint32_t kEffectLightningSparks = 0x00580023;
constexpr uint32_t kEffectMagicCircle = 0x00170000;
constexpr uint32_t kEffectTeleportRing = 0x01040001;
constexpr uint32_t kEffectTeleportFlash = 0x01040002;

using PlayMotionFn = void(__fastcall*)(uint32_t motion_id, void* target_pos, float speed, void* extra);
using PlayEffectFn = void(__fastcall*)(uint32_t effect_id, void* p1, void* pos, uint64_t flags);

constexpr int kDashFrames = 16;
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

void play_actor_motion(uint8_t* body, uint32_t motion_id) {
    uintptr_t base = mod_base();
    if (!base || !body) {
        return;
    }

    *reinterpret_cast<uint32_t*>(body + kMotionPrevRva) = motion_id;
    *reinterpret_cast<uint32_t*>(body + kMotionActiveRva) = motion_id;

    auto** anim_ptr = reinterpret_cast<uint8_t**>(body + kAnimStructRva);
    if (anim_ptr && *anim_ptr) {
        *reinterpret_cast<float*>(*anim_ptr + kAnimTimerOffset) = 0.0f;
    }

    auto play_fn = reinterpret_cast<PlayMotionFn>(base + kPlayMotionRva);
    play_fn(motion_id, body + kPosVisualRva, 20000.0f, nullptr);
}

void play_effect_at(float x, float y, float z, uint32_t effect_id) {
    uintptr_t base = mod_base();
    if (!base) {
        return;
    }
    float pos[4] = {x, y, z, 0.0f};
    auto effect_fn = reinterpret_cast<PlayEffectFn>(base + kPlayEffectRva);
    effect_fn(effect_id, nullptr, pos, 0x100000000ULL);
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
    // 16 frame dynamic momentum curve (total 628 units):
    // Frames 1-3:   58.0f (explosive launch: 174 units)
    // Frames 4-8:   46.0f (high-speed sustained glide: 230 units)
    // Frames 9-13:  34.0f (smooth deceleration: 170 units)
    // Frames 14-16: 18.0f (seamless landing recovery: 54 units)
    if (step <= 3) {
        return 58.0f;
    }
    if (step <= 8) {
        return 46.0f;
    }
    if (step <= 13) {
        return 34.0f;
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
            in_z = -static_cast<float>(sy) / 32767.0f;
        }
    }

    // 2. Keyboard WASD and Arrow Keys
    // In DMC1 camera/world space:
    // W / Up is -Z (Forward, away from camera)
    // S / Down is +Z (Backward, towards camera)
    // D / Right is +X (Right)
    // A / Left is -X (Left)
    float key_x = 0.0f;
    float key_z = 0.0f;
    if ((GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState(VK_UP) & 0x8000) != 0) {
        key_z -= 1.0f;
    }
    if ((GetAsyncKeyState('S') & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) {
        key_z += 1.0f;
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

    // Check if Lock-on is active (RB on Gamepad, or Shift / Space on Keyboard)
    bool lock_on = false;
    if (g_has_buffered_pad) {
        WORD buttons = g_buffered_pad_state.gamepad.buttons;
        lock_on = (buttons & 0x0200) != 0; // RB in XInput is 0x0200 (Right Shoulder)
    }


    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0) {
        lock_on = true;
    }

    // 1. Warp Dash (Forward Warp / Air Trick) -> ONLY with Lock-on + Forward relative to Dante
    if (lock_on && has_direction && dot > 0.60f) {
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

                // 1. Departure magic glyph and teleport ring at origin
                play_effect_at(cur_x, cur_y, cur_z, kEffectMagicCircle);
                play_effect_at(cur_x, cur_y, cur_z, kEffectTeleportRing);
                play_effect_at(cur_x, cur_y, cur_z, kEffectDashWhoosh);

                // 2. Perform warp directly to enemy
                write_coords(body, target_x, ey, target_z);
                write_f32(body + kFacingRva, std::atan2(edx, edz));

                // 3. Arrival colored teleport flash and lightning streak
                play_effect_at(target_x, ey, target_z, kEffectTeleportFlash);
                play_effect_at(target_x, ey, target_z, kEffectLightningSparks);

                *reinterpret_cast<uint16_t*>(body) = 0x0602;
                body[2] = 1;
                play_actor_motion(body, kMotionStinger);

                g_iframe_timer = 12;
                g_cooldown = kWarpCooldownFrames;
                g_active_action_name = "WARP DASH";
                mod_log("trickster: Warp Dash to enemy dist=%.1f in_x=%.2f in_z=%.2f dot=%.2f", edist, in_x, in_z, dot);
                return;
            }
        }

        // Forward warp across the room in Dante's facing direction
        float warp_dist = 750.0f;
        float target_x = cur_x + f_dx * warp_dist;
        float target_z = cur_z + f_dz * warp_dist;

        play_effect_at(cur_x, cur_y, cur_z, kEffectMagicCircle);
        play_effect_at(cur_x, cur_y, cur_z, kEffectTeleportRing);
        play_effect_at(cur_x, cur_y, cur_z, kEffectDashWhoosh);

        write_coords(body, target_x, cur_y, target_z);

        play_effect_at(target_x, cur_y, target_z, kEffectTeleportFlash);
        play_effect_at(target_x, cur_y, target_z, kEffectLightningSparks);

        *reinterpret_cast<uint16_t*>(body) = 0x0602;
        body[2] = 1;
        play_actor_motion(body, kMotionStinger);

        g_iframe_timer = 10;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "WARP DASH";
        mod_log("trickster: Warp Dash forward %.1f in_x=%.2f in_z=%.2f dot=%.2f facing=%.2f", warp_dist, in_x, in_z, dot, facing);
        return;
    }

    // 2. Backdash (Retreat Warp / Trick Down) -> ONLY with Lock-on + Backward relative to Dante
    if (lock_on && has_direction && dot < -0.60f) {
        float warp_dist = 560.0f;
        float target_x = cur_x - f_dx * warp_dist;
        float target_z = cur_z - f_dz * warp_dist;

        play_effect_at(cur_x, cur_y, cur_z, kEffectMagicCircle);
        play_effect_at(cur_x, cur_y, cur_z, kEffectDashWhoosh);

        write_coords(body, target_x, cur_y, target_z);

        play_effect_at(target_x, cur_y, target_z, kEffectTeleportFlash);
        play_effect_at(target_x, cur_y, target_z, kEffectSparkStreak);

        *reinterpret_cast<uint16_t*>(body) = 0x0902;
        body[2] = 1;
        play_actor_motion(body, kMotionBackstep);

        g_iframe_timer = 12;
        g_cooldown = kWarpCooldownFrames;
        g_active_action_name = "BACKDASH";
        mod_log("trickster: Backdash retreat %.1f in_x=%.2f in_z=%.2f dot=%.2f facing=%.2f", warp_dist, in_x, in_z, dot, facing);
        return;
    }

    // 3. Normal Ground Dash -> Whenever Lock-on is NOT held, OR neutral, OR strafe/side direction!
    g_dash_timer = kDashFrames;
    g_iframe_timer = kDashFrames;
    g_cooldown = kDashCooldownFrames;

    if (has_direction) {
        // Dash in the direction pressed (moving left, right, forward, backward, or strafe)
        g_dash_dx = in_x;
        g_dash_dz = in_z;
        write_f32(body + kFacingRva, std::atan2(in_x, in_z));
    } else {
        // Neutral dash forward in current facing
        g_dash_dx = f_dx;
        g_dash_dz = f_dz;
    }

    uintptr_t base = mod_base();
    if (base) {
        *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) |= 0x1C;
    }

    *reinterpret_cast<uint16_t*>(body) = 0x0902;
    body[2] = 1;
    body[0x17A4] = 0;

    play_actor_motion(body, kMotionDash);
    play_effect_at(cur_x, cur_y, cur_z, kEffectDashWhoosh);
    play_effect_at(cur_x, cur_y, cur_z, kEffectGroundDust);
    play_effect_at(cur_x, cur_y, cur_z, kEffectSparkStreak);

    g_active_action_name = "DASH";
    mod_log("trickster: Dash started dx=%.2f dz=%.2f facing=%.2f dot=%.2f lock_on=%d", g_dash_dx, g_dash_dz, read_f32(body + kFacingRva), dot, lock_on ? 1 : 0);
}

}  // namespace

void trickster_init() {
    uintptr_t base = mod_base();
    if (base) {
        *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) &= 0xEB;
    }
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

        // Keep Action 9 active in the player state machine throughout the dash
        // so DMC1 engine maintains Dante in the evasive dash animation instead of idling
        *reinterpret_cast<uint16_t*>(body) = 0x0902;
        body[2] = 1;

        uintptr_t base = mod_base();
        if (base) {
            *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) |= 0x1C;
        }

        float cur_x = read_f32(body + kPosBaseRva);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        int step = (kDashFrames - g_dash_timer) + 1;
        float speed = get_dash_speed(step);

        float next_x = cur_x + g_dash_dx * speed;
        float next_z = cur_z + g_dash_dz * speed;

        write_horizontal_coords(body, next_x, next_z);

        g_dash_timer--;
        if (g_dash_timer == 0) {
            // Turn off colored motion trail
            if (base) {
                *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) &= 0xEB;
            }

            // Seamless transition
            bool input_held = false;
            if ((GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState('S') & 0x8000) != 0 ||
                (GetAsyncKeyState('A') & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_UP) & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
                (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
                input_held = true;
            }
            if (g_has_buffered_pad) {
                SHORT sx = g_buffered_pad_state.gamepad.leftX;
                SHORT sy = g_buffered_pad_state.gamepad.leftY;
                if (std::abs(sx) > 7000 || std::abs(sy) > 7000) {
                    input_held = true;
                }
            }

            if (input_held) {
                *reinterpret_cast<uint16_t*>(body) = 0x0202; // Action 2: Run
                body[2] = 1;
                play_actor_motion(body, kMotionRun);
            } else {
                *reinterpret_cast<uint16_t*>(body) = 0x0002; // Action 0: Idle
                body[2] = 0;
                play_actor_motion(body, kMotionLanding);
            }
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
