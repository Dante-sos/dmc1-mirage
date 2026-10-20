#include "style.h"
#include "style_trickster.h"
#include "mod.h"

#include <atomic>

namespace {

constexpr WORD kDpadUp = 0x0001;
constexpr WORD kDpadDown = 0x0002;
constexpr WORD kDpadLeft = 0x0004;
constexpr WORD kDpadRight = 0x0008;
constexpr WORD kButtonB = 0x2000;
constexpr WORD kButtonX = 0x4000; // Square / Shoot in default controls
constexpr WORD kButtonY = 0x8000; // Triangle / Melee

constexpr uintptr_t kKeyboardDeviceRva = 0xC0B598;

StyleType g_active_style = StyleType::Trickster;
WORD g_prev_buttons = 0;

bool g_key_z = false;
bool g_key_x = false;
bool g_key_c = false;
bool g_key_v = false;
bool g_key_l = false;
std::atomic<bool> g_key_action_notified{false};

using GetDeviceStateFn = HRESULT(__fastcall*)(void* this_ptr, DWORD cbData, void* lpvData);
GetDeviceStateFn g_real_get_device_state = nullptr;

HRESULT __fastcall hook_get_device_state(void* this_ptr, DWORD cbData, void* lpvData) {
    HRESULT hr = g_real_get_device_state(this_ptr, cbData, lpvData);
    if (SUCCEEDED(hr) && lpvData && cbData >= 256 && mod_in_game()) {
        auto* keys = static_cast<uint8_t*>(lpvData);
        // Clear style keys from DMC1's DirectInput so default gun shoot (DIK_L = 0x26) never fires
        keys[0x26] = 0; // DIK_L (Trickster style action)
        keys[0x2C] = 0; // DIK_Z (Trickster)
        keys[0x2D] = 0; // DIK_X (Swordmaster)
        keys[0x2E] = 0; // DIK_C (Gunslinger)
        keys[0x2F] = 0; // DIK_V (RoyalGuard)

        // When performing a style action, suppress attack/fire keys so style action takes priority
        if (style_in_action()) {
            keys[0x24] = 0; // DIK_J (Melee attack)
            keys[0x17] = 0; // DIK_I (Devil trigger)
            // Drive WASD in dash direction so DMC1 motion engine animates Dante in full motion
            float d_dx = trickster_dash_dx();
            float d_dz = trickster_dash_dz();
            if (d_dz < -0.25f) keys[0x11] = 0x80; // DIK_W
            if (d_dz > 0.25f) keys[0x1F] = 0x80;  // DIK_S
            if (d_dx < -0.25f) keys[0x1E] = 0x80; // DIK_A
            if (d_dx > 0.25f) keys[0x20] = 0x80;  // DIK_D
        }
    }
    return hr;
}

void try_hook_keyboard_device() {
    static bool s_hooked = false;
    if (s_hooked) {
        return;
    }
    uintptr_t base = mod_base();
    if (!base) {
        return;
    }
    auto** dev_slot = reinterpret_cast<void***>(base + kKeyboardDeviceRva);
    if (!dev_slot || !*dev_slot) {
        return;
    }
    void** dev = *dev_slot;
    void** vtbl = *reinterpret_cast<void***>(dev);
    if (!vtbl) {
        return;
    }

    if (vtbl[9] != reinterpret_cast<void*>(hook_get_device_state)) {
        g_real_get_device_state = reinterpret_cast<GetDeviceStateFn>(vtbl[9]);
        DWORD old = 0;
        if (VirtualProtect(&vtbl[9], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            vtbl[9] = reinterpret_cast<void*>(hook_get_device_state);
            VirtualProtect(&vtbl[9], sizeof(void*), old, &old);
            s_hooked = true;
            mod_log("DirectInput8 keyboard device GetDeviceState hooked successfully");
        }
    }
}

bool key_edge(int vk, bool& was_down) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !was_down;
    was_down = down;
    return pressed;
}

}  // namespace

void style_init() {
    g_active_style = StyleType::Trickster;
    g_prev_buttons = 0;
    g_key_z = false;
    g_key_x = false;
    g_key_c = false;
    g_key_v = false;
    g_key_l = false;
    trickster_init();
    mod_log("style system initialized: active=TRICKSTER (keys: Z=Trickster, X=Swordmaster, C=Gunslinger, V=RoyalGuard, L=Action)");
}

StyleType style_current() {
    return g_active_style;
}

const char* style_name(StyleType type) {
    switch (type) {
        case StyleType::Trickster:
            return "TRICKSTER";
        case StyleType::RoyalGuard:
            return "ROYAL GUARD";
        case StyleType::Swordmaster:
            return "SWORDMASTER";
        case StyleType::Gunslinger:
            return "GUNSLINGER";
        default:
            return "NONE";
    }
}

void style_set(StyleType type) {
    if (g_active_style == type) {
        return;
    }
    g_active_style = type;
    mod_log("style switched to %s", style_name(type));
}

bool style_in_action() {
    if (g_active_style == StyleType::Trickster) {
        return trickster_is_dashing();
    }
    return false;
}

const char* style_action_name() {
    if (g_active_style == StyleType::Trickster) {
        return trickster_action_name();
    }
    return "";
}

void style_on_pad(XInputState* state) {
    if (!state || !mod_in_game()) {
        g_prev_buttons = state ? state->gamepad.buttons : 0;
        return;
    }

    WORD buttons = state->gamepad.buttons;
    WORD pressed = buttons & ~g_prev_buttons;
    g_prev_buttons = buttons;

    // Style Switching via D-Pad (Up=Trickster, Down=Royal Guard, Left=Gunslinger, Right=Swordmaster)
    if (pressed & kDpadUp) {
        style_set(StyleType::Trickster);
    } else if (pressed & kDpadDown) {
        style_set(StyleType::RoyalGuard);
    } else if (pressed & kDpadLeft) {
        style_set(StyleType::Gunslinger);
    } else if (pressed & kDpadRight) {
        style_set(StyleType::Swordmaster);
    }

    // Style Action via B / Circle Button
    bool action_pressed = (pressed & kButtonB) != 0;
    bool action_held = (buttons & kButtonB) != 0;
    bool key_l_held = (GetAsyncKeyState('L') & 0x8000) != 0;

    if (g_active_style == StyleType::Trickster) {
        if (action_pressed) {
            trickster_request_action(state);
        }
        // Consume B, Square (shoot), Melee (Y), Right Trigger so other actions don't interrupt dash
        if (action_held || key_l_held || trickster_is_dashing()) {
            state->gamepad.buttons &= ~kButtonB;
            state->gamepad.buttons &= ~kButtonX;
            state->gamepad.buttons &= ~kButtonY;
            state->gamepad.rightTrigger = 0;
        }

        if (trickster_is_dashing()) {
            // Feed the dash direction into the gamepad stick so DMC1 motion engine actively animates Dante forward!
            // In XInput: leftX positive is Right, leftY positive is Up/Forward (-Z in DMC1 world)
            float d_dx = trickster_dash_dx();
            float d_dz = trickster_dash_dz();
            state->gamepad.leftX = static_cast<SHORT>(d_dx * 32700.0f);
            state->gamepad.leftY = static_cast<SHORT>(-d_dz * 32700.0f);
        }
    }
}

void style_notify_key_action() {
    g_key_action_notified.store(true);
}

void style_update() {
    if (!mod_in_game()) {
        g_key_z = false;
        g_key_x = false;
        g_key_c = false;
        g_key_v = false;
        g_key_l = false;
        g_key_action_notified.store(false);
        return;
    }

    try_hook_keyboard_device();

    // Keyboard style switching: Z=Trickster, X=Swordmaster, C=Gunslinger, V=RoyalGuard
    if (key_edge('Z', g_key_z)) {
        style_set(StyleType::Trickster);
    } else if (key_edge('X', g_key_x)) {
        style_set(StyleType::Swordmaster);
    } else if (key_edge('C', g_key_c)) {
        style_set(StyleType::Gunslinger);
    } else if (key_edge('V', g_key_v)) {
        style_set(StyleType::RoyalGuard);
    }

    // Keyboard style action: L (both direct hook notification and edge polling)
    if (g_key_action_notified.exchange(false) || key_edge('L', g_key_l)) {
        if (g_active_style == StyleType::Trickster) {
            trickster_request_action(nullptr);
        }
    }

    if (g_active_style == StyleType::Trickster) {
        trickster_update();
    }
}
