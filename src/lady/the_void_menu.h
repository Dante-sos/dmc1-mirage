#pragma once

#include <cstdint>
#include <windows.h>
#include <vector>

struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;
};

enum class DummyType : int {
    MarionetteBlade = 0,
    MarionetteShotgun = 1,
    SinScissors = 2,
    SinScythe = 3,
    Shadow = 4,
    Frost = 5,
    Blade = 6,
    Nobody = 7,
    Count = 8
};

enum class DummyAiMode : int {
    StandStill = 0,  // Punching Bag (no moving/attacking, juggles freely)
    Defensive = 1,   // Blocks / parries when hit
    Combat = 2       // Standard attack AI
};

struct VoidSettings {
    bool window_open = false;
    float win_x = 40.0f;
    float win_y = 60.0f;
    float win_w = 540.0f;
    float win_h = 760.0f;

    // Window Dragging
    bool dragging = false;
    float drag_off_x = 0.0f;
    float drag_off_y = 0.0f;

    // Dropdown States: 0 = none, 1 = dummy_type, 2 = dummy_ai
    int open_dropdown = 0;

    // Slider Dragging: 0 = none, 1 = dummy_hp, 2 = damage_mult
    int active_slider = 0;

    // Configurations
    int dummy_count = 1; // 1 to 3 simultaneous training dummies
    DummyType dummy_type = DummyType::MarionetteBlade;
    DummyAiMode dummy_ai = DummyAiMode::StandStill;
    bool immortal_dummy = true;
    bool infinite_juggle = true; // floatier air physics / extended aerial combo juggle
    bool godmode_player = true;
    bool infinite_dt = true;
    bool show_telemetry = true;
    float dummy_hp = 5000.0f;
    float damage_mult = 1.0f;

    // Telemetry Statistics
    int combo_hits = 0;
    int max_combo_hits = 0;
    float current_combo_damage = 0.0f;
    float max_combo_damage = 0.0f;
    float last_hit_damage = 0.0f;
    float dps = 0.0f;
    uint64_t last_hit_tick = 0;
    uint64_t combo_start_tick = 0;
};

extern VoidSettings g_void_settings;

// Window Lifecycle & WndProc Hook
void void_menu_init(HWND game_hwnd);
void void_menu_toggle();
bool void_menu_is_open();
bool void_menu_on_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

// Actions triggered by buttons
void void_menu_reset_dummy();
void void_menu_kill_dummy();
void void_menu_on_enemy_damage(float damage);
void void_menu_update();
void void_apply_dummy_subtable(DummyType type);

// Rendering (called each frame during D3D11 paint)
void void_menu_render(std::vector<Vertex>& verts, float screen_w, float screen_h);
