#include "the_void_menu.h"
#include "the_void.h"
#include "mod.h"

#include <windowsx.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <atomic>


// Drawing functions from ui.cpp
extern void draw_rect(std::vector<Vertex>& verts, float x, float y, float w, float h, float r, float g, float b, float a);
extern void draw_text(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a);
extern void draw_text_outlined(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a);
extern float text_width(const char* text, float scale, float tracking);
extern void void_apply_dummy_subtable(DummyType type);

VoidSettings g_void_settings{};

namespace {

HWND g_game_hwnd = nullptr;
WNDPROC g_orig_wndproc = nullptr;

float g_mouse_x = 0.0f;
float g_mouse_y = 0.0f;
bool g_mouse_down = false;
std::atomic<bool> g_mouse_clicked{false};

constexpr float kScale = 0.56f;
constexpr float kSmallScale = 0.44f;
constexpr float kBigScale = 0.92f;

const char* kDummyNames[] = {
    "Marionette (Blade)",
    "Marionette (Shotgun)",
    "Sin Scissors",
    "Sin Scythe",
    "Shadow",
    "Frost",
    "Blade",
    "Nobody"
};

const char* kAiNames[] = {
    "Stand Still (Punching Bag)",
    "Defensive AI",
    "Combat AI (Normal)"
};

uint64_t get_time_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool in_rect(float px, float py, float rx, float ry, float rw, float rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

LRESULT CALLBACK void_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (void_menu_on_wndproc(hwnd, msg, wparam, lparam)) {
        return 0; // consumed by void menu!
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wparam, lparam);
}

} // namespace

void void_menu_init(HWND game_hwnd) {
    if (!game_hwnd || g_game_hwnd == game_hwnd) return;
    g_game_hwnd = game_hwnd;
    g_orig_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(void_wndproc)));
    mod_log("the_void_menu: installed WndProc hook on HWND %p", game_hwnd);
}

void void_menu_toggle() {
    g_void_settings.window_open = !g_void_settings.window_open;
    g_void_settings.open_dropdown = 0;
    g_void_settings.active_slider = 0;
    g_void_settings.dragging = false;

    if (g_void_settings.window_open) {
        while (ShowCursor(TRUE) < 0);
    } else {
        while (ShowCursor(FALSE) >= 0);
    }
    mod_log("the_void_menu: window toggled (open=%d)", g_void_settings.window_open ? 1 : 0);
}

bool void_menu_is_open() {
    return g_void_settings.window_open;
}

bool void_menu_on_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_KEYDOWN: {
            if (wparam == VK_INSERT || wparam == VK_F1) {
                void_menu_toggle();
                return true;
            }
            if (g_void_settings.window_open && wparam == VK_ESCAPE) {
                void_menu_toggle();
                return true;
            }
            if (mod_is_the_void() && mod_in_game() && !g_void_settings.window_open) {
                if (wparam == '1') {
                    mod_switch_melee();
                    return true;
                }
                if (wparam == '2') {
                    mod_switch_ranged();
                    return true;
                }
                if (wparam == VK_F5 || wparam == VK_HOME) {
                    void_menu_reset_dummy();
                    return true;
                }
            }
            break;
        }

        case WM_MOUSEMOVE: {
            g_mouse_x = static_cast<float>(GET_X_LPARAM(lparam));
            g_mouse_y = static_cast<float>(GET_Y_LPARAM(lparam));

            if (!g_void_settings.window_open) break;

            // Handle window dragging
            if (g_void_settings.dragging) {
                g_void_settings.win_x = g_mouse_x - g_void_settings.drag_off_x;
                g_void_settings.win_y = g_mouse_y - g_void_settings.drag_off_y;
                return true;
            }

            // Handle slider dragging
            if (g_void_settings.active_slider == 1) { // Dummy Max HP
                float slider_x = g_void_settings.win_x + 20.0f;
                float slider_w = g_void_settings.win_w - 40.0f;
                float frac = std::clamp((g_mouse_x - slider_x) / slider_w, 0.0f, 1.0f);
                g_void_settings.dummy_hp = std::round((1000.0f + frac * 9000.0f) / 250.0f) * 250.0f;
                return true;
            } else if (g_void_settings.active_slider == 2) { // Damage Multiplier
                float slider_x = g_void_settings.win_x + 20.0f;
                float slider_w = g_void_settings.win_w - 40.0f;
                float frac = std::clamp((g_mouse_x - slider_x) / slider_w, 0.0f, 1.0f);
                g_void_settings.damage_mult = std::round((0.5f + frac * 2.5f) * 10.0f) / 10.0f;
                return true;
            }

            // If mouse over window or open dropdown, consume
            if (in_rect(g_mouse_x, g_mouse_y, g_void_settings.win_x, g_void_settings.win_y, g_void_settings.win_w, g_void_settings.win_h + (g_void_settings.open_dropdown ? 150.0f : 0.0f))) {
                return true;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            if (!g_void_settings.window_open) break;

            POINT pt;
            if (GetCursorPos(&pt) && hwnd) {
                ScreenToClient(hwnd, &pt);
                g_mouse_x = static_cast<float>(pt.x);
                g_mouse_y = static_cast<float>(pt.y);
            } else {
                g_mouse_x = static_cast<float>(GET_X_LPARAM(lparam));
                g_mouse_y = static_cast<float>(GET_Y_LPARAM(lparam));
            }
            g_mouse_down = true;
            g_mouse_clicked.store(true);

            // Check if clicking inside window
            float extra_h = (g_void_settings.open_dropdown ? 220.0f : 0.0f);
            if (in_rect(g_mouse_x, g_mouse_y, g_void_settings.win_x, g_void_settings.win_y, g_void_settings.win_w, g_void_settings.win_h + extra_h)) {
                return true; // block attack from registering in game!
            }
            break;
        }

        case WM_LBUTTONUP: {
            if (!g_void_settings.window_open) break;

            g_mouse_down = false;
            g_void_settings.dragging = false;
            g_void_settings.active_slider = 0;

            if (in_rect(g_mouse_x, g_mouse_y, g_void_settings.win_x, g_void_settings.win_y, g_void_settings.win_w, g_void_settings.win_h)) {
                return true;
            }
            break;
        }

        case WM_SETCURSOR: {
            if (g_void_settings.window_open) {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return true;
            }
            break;
        }

        default:
            break;
    }
    return false;
}

void void_menu_reset_dummy() {
    uint8_t* player = mod_actor();
    if (!player) return;

    uintptr_t base = mod_base();
    if (!base) return;

    int32_t count = *reinterpret_cast<int32_t*>(base + 0x4CB6BA0);
    auto** enemy_list = reinterpret_cast<uint8_t**>(base + 0x4CB6BB0);

    // If no enemies spawned in the room yet, call the room enemy spawner to instantiate them!
    if (count <= 0 || !enemy_list) {
        void_apply_dummy_subtable(g_void_settings.dummy_type);
        using SpawnFn = void(__fastcall*)(void*);
        auto spawn_fn = reinterpret_cast<SpawnFn>(base + 0x244700);
        if (spawn_fn) {
            static float s_matrix[16] = {0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1};
            spawn_fn(s_matrix);
            count = *reinterpret_cast<int32_t*>(base + 0x4CB6BA0);
            enemy_list = reinterpret_cast<uint8_t**>(base + 0x4CB6BB0);
        }
    }

    if (count <= 0 || !enemy_list) {
        mod_log("the_void_menu: reset_dummy failed: count=%d", count);
        return;
    }

    float px = *reinterpret_cast<float*>(player + 0x1AC0);
    float py = *reinterpret_cast<float*>(player + 0x1AC4);
    float pz = *reinterpret_cast<float*>(player + 0x1AC8);
    float pyaw = *reinterpret_cast<float*>(player + 0x124);

    float forward_x = -std::sin(pyaw);
    float forward_z = std::cos(pyaw);
    float right_x = std::cos(pyaw);
    float right_z = std::sin(pyaw);
    float dummy_yaw = pyaw + 3.14159f;

    int max_dummies = std::clamp(g_void_settings.dummy_count, 1, 3);
    const float x_offsets[3] = {0.0f, -140.0f, 140.0f};

    int placed = 0;
    for (int i = 0; i < count && placed < max_dummies; ++i) {
        uint8_t* dummy = enemy_list[i];
        if (!dummy || dummy == player) continue;
        auto ptr_val = reinterpret_cast<uintptr_t>(dummy);
        if (ptr_val < 0x10000 || ptr_val > 0x7FFFFFFFFFFFull || (ptr_val & 7) != 0) continue;

        float off = x_offsets[placed];
        float dummy_x = px + forward_x * 240.0f + right_x * off;
        float dummy_z = pz + forward_z * 240.0f + right_z * off;

        for (uintptr_t offset : {uintptr_t{0x1AC0}, uintptr_t{0x1E0}, uintptr_t{0x170}}) {
            *reinterpret_cast<float*>(dummy + offset) = dummy_x;
            *reinterpret_cast<float*>(dummy + offset + 4) = py;
            *reinterpret_cast<float*>(dummy + offset + 8) = dummy_z;
        }
        *reinterpret_cast<float*>(dummy + 0x124) = dummy_yaw;

        // Zero momentum
        for (uintptr_t offset : {uintptr_t{0x1AC0 + 0x10}, uintptr_t{0x1E0 + 0x10}}) {
            *reinterpret_cast<float*>(dummy + offset) = 0.0f;
            *reinterpret_cast<float*>(dummy + offset + 4) = 0.0f;
            *reinterpret_cast<float*>(dummy + offset + 8) = 0.0f;
        }

        // Revive & refill HP to dummy_hp
        uint16_t hp_val = static_cast<uint16_t>(g_void_settings.dummy_hp);
        *reinterpret_cast<uint16_t*>(dummy + 0x1BCE) = hp_val;
        *reinterpret_cast<uint16_t*>(dummy + 0x1BD0) = hp_val;

        // Reset defeated state flag
        *reinterpret_cast<uint16_t*>(dummy + 0x17A2) = 0;

        placed++;
    }

    mod_log("the_void_menu: reset %d dummy(s) in front of Dante (total_count=%d)", placed, count);
}

void void_menu_kill_dummy() {
    uintptr_t base = mod_base();
    if (!base) return;

    int32_t count = *reinterpret_cast<int32_t*>(base + 0x4CB6BA0);
    auto** enemy_list = reinterpret_cast<uint8_t**>(base + 0x4CB6BB0);
    if (count <= 0 || !enemy_list) return;

    for (int i = 0; i < count; ++i) {
        uint8_t* e = enemy_list[i];
        if (!e) continue;
        *reinterpret_cast<uint16_t*>(e + 0x1BCE) = 0; // set HP to 0
        *reinterpret_cast<uint16_t*>(e + 0x17A2) = 0xFFFF; // mark defeated
    }
    mod_log("the_void_menu: dummy despawned / killed");
}

void void_menu_on_enemy_damage(float damage) {
    uint64_t now = get_time_ms();
    if (now - g_void_settings.last_hit_tick > 2500) { // combo timeout (2.5s)
        g_void_settings.combo_hits = 0;
        g_void_settings.current_combo_damage = 0.0f;
        g_void_settings.combo_start_tick = now;
    }

    g_void_settings.combo_hits++;
    g_void_settings.last_hit_damage = damage;
    g_void_settings.current_combo_damage += damage;
    g_void_settings.last_hit_tick = now;

    if (g_void_settings.combo_hits > g_void_settings.max_combo_hits) {
        g_void_settings.max_combo_hits = g_void_settings.combo_hits;
    }
    if (g_void_settings.current_combo_damage > g_void_settings.max_combo_damage) {
        g_void_settings.max_combo_damage = g_void_settings.current_combo_damage;
    }

    float duration_s = (now - g_void_settings.combo_start_tick) / 1000.0f;
    if (duration_s > 0.2f) {
        g_void_settings.dps = g_void_settings.current_combo_damage / duration_s;
    } else {
        g_void_settings.dps = damage;
    }
}

void void_menu_update() {
    // 1. Hotkey check via GetAsyncKeyState (fires reliably regardless of DirectInput/message loops)
    static bool s_prev_insert = false;
    bool insert_down = ((GetAsyncKeyState(VK_INSERT) & 0x8000) != 0) ||
                       ((GetAsyncKeyState(VK_F1) & 0x8000) != 0);
    if (insert_down && !s_prev_insert) {
        void_menu_toggle();
    }
    s_prev_insert = insert_down;

    static bool s_prev_esc = false;
    bool esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (esc_down && !s_prev_esc && g_void_settings.window_open) {
        void_menu_toggle();
    }
    s_prev_esc = esc_down;

    // Check weapon switch hotkeys if menu closed
    if (!g_void_settings.window_open) {
        static bool s_prev_1 = false;
        bool key_1 = (GetAsyncKeyState('1') & 0x8000) != 0;
        if (key_1 && !s_prev_1) {
            mod_switch_melee();
        }
        s_prev_1 = key_1;

        static bool s_prev_2 = false;
        bool key_2 = (GetAsyncKeyState('2') & 0x8000) != 0;
        if (key_2 && !s_prev_2) {
            mod_switch_ranged();
        }
        s_prev_2 = key_2;
    }

    // 2. Mouse position & drag tracking (event-driven clicks via WndProc)
    if (g_void_settings.window_open) {
        POINT pt;
        if (GetCursorPos(&pt)) {
            if (g_game_hwnd) {
                ScreenToClient(g_game_hwnd, &pt);
            }
            g_mouse_x = static_cast<float>(pt.x);
            g_mouse_y = static_cast<float>(pt.y);
        }

        bool lbutton = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (!lbutton) {
            g_mouse_down = false;
            g_void_settings.dragging = false;
            g_void_settings.active_slider = 0;
        }

        // Handle window dragging
        if (g_void_settings.dragging) {
            g_void_settings.win_x = g_mouse_x - g_void_settings.drag_off_x;
            g_void_settings.win_y = g_mouse_y - g_void_settings.drag_off_y;
        }

        // Handle slider dragging
        if (g_void_settings.active_slider == 1) { // Dummy Max HP
            float slider_x = g_void_settings.win_x + 20.0f;
            float slider_w = g_void_settings.win_w - 40.0f;
            float frac = std::clamp((g_mouse_x - slider_x) / slider_w, 0.0f, 1.0f);
            g_void_settings.dummy_hp = std::round((1000.0f + frac * 9000.0f) / 250.0f) * 250.0f;
        } else if (g_void_settings.active_slider == 2) { // Damage Multiplier
            float slider_x = g_void_settings.win_x + 20.0f;
            float slider_w = g_void_settings.win_w - 40.0f;
            float frac = std::clamp((g_mouse_x - slider_x) / slider_w, 0.0f, 1.0f);
            g_void_settings.damage_mult = std::round((0.5f + frac * 2.5f) * 10.0f) / 10.0f;
        }
    }

    // 3. Check combo expiration
    uint64_t now = get_time_ms();
    if (g_void_settings.combo_hits > 0 && now - g_void_settings.last_hit_tick > 3000) {
        g_void_settings.combo_hits = 0;
        g_void_settings.current_combo_damage = 0.0f;
        g_void_settings.dps = 0.0f;
    }
}

void void_menu_render(std::vector<Vertex>& verts, float screen_w, float screen_h) {
    if (!mod_is_the_void()) return;

    // 1. Render Combo Telemetry HUD (when enabled)
    if (g_void_settings.show_telemetry) {
        uint64_t now = get_time_ms();
        bool active = (g_void_settings.combo_hits > 0) || (now - g_void_settings.last_hit_tick < 3500);
        if (active) {
            float hud_w = 280.0f;
            float hud_h = 145.0f;
            float hud_x = screen_w - hud_w - 24.0f;
            float hud_y = 60.0f;

            // Background card (translucent sleek dark)
            draw_rect(verts, hud_x, hud_y, hud_w, hud_h, 0.0f, 0.0f, 0.0f, 0.25f);
            // Border
            draw_rect(verts, hud_x, hud_y, hud_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.60f);
            draw_rect(verts, hud_x, hud_y + hud_h - 1.0f, hud_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.60f);
            draw_rect(verts, hud_x, hud_y, 1.0f, hud_h, 1.0f, 1.0f, 1.0f, 0.60f);
            draw_rect(verts, hud_x + hud_w - 1.0f, hud_y, 1.0f, hud_h, 1.0f, 1.0f, 1.0f, 0.60f);
            // Top accent border (White)
            draw_rect(verts, hud_x, hud_y, hud_w, 2.0f, 1.0f, 1.0f, 1.0f, 0.90f);

            // Title
            draw_text_outlined(verts, hud_x + 14.0f, hud_y + 8.0f, "COMBO TELEMETRY", kScale, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);

            // Hit Counter (Large Font)
            char hits_str[32] = {};
            std::snprintf(hits_str, sizeof(hits_str), "%d  HITS", g_void_settings.combo_hits);
            draw_text_outlined(verts, hud_x + 14.0f, hud_y + 32.0f, hits_str, kBigScale, 0.8f, 1.0f, 1.0f, 1.0f, 1.0f);

            // Stats
            char stat1[64] = {}, stat2[64] = {}, stat3[64] = {};
            std::snprintf(stat1, sizeof(stat1), "TOTAL:  %.0f DMG", g_void_settings.current_combo_damage);
            std::snprintf(stat2, sizeof(stat2), "LAST:   %.0f DMG", g_void_settings.last_hit_damage);
            std::snprintf(stat3, sizeof(stat3), "DPS:    %.1f /s  (MAX: %d)", g_void_settings.dps, g_void_settings.max_combo_hits);

            draw_text(verts, hud_x + 14.0f, hud_y + 74.0f, stat1, kSmallScale, 0.4f, 0.92f, 0.92f, 0.95f, 0.95f);
            draw_text(verts, hud_x + 14.0f, hud_y + 96.0f, stat2, kSmallScale, 0.4f, 0.92f, 0.92f, 0.95f, 0.95f);
            draw_text(verts, hud_x + 14.0f, hud_y + 118.0f, stat3, kSmallScale, 0.4f, 0.85f, 0.85f, 0.90f, 0.95f);
        }
    }

    // 2. Render REFramework-Style Floating Transparent Window
    if (!g_void_settings.window_open) return;

    POINT cursor_pt;
    if (GetCursorPos(&cursor_pt) && g_game_hwnd) {
        ScreenToClient(g_game_hwnd, &cursor_pt);
        g_mouse_x = static_cast<float>(cursor_pt.x);
        g_mouse_y = static_cast<float>(cursor_pt.y);
    }
    bool clicked = g_mouse_clicked.exchange(false);

    float wx = g_void_settings.win_x;
    float wy = g_void_settings.win_y;
    float ww = g_void_settings.win_w;
    float wh = g_void_settings.win_h;

    // Clamp window to screen bounds
    wx = std::clamp(wx, 5.0f, screen_w - ww - 5.0f);
    wy = std::clamp(wy, 5.0f, screen_h - wh - 5.0f);
    g_void_settings.win_x = wx;
    g_void_settings.win_y = wy;

    // Window Background (Glassmorphic pure black high transparency)
    draw_rect(verts, wx, wy, ww, wh, 0.0f, 0.0f, 0.0f, 0.22f);
    // Window Border (Crisp white border)
    draw_rect(verts, wx, wy, ww, 1.0f, 1.0f, 1.0f, 1.0f, 0.65f);
    draw_rect(verts, wx, wy + wh - 1.0f, ww, 1.0f, 1.0f, 1.0f, 1.0f, 0.65f);
    draw_rect(verts, wx, wy, 1.0f, wh, 1.0f, 1.0f, 1.0f, 0.65f);
    draw_rect(verts, wx + ww - 1.0f, wy, 1.0f, wh, 1.0f, 1.0f, 1.0f, 0.65f);

    // Title Bar Header
    float header_h = 38.0f;
    bool header_hover = in_rect(g_mouse_x, g_mouse_y, wx, wy, ww - 34.0f, header_h);
    draw_rect(verts, wx, wy, ww, header_h, 0.03f, 0.03f, 0.03f, 0.50f);
    // Top Accent line (Pure White)
    draw_rect(verts, wx, wy, ww, 2.0f, 1.0f, 1.0f, 1.0f, 0.95f);
    draw_text_outlined(verts, wx + 16.0f, wy + 8.0f, "THE VOID // TRAINING CONFIG", kScale, 0.4f, 1.0f, 1.0f, 1.0f, 1.0f);

    // Handle Title Bar Drag
    if (header_hover && clicked) {
        g_void_settings.dragging = true;
        g_void_settings.drag_off_x = g_mouse_x - wx;
        g_void_settings.drag_off_y = g_mouse_y - wy;
    }

    // Close Button [X]
    float close_w = 26.0f;
    float close_h = 26.0f;
    float close_x = wx + ww - close_w - 6.0f;
    float close_y = wy + 5.0f;
    bool close_hover = in_rect(g_mouse_x, g_mouse_y, close_x, close_y, close_w, close_h);
    draw_rect(verts, close_x, close_y, close_w, close_h, close_hover ? 0.35f : 0.10f, close_hover ? 0.35f : 0.10f, close_hover ? 0.35f : 0.10f, 0.80f);
    draw_rect(verts, close_x, close_y, close_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.40f);
    draw_rect(verts, close_x, close_y + close_h - 1.0f, close_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.40f);
    draw_rect(verts, close_x, close_y, 1.0f, close_h, 1.0f, 1.0f, 1.0f, 0.40f);
    draw_rect(verts, close_x + close_w - 1.0f, close_y, 1.0f, close_h, 1.0f, 1.0f, 1.0f, 0.40f);
    draw_text(verts, close_x + 8.0f, close_y + 4.0f, "X", kSmallScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (close_hover && clicked) {
        void_menu_toggle();
        return;
    }

    // Window Content Layout Cursor
    float cur_y = wy + header_h + 16.0f;
    float pad_x = wx + 20.0f;
    float content_w = ww - 40.0f;

    // SECTION 1: TRAINING DUMMY
    draw_text(verts, pad_x, cur_y, "TRAINING DUMMY", kScale, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    cur_y += 22.0f;
    draw_rect(verts, pad_x, cur_y - 2.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.35f);

    // Dummy Count Stepper (1 to 3 dummies)
    draw_text(verts, pad_x, cur_y, "Simultaneous Dummies:", kSmallScale, 0.3f, 0.85f, 0.85f, 0.85f, 1.0f);
    cur_y += 20.0f;
    float step_btn_w = 34.0f;
    float step_btn_h = 26.0f;
    float step_val_w = 48.0f;

    bool minus_hover = in_rect(g_mouse_x, g_mouse_y, pad_x, cur_y, step_btn_w, step_btn_h);
    draw_rect(verts, pad_x, cur_y, step_btn_w, step_btn_h, minus_hover ? 0.30f : 0.12f, minus_hover ? 0.30f : 0.12f, minus_hover ? 0.30f : 0.12f, 0.70f);
    draw_rect(verts, pad_x, cur_y, step_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, pad_x, cur_y + step_btn_h - 1.0f, step_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, pad_x, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, pad_x + step_btn_w - 1.0f, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_text(verts, pad_x + 12.0f, cur_y + 3.0f, "-", kScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (minus_hover && clicked && g_void_settings.dummy_count > 1) {
        g_void_settings.dummy_count--;
        void_menu_reset_dummy();
    }

    char count_str[16] = {};
    std::snprintf(count_str, sizeof(count_str), "%d", g_void_settings.dummy_count);
    float val_x = pad_x + step_btn_w + 6.0f;
    draw_rect(verts, val_x, cur_y, step_val_w, step_btn_h, 0.08f, 0.08f, 0.08f, 0.70f);
    draw_rect(verts, val_x, cur_y, step_val_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_rect(verts, val_x, cur_y + step_btn_h - 1.0f, step_val_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_rect(verts, val_x, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_rect(verts, val_x + step_val_w - 1.0f, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_text(verts, val_x + 20.0f, cur_y + 3.0f, count_str, kScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);

    float plus_x = val_x + step_val_w + 6.0f;
    bool plus_hover = in_rect(g_mouse_x, g_mouse_y, plus_x, cur_y, step_btn_w, step_btn_h);
    draw_rect(verts, plus_x, cur_y, step_btn_w, step_btn_h, plus_hover ? 0.30f : 0.12f, plus_hover ? 0.30f : 0.12f, plus_hover ? 0.30f : 0.12f, 0.70f);
    draw_rect(verts, plus_x, cur_y, step_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, plus_x, cur_y + step_btn_h - 1.0f, step_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, plus_x, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_rect(verts, plus_x + step_btn_w - 1.0f, cur_y, 1.0f, step_btn_h, 1.0f, 1.0f, 1.0f, 0.50f);
    draw_text(verts, plus_x + 11.0f, cur_y + 3.0f, "+", kScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (plus_hover && clicked && g_void_settings.dummy_count < 3) {
        g_void_settings.dummy_count++;
        void_menu_reset_dummy();
    }
    cur_y += 34.0f;

    // Dropdown 1: Dummy Type
    draw_text(verts, pad_x, cur_y, "Dummy Enemy Type:", kSmallScale, 0.3f, 0.85f, 0.85f, 0.85f, 1.0f);
    cur_y += 20.0f;
    float dd1_y = cur_y;
    bool dd1_hover = in_rect(g_mouse_x, g_mouse_y, pad_x, dd1_y, content_w, 30.0f);
    draw_rect(verts, pad_x, dd1_y, content_w, 30.0f, 0.10f, 0.10f, 0.10f, 0.65f);
    draw_rect(verts, pad_x, dd1_y, content_w, 1.0f, 1.0f, 1.0f, 1.0f, dd1_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x, dd1_y + 29.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, dd1_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x, dd1_y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, dd1_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x + content_w - 1.0f, dd1_y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, dd1_hover ? 0.85f : 0.40f);
    draw_text(verts, pad_x + 10.0f, dd1_y + 5.0f, kDummyNames[static_cast<int>(g_void_settings.dummy_type)], kSmallScale, 0.3f, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_text(verts, pad_x + content_w - 20.0f, dd1_y + 5.0f, "v", kSmallScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (dd1_hover && clicked) {
        g_void_settings.open_dropdown = (g_void_settings.open_dropdown == 1) ? 0 : 1;
    }
    cur_y += 38.0f;

    // Dropdown 2: AI Behavior
    draw_text(verts, pad_x, cur_y, "Dummy AI Behavior:", kSmallScale, 0.3f, 0.85f, 0.85f, 0.85f, 1.0f);
    cur_y += 20.0f;
    float dd2_y = cur_y;
    bool dd2_hover = in_rect(g_mouse_x, g_mouse_y, pad_x, dd2_y, content_w, 30.0f);
    draw_rect(verts, pad_x, dd2_y, content_w, 30.0f, 0.10f, 0.10f, 0.10f, 0.65f);
    draw_rect(verts, pad_x, dd2_y, content_w, 1.0f, 1.0f, 1.0f, 1.0f, dd2_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x, dd2_y + 29.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, dd2_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x, dd2_y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, dd2_hover ? 0.85f : 0.40f);
    draw_rect(verts, pad_x + content_w - 1.0f, dd2_y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, dd2_hover ? 0.85f : 0.40f);
    draw_text(verts, pad_x + 10.0f, dd2_y + 5.0f, kAiNames[static_cast<int>(g_void_settings.dummy_ai)], kSmallScale, 0.3f, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_text(verts, pad_x + content_w - 20.0f, dd2_y + 5.0f, "v", kSmallScale, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (dd2_hover && clicked) {
        g_void_settings.open_dropdown = (g_void_settings.open_dropdown == 2) ? 0 : 2;
    }
    cur_y += 38.0f;

    // Slider 1: Dummy Max HP
    char hp_label[48] = {};
    std::snprintf(hp_label, sizeof(hp_label), "Dummy Max HP: %.0f", g_void_settings.dummy_hp);
    draw_text(verts, pad_x, cur_y, hp_label, kSmallScale, 0.3f, 0.85f, 0.85f, 0.85f, 1.0f);
    cur_y += 20.0f;
    float s1_y = cur_y;
    draw_rect(verts, pad_x, s1_y + 4.0f, content_w, 8.0f, 0.15f, 0.15f, 0.15f, 0.65f);
    draw_rect(verts, pad_x, s1_y + 4.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_rect(verts, pad_x, s1_y + 11.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    float hp_frac = std::clamp((g_void_settings.dummy_hp - 1000.0f) / 9000.0f, 0.0f, 1.0f);
    draw_rect(verts, pad_x, s1_y + 4.0f, content_w * hp_frac, 8.0f, 1.0f, 1.0f, 1.0f, 0.85f);
    draw_rect(verts, pad_x + content_w * hp_frac - 5.0f, s1_y, 10.0f, 16.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_rect(verts, pad_x + content_w * hp_frac - 5.0f, s1_y, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.8f);
    draw_rect(verts, pad_x + content_w * hp_frac - 5.0f, s1_y + 15.0f, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.8f);
    if (in_rect(g_mouse_x, g_mouse_y, pad_x - 5.0f, s1_y - 2.0f, content_w + 10.0f, 20.0f) && clicked) {
        g_void_settings.active_slider = 1;
    }
    cur_y += 28.0f;

    // Checkbox helper
    auto draw_check = [&](float x, float y, const char* label, bool& val) {
        bool hover = in_rect(g_mouse_x, g_mouse_y, x, y, content_w, 20.0f);
        draw_rect(verts, x, y, 20.0f, 20.0f, 0.10f, 0.10f, 0.10f, 0.65f);
        draw_rect(verts, x, y, 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.50f);
        draw_rect(verts, x, y + 19.0f, 20.0f, 1.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.50f);
        draw_rect(verts, x, y, 1.0f, 20.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.50f);
        draw_rect(verts, x + 19.0f, y, 1.0f, 20.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.50f);
        if (val) {
            draw_rect(verts, x + 4.0f, y + 4.0f, 12.0f, 12.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        }
        draw_text(verts, x + 30.0f, y + 2.0f, label, kSmallScale, 0.3f, 0.90f, 0.90f, 0.90f, 1.0f);
        if (hover && clicked) {
            val = !val;
        }
    };

    draw_check(pad_x, cur_y, "Immortal Dummy (HP Never Depletes)", g_void_settings.immortal_dummy);
    cur_y += 28.0f;
    draw_check(pad_x, cur_y, "Infinite Juggle (Floatier Air Physics)", g_void_settings.infinite_juggle);
    cur_y += 30.0f;

    // SECTION 2: DUMMY ACTIONS (BUTTONS)
    auto draw_btn = [&](float x, float y, const char* label, void (*on_click)()) {
        bool hover = in_rect(g_mouse_x, g_mouse_y, x, y, content_w, 30.0f);
        float r = hover ? 0.28f : 0.14f;
        draw_rect(verts, x, y, content_w, 30.0f, r, r, r, 0.75f);
        draw_rect(verts, x, y, content_w, 1.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.45f);
        draw_rect(verts, x, y + 29.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.45f);
        draw_rect(verts, x, y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.45f);
        draw_rect(verts, x + content_w - 1.0f, y, 1.0f, 30.0f, 1.0f, 1.0f, 1.0f, hover ? 0.90f : 0.45f);
        float tw = text_width(label, kSmallScale, 0.3f);
        draw_text(verts, x + (content_w - tw) * 0.5f, y + 6.0f, label, kSmallScale, 0.3f, 1.0f, 1.0f, 1.0f, 1.0f);
        if (hover && clicked) {
            on_click();
        }
    };

    draw_btn(pad_x, cur_y, "Spawn / Reset Dummy in Front of Dante", void_menu_reset_dummy);
    cur_y += 38.0f;
    draw_btn(pad_x, cur_y, "Despawn / Kill Dummy", void_menu_kill_dummy);
    cur_y += 42.0f;

    // SECTION 3: PLAYER & DIFFICULTY
    draw_text(verts, pad_x, cur_y, "PLAYER & DIFFICULTY", kScale, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    cur_y += 22.0f;
    draw_rect(verts, pad_x, cur_y - 2.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.35f);

    draw_check(pad_x, cur_y, "God Mode (Infinite Player HP)", g_void_settings.godmode_player);
    cur_y += 28.0f;
    draw_check(pad_x, cur_y, "Infinite Devil Trigger (10 Runes)", g_void_settings.infinite_dt);
    cur_y += 28.0f;
    draw_check(pad_x, cur_y, "Show Combo Telemetry HUD", g_void_settings.show_telemetry);
    cur_y += 30.0f;

    // Weapon Switching Buttons (two columns)
    float half_btn_w = (content_w - 12.0f) * 0.5f;
    bool w1_hover = in_rect(g_mouse_x, g_mouse_y, pad_x, cur_y, half_btn_w, 28.0f);
    float w1_col = w1_hover ? 0.28f : 0.14f;
    draw_rect(verts, pad_x, cur_y, half_btn_w, 28.0f, w1_col, w1_col, w1_col, 0.75f);
    draw_rect(verts, pad_x, cur_y, half_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, w1_hover ? 0.90f : 0.45f);
    draw_rect(verts, pad_x, cur_y + 27.0f, half_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, w1_hover ? 0.90f : 0.45f);
    draw_rect(verts, pad_x, cur_y, 1.0f, 28.0f, 1.0f, 1.0f, 1.0f, w1_hover ? 0.90f : 0.45f);
    draw_rect(verts, pad_x + half_btn_w - 1.0f, cur_y, 1.0f, 28.0f, 1.0f, 1.0f, 1.0f, w1_hover ? 0.90f : 0.45f);
    float w1_tw = text_width("Cycle Melee [1]", kSmallScale, 0.2f);
    draw_text(verts, pad_x + (half_btn_w - w1_tw) * 0.5f, cur_y + 5.0f, "Cycle Melee [1]", kSmallScale, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (w1_hover && clicked) {
        mod_switch_melee();
    }

    float w2_x = pad_x + half_btn_w + 12.0f;
    bool w2_hover = in_rect(g_mouse_x, g_mouse_y, w2_x, cur_y, half_btn_w, 28.0f);
    float w2_col = w2_hover ? 0.28f : 0.14f;
    draw_rect(verts, w2_x, cur_y, half_btn_w, 28.0f, w2_col, w2_col, w2_col, 0.75f);
    draw_rect(verts, w2_x, cur_y, half_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, w2_hover ? 0.90f : 0.45f);
    draw_rect(verts, w2_x, cur_y + 27.0f, half_btn_w, 1.0f, 1.0f, 1.0f, 1.0f, w2_hover ? 0.90f : 0.45f);
    draw_rect(verts, w2_x, cur_y, 1.0f, 28.0f, 1.0f, 1.0f, 1.0f, w2_hover ? 0.90f : 0.45f);
    draw_rect(verts, w2_x + half_btn_w - 1.0f, cur_y, 1.0f, 28.0f, 1.0f, 1.0f, 1.0f, w2_hover ? 0.90f : 0.45f);
    float w2_tw = text_width("Cycle Gun [2]", kSmallScale, 0.2f);
    draw_text(verts, w2_x + (half_btn_w - w2_tw) * 0.5f, cur_y + 5.0f, "Cycle Gun [2]", kSmallScale, 0.2f, 1.0f, 1.0f, 1.0f, 1.0f);
    if (w2_hover && clicked) {
        mod_switch_ranged();
    }
    cur_y += 36.0f;

    // Slider 2: Damage Multiplier
    char dmg_label[48] = {};
    std::snprintf(dmg_label, sizeof(dmg_label), "Damage Multiplier: %.1fx", g_void_settings.damage_mult);
    draw_text(verts, pad_x, cur_y, dmg_label, kSmallScale, 0.3f, 0.85f, 0.85f, 0.85f, 1.0f);
    cur_y += 20.0f;
    float s2_y = cur_y;
    draw_rect(verts, pad_x, s2_y + 4.0f, content_w, 8.0f, 0.15f, 0.15f, 0.15f, 0.65f);
    draw_rect(verts, pad_x, s2_y + 4.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    draw_rect(verts, pad_x, s2_y + 11.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.30f);
    float dmg_frac = std::clamp((g_void_settings.damage_mult - 0.5f) / 2.5f, 0.0f, 1.0f);
    draw_rect(verts, pad_x, s2_y + 4.0f, content_w * dmg_frac, 8.0f, 1.0f, 1.0f, 1.0f, 0.85f);
    draw_rect(verts, pad_x + content_w * dmg_frac - 5.0f, s2_y, 10.0f, 16.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    draw_rect(verts, pad_x + content_w * dmg_frac - 5.0f, s2_y, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.8f);
    draw_rect(verts, pad_x + content_w * dmg_frac - 5.0f, s2_y + 15.0f, 10.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.8f);
    if (in_rect(g_mouse_x, g_mouse_y, pad_x - 5.0f, s2_y - 2.0f, content_w + 10.0f, 20.0f) && clicked) {
        g_void_settings.active_slider = 2;
    }
    cur_y += 32.0f;

    // Footer Hint
    draw_text(verts, pad_x, wy + wh - 22.0f, "Press [INSERT] or [F1] to toggle menu", kSmallScale * 0.9f, 0.2f, 0.65f, 0.65f, 0.65f, 0.90f);

    // 3. RENDER OPEN DROPDOWN POPUPS ON TOP (so they overlap any widgets underneath)
    if (g_void_settings.open_dropdown == 1) { // Dummy Type Dropdown Popup
        float pop_y = dd1_y + 30.0f;
        float pop_h = static_cast<float>(static_cast<int>(DummyType::Count)) * 26.0f;
        bool in_pop1 = in_rect(g_mouse_x, g_mouse_y, pad_x, pop_y, content_w, pop_h);
        draw_rect(verts, pad_x, pop_y, content_w, pop_h, 0.06f, 0.06f, 0.06f, 0.92f);
        draw_rect(verts, pad_x, pop_y, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x, pop_y + pop_h - 1.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x, pop_y, 1.0f, pop_h, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x + content_w - 1.0f, pop_y, 1.0f, pop_h, 1.0f, 1.0f, 1.0f, 0.70f);
        for (int i = 0; i < static_cast<int>(DummyType::Count); ++i) {
            float item_y = pop_y + i * 26.0f;
            bool hover = in_rect(g_mouse_x, g_mouse_y, pad_x, item_y, content_w, 26.0f);
            if (hover) {
                draw_rect(verts, pad_x, item_y, content_w, 26.0f, 0.22f, 0.22f, 0.22f, 0.95f);
                if (clicked) {
                    g_void_settings.dummy_type = static_cast<DummyType>(i);
                    void_apply_dummy_subtable(g_void_settings.dummy_type);
                    void_menu_reset_dummy();
                    g_void_settings.open_dropdown = 0;
                }
            }
            float col = (g_void_settings.dummy_type == static_cast<DummyType>(i)) ? 1.00f : 0.75f;
            draw_text(verts, pad_x + 10.0f, item_y + 5.0f, kDummyNames[i], kSmallScale, 0.3f, col, col, col, 1.0f);
        }
        if (!in_pop1 && !dd1_hover && clicked) {
            g_void_settings.open_dropdown = 0;
        }
    } else if (g_void_settings.open_dropdown == 2) { // AI Behavior Dropdown Popup
        float pop_y = dd2_y + 30.0f;
        float pop_h = 3 * 26.0f;
        bool in_pop2 = in_rect(g_mouse_x, g_mouse_y, pad_x, pop_y, content_w, pop_h);
        draw_rect(verts, pad_x, pop_y, content_w, pop_h, 0.06f, 0.06f, 0.06f, 0.92f);
        draw_rect(verts, pad_x, pop_y, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x, pop_y + pop_h - 1.0f, content_w, 1.0f, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x, pop_y, 1.0f, pop_h, 1.0f, 1.0f, 1.0f, 0.70f);
        draw_rect(verts, pad_x + content_w - 1.0f, pop_y, 1.0f, pop_h, 1.0f, 1.0f, 1.0f, 0.70f);
        for (int i = 0; i < 3; ++i) {
            float item_y = pop_y + i * 26.0f;
            bool hover = in_rect(g_mouse_x, g_mouse_y, pad_x, item_y, content_w, 26.0f);
            if (hover) {
                draw_rect(verts, pad_x, item_y, content_w, 26.0f, 0.22f, 0.22f, 0.22f, 0.95f);
                if (clicked) {
                    g_void_settings.dummy_ai = static_cast<DummyAiMode>(i);
                    g_void_settings.open_dropdown = 0;
                }
            }
            float col = (g_void_settings.dummy_ai == static_cast<DummyAiMode>(i)) ? 1.00f : 0.75f;
            draw_text(verts, pad_x + 10.0f, item_y + 5.0f, kAiNames[i], kSmallScale, 0.3f, col, col, col, 1.0f);
        }
        if (!in_pop2 && !dd2_hover && clicked) {
            g_void_settings.open_dropdown = 0;
        }
    }

    // 4. Render Software Mouse Cursor (always visible on top of everything)
    if (g_void_settings.window_open) {
        float cx = g_mouse_x;
        float cy = g_mouse_y;
        // Shadow
        draw_rect(verts, cx + 1.0f, cy + 1.0f, 11.0f, 11.0f, 0.0f, 0.0f, 0.0f, 0.85f);
        // Outer core (pure white)
        draw_rect(verts, cx, cy, 10.0f, 10.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        // Inner contrast
        draw_rect(verts, cx + 2.0f, cy + 2.0f, 6.0f, 6.0f, 0.05f, 0.05f, 0.05f, 1.0f);
        draw_rect(verts, cx + 3.0f, cy + 3.0f, 4.0f, 4.0f, 1.0f, 1.0f, 1.0f, 1.0f);
        // Crosshair ticks
        draw_rect(verts, cx - 4.0f, cy + 4.0f, 3.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.90f);
        draw_rect(verts, cx + 11.0f, cy + 4.0f, 3.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.90f);
        draw_rect(verts, cx + 4.0f, cy - 4.0f, 2.0f, 3.0f, 1.0f, 1.0f, 1.0f, 0.90f);
        draw_rect(verts, cx + 4.0f, cy + 11.0f, 2.0f, 3.0f, 1.0f, 1.0f, 1.0f, 0.90f);
    }
}
