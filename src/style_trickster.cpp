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

constexpr uintptr_t kActorPlayMotionRva = 0x26CCD0;
constexpr uintptr_t kPlayEffectRva = 0x81D20;
constexpr uintptr_t kGetEffectInstanceRva = 0x820D0;

// Dante authentic motion IDs in DMC1 (from body + 0x540 motion table)
constexpr uint32_t kMotionIdle = 0;
constexpr uint32_t kMotionWalk = 1;
constexpr uint32_t kMotionRun = 2;
constexpr uint32_t kMotionDashSlide = 0x25;   // 37 decimal: authentic DMC1 evasive slide/hop
constexpr uint32_t kMotionSprintSlide = 0x20; // 32 decimal: authentic DMC1 sprint slide
constexpr uint32_t kMotionBackflip = 0x1D;   // 29 decimal: authentic backflip/retreat
constexpr uint32_t kMotionStinger = 0x523;   // Stinger forward thrust

// Game audio/particle effect IDs
constexpr uintptr_t kMotionTrailRva = 0x56383B;
constexpr uint32_t kEffectDashWhoosh = 0x00440000;
constexpr uint32_t kEffectGroundDust = 0x00030000;
constexpr uint32_t kEffectEvasionWind = 0x000A0000;
constexpr uint32_t kEffectSparkStreak = 0x00580021;
constexpr uint32_t kEffectLightningSparks = 0x00580023;
constexpr uint32_t kEffectMagicCircle = 0x00170000;
constexpr uint32_t kEffectTeleportRing = 0x01040001;
constexpr uint32_t kEffectTeleportFlash = 0x01040002;

using ActorPlayMotionFn = void(__fastcall*)(void* actor, void* motion_table, uint32_t motion_id, uint32_t mode, uint32_t flags);
using PlayEffectFn = void*(__fastcall*)(uint32_t effect_id, void* p1, void* pos, uint64_t flags);
using GetEffectInstanceFn = void*(__fastcall*)(void* handle);

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
bool g_has_live_pad = false;
XInputState g_live_pad_state{};

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

void play_actor_motion(uint8_t* body, uint32_t motion_id, uint8_t blend_frames = 3) {
    uintptr_t base = mod_base();
    if (!base || !body) {
        return;
    }

    *reinterpret_cast<uint32_t*>(body + kMotionPrevRva) = motion_id & 0xFF;
    *reinterpret_cast<uint32_t*>(body + kMotionActiveRva) = motion_id & 0xFF;

    void* motion_table = *reinterpret_cast<void**>(body + 0x540);
    if (!motion_table) {
        return;
    }

    uint32_t full_id = motion_id;
    if ((motion_id & 0xFF00) == 0) {
        full_id = (static_cast<uint32_t>(blend_frames) << 8) | (motion_id & 0xFF);
    }

    auto play_fn = reinterpret_cast<ActorPlayMotionFn>(base + kActorPlayMotionRva);
    play_fn(body, motion_table, full_id, 1, 0x100);
}

bool is_movement_input_held() {
    if ((GetAsyncKeyState('W') & 0x8000) != 0 || (GetAsyncKeyState('S') & 0x8000) != 0 ||
        (GetAsyncKeyState('A') & 0x8000) != 0 || (GetAsyncKeyState('D') & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_UP) & 0x8000) != 0 || (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0 ||
        (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0 || (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
        return true;
    }
    if (g_has_live_pad) {
        SHORT sx = g_live_pad_state.gamepad.leftX;
        SHORT sy = g_live_pad_state.gamepad.leftY;
        if (std::abs(sx) > 7000 || std::abs(sy) > 7000) {
            return true;
        }
    } else if (g_has_buffered_pad) {
        SHORT sx = g_buffered_pad_state.gamepad.leftX;
        SHORT sy = g_buffered_pad_state.gamepad.leftY;
        if (std::abs(sx) > 7000 || std::abs(sy) > 7000) {
            return true;
        }
    }
    return false;
}

void play_effect_at(float x, float y, float z, uint32_t effect_id, float r = -1.0f, float g = 0.0f, float b = 0.0f, float a = 255.0f) {
    uintptr_t base = mod_base();
    if (!base) {
        return;
    }
    float pos[4] = {x, y, z, 0.0f};
    auto effect_fn = reinterpret_cast<PlayEffectFn>(base + kPlayEffectRva);
    void* handle = effect_fn(effect_id, nullptr, pos, 0x100000000ULL);
    if (handle && r >= 0.0f) {
        auto get_inst_fn = reinterpret_cast<GetEffectInstanceFn>(base + kGetEffectInstanceRva);
        auto* inst = static_cast<uint8_t*>(get_inst_fn(handle));
        if (inst) {
            auto* color = reinterpret_cast<float*>(inst + 0xC0);
            color[0] = r;
            color[1] = g;
            color[2] = b;
            color[3] = a;
        }
    }
}

void write_coords(uint8_t* body, float x, float y, float z) {
    if (!body) {
        return;
    }
    for (uintptr_t off : {kPosBaseRva, kPosVisualRva, kPosTargetRva, uintptr_t{0x1ACC}}) {
        write_f32(body + off, x);
        write_f32(body + off + 4, y);
        write_f32(body + off + 8, z);
    }
    auto** xform_slot = reinterpret_cast<uint8_t**>(body + 0x180);
    if (xform_slot && *xform_slot) {
        write_f32(*xform_slot + 0x30, x);
        write_f32(*xform_slot + 0x34, y);
        write_f32(*xform_slot + 0x38, z);
    }
}

void write_horizontal_coords(uint8_t* body, float x, float z) {
    if (!body) {
        return;
    }
    for (uintptr_t off : {kPosBaseRva, kPosVisualRva, kPosTargetRva, uintptr_t{0x1ACC}}) {
        write_f32(body + off, x);
        write_f32(body + off + 8, z);
    }
    auto** xform_slot = reinterpret_cast<uint8_t**>(body + 0x180);
    if (xform_slot && *xform_slot) {
        write_f32(*xform_slot + 0x30, x);
        write_f32(*xform_slot + 0x38, z);
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
    // 16 frame dynamic momentum curve (total 458 units):
    // Frames 1-3:   42.0f (explosive launch: 126 units)
    // Frames 4-8:   34.0f (high-speed sustained glide: 170 units)
    // Frames 9-13:  24.0f (smooth deceleration: 120 units)
    // Frames 14-16: 14.0f (seamless landing recovery: 42 units)
    if (step <= 3) {
        return 42.0f;
    }
    if (step <= 8) {
        return 34.0f;
    }
    if (step <= 13) {
        return 24.0f;
    }
    return 14.0f;
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
                play_effect_at(cur_x, cur_y + 40.0f, cur_z, kEffectDashWhoosh);

                // 2. Perform warp directly to enemy
                write_coords(body, target_x, ey, target_z);
                write_f32(body + kFacingRva, std::atan2(edx, edz));

                // 3. Arrival teleport flash and lightning sparks
                play_effect_at(target_x, ey, target_z, kEffectTeleportFlash);
                play_effect_at(target_x, ey, target_z, kEffectLightningSparks);

                *reinterpret_cast<uint16_t*>(body) = 0x0001;
                body[2] = 1;
                body[0x17A4] = 0x80;
                play_actor_motion(body, kMotionStinger, 2);

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
        play_effect_at(cur_x, cur_y + 40.0f, cur_z, kEffectDashWhoosh);

        write_coords(body, target_x, cur_y, target_z);

        play_effect_at(target_x, cur_y, target_z, kEffectTeleportFlash);
        play_effect_at(target_x, cur_y, target_z, kEffectLightningSparks);

        *reinterpret_cast<uint16_t*>(body) = 0x0001;
        body[2] = 1;
        body[0x17A4] = 0x80;
        play_actor_motion(body, kMotionStinger, 2);

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
        play_effect_at(cur_x, cur_y + 40.0f, cur_z, kEffectDashWhoosh);

        write_coords(body, target_x, cur_y, target_z);

        play_effect_at(target_x, cur_y, target_z, kEffectTeleportFlash);
        play_effect_at(target_x, cur_y, target_z, kEffectSparkStreak);

        *reinterpret_cast<uint16_t*>(body) = 0x0102; // Action 2, Subaction 1
        body[2] = 2; // Step 2 = backflip
        body[0x17A4] = 0x80;
        play_actor_motion(body, kMotionBackflip, 3);

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

    float dash_facing = facing;
    if (has_direction) {
        // Dash in the direction pressed (moving left, right, forward, backward, or strafe)
        g_dash_dx = in_x;
        g_dash_dz = in_z;
        dash_facing = std::atan2(in_x, in_z);
        write_f32(body + kFacingRva, dash_facing);
    } else {
        // Neutral dash forward in current facing
        g_dash_dx = f_dx;
        g_dash_dz = f_dz;
    }

    write_f32(body + 0x1928, dash_facing);
    write_f32(body + 0x1A28, dash_facing);

    uintptr_t base = mod_base();
    if (base) {
        *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) |= 0x18;
    }

    // Enable native 3D ghost afterimage cloner (bit 0x4000 set, bit 0x2000 cleared)
    *reinterpret_cast<uint16_t*>(body + 0x0E) |= 0x4000;
    *reinterpret_cast<uint16_t*>(body + 0x0E) &= ~0x2000;
    *reinterpret_cast<int32_t*>(body + 0x1a88) = 16;
    write_f32(body + 0x210, 0.0f);
    write_f32(body + 0x214, 0.0f);
    write_f32(body + 0x218, 0.0f);
    write_f32(body + 0x21C, 1.0f);

    // Set player state machine to active Evasion (Action 2, Subaction 1, Step 4 = Evasive slide/hop)
    *reinterpret_cast<uint16_t*>(body) = 0x0102;
    body[2] = 4;
    body[0x17A4] = 0x80; // Uninterruptible action lock

    // Initial forward dash velocity
    float init_speed = get_dash_speed(1);
    write_f32(body + 0x1F0, g_dash_dx * init_speed);
    write_f32(body + 0x1F4, 0.0f);
    write_f32(body + 0x1F8, g_dash_dz * init_speed);

    // Trigger authentic DMC1 evasive slide motion with crisp 3-frame blend
    play_actor_motion(body, kMotionDashSlide, 3);

    // 3D particles directly at Dante's body in world space
    play_effect_at(cur_x, cur_y + 35.0f, cur_z, kEffectEvasionWind);
    play_effect_at(cur_x, cur_y + 40.0f, cur_z, kEffectDashWhoosh);
    play_effect_at(cur_x, cur_y, cur_z, kEffectGroundDust);

    g_active_action_name = "DASH";
    mod_log("trickster: Dash started dx=%.2f dz=%.2f facing=%.2f dot=%.2f lock_on=%d", g_dash_dx, g_dash_dz, dash_facing, dot, lock_on ? 1 : 0);
}

}  // namespace

void trickster_init() {
    uint8_t* body = mod_actor();
    if (body) {
        *reinterpret_cast<uint16_t*>(body + 0x0E) &= ~0x4000;
        *reinterpret_cast<int32_t*>(body + 0x1a88) = 0;
        body[0x17A4] = 0;
    }
    uintptr_t base = mod_base();
    if (base) {
        *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) &= ~0x18;
    }
    g_dash_timer = 0;
    g_cooldown = 0;
    g_iframe_timer = 0;
    g_protected_hp = 0;
    g_action_buffer = 0;
    g_has_buffered_pad = false;
    g_has_live_pad = false;
    std::memset(&g_buffered_pad_state, 0, sizeof(g_buffered_pad_state));
    std::memset(&g_live_pad_state, 0, sizeof(g_live_pad_state));
    g_dash_dx = 0.0f;
    g_dash_dz = 0.0f;
    g_active_action_name = "";
}

void trickster_feed_pad_state(const XInputState* state) {
    if (state) {
        g_live_pad_state = *state;
        g_has_live_pad = true;
    } else {
        g_has_live_pad = false;
    }
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

int trickster_dash_timer() {
    return g_dash_timer;
}

void trickster_request_action(const XInputState* state) {
    g_action_buffer = kActionBufferFrames;
    if (state) {
        g_buffered_pad_state = *state;
        g_has_buffered_pad = true;
        g_live_pad_state = *state;
        g_has_live_pad = true;
    } else {
        g_has_buffered_pad = false;
        g_has_live_pad = false;
    }

    // Cancel recovery into next dash or start new dash immediately
    if (g_dash_timer <= 4 && g_cooldown == 0) {
        g_action_buffer = 0;
        g_dash_timer = 0;
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

        // Turn on actor shadow trail (enable bit 0x4000, clear bit 0x2000, set duration 16)
        *reinterpret_cast<uint16_t*>(body + 0x0E) |= 0x4000;
        *reinterpret_cast<uint16_t*>(body + 0x0E) &= ~0x2000;
        *reinterpret_cast<int32_t*>(body + 0x1a88) = 16;
        write_f32(body + 0x210, 0.0f);
        write_f32(body + 0x214, 0.0f);
        write_f32(body + 0x218, 0.0f);
        write_f32(body + 0x21C, 1.0f);

        uintptr_t base = mod_base();
        if (base) {
            *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) |= 0x18;
        }

        // Maintain active evasion action state and uninterruptible lock
        *reinterpret_cast<uint16_t*>(body) = 0x0102; // Action 2, Subaction 1
        body[2] = 5; // Step 5 = evasion physics active step
        body[0x17A4] = 0x80;

        // Keep Dante facing strictly in the dash direction throughout the dash
        float facing_angle = std::atan2(g_dash_dx, g_dash_dz);
        write_f32(body + kFacingRva, facing_angle);
        write_f32(body + 0x1928, facing_angle);
        write_f32(body + 0x1A28, facing_angle);

        float cur_x = read_f32(body + kPosBaseRva);
        float cur_y = read_f32(body + kPosBaseRva + 4);
        float cur_z = read_f32(body + kPosBaseRva + 8);

        int step = (kDashFrames - g_dash_timer) + 1;
        float speed = get_dash_speed(step);

        float next_x = cur_x + g_dash_dx * speed;
        float next_z = cur_z + g_dash_dz * speed;

        write_horizontal_coords(body, next_x, next_z);

        // Update velocity vector in the actor
        write_f32(body + 0x1F0, g_dash_dx * speed);
        write_f32(body + 0x1F4, 0.0f);
        write_f32(body + 0x1F8, g_dash_dz * speed);

        // Keep visual Y aligned with base Y for level, seamless gliding
        write_f32(body + kPosVisualRva + 4, cur_y);

        // Spawn 3D world particles attached directly to Dante in world space
        if (g_dash_timer % 3 == 0) {
            play_effect_at(next_x, cur_y + 35.0f, next_z, kEffectEvasionWind);
            play_effect_at(next_x, cur_y, next_z, kEffectGroundDust);
            play_effect_at(next_x, cur_y + 40.0f, next_z, kEffectDashWhoosh);
        }

        g_dash_timer--;
        if (g_dash_timer == 0) {
            // Turn off 3D shadow trail
            *reinterpret_cast<uint16_t*>(body + 0x0E) &= ~0x4000;
            *reinterpret_cast<int32_t*>(body + 0x1a88) = 0;
            if (base) {
                *reinterpret_cast<uint8_t*>(base + kMotionTrailRva) &= ~0x18;
            }

            // Restore visual Y to base Y
            write_f32(body + kPosVisualRva + 4, cur_y);
            body[0x17A4] = 0; // Release uninterruptible lock

            // Clear horizontal velocity
            write_f32(body + 0x1F0, 0.0f);
            write_f32(body + 0x1F8, 0.0f);

            // Seamless transition check
            bool input_held = is_movement_input_held();

            if (input_held) {
                // Instantly flow from slide into full running sprint
                *reinterpret_cast<uint16_t*>(body) = 0x0001; // Action 1, Subaction 0: Run
                body[2] = 1;
                play_actor_motion(body, kMotionRun, 4);
            } else {
                // Smoothly finish slide into standing idle without any freeze or pop
                *reinterpret_cast<uint16_t*>(body) = 0x0000; // Action 0: Idle
                body[2] = 0;
                play_actor_motion(body, kMotionIdle, 4);
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
        if (g_iframe_timer == 0 && g_dash_timer == 0) {
            body[0x17A4] = 0;
        }
    }
}

void trickster_dash_direction(float& dx, float& dz) {
    dx = g_dash_dx;
    dz = g_dash_dz;
}
