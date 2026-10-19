#include "mod.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <gdiplus.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kMissionCount = 23;
const char* kMissions[kMissionCount] = {
    "1   Curse of the Bloody Puppets",
    "2   Judge of Death",
    "3   Destroyer of Ardor",
    "4   Black Knight",
    "5   Guiding of the Soul",
    "6   Evil of the Waterways",
    "7   Holding the Key of Ardor",
    "8   The Legendary Knight Returns",
    "9   New Strength",
    "10  Canyon of Mist",
    "11  Fate",
    "12  Ghost Ship",
    "13  Abyss",
    "14  Deep Darkness & Towering Mountains",
    "15  Wheel of Destiny",
    "16  Nightmare of Darkness",
    "17  Parted Memento",
    "18  Spirit Stone, Elixir",
    "19  Enter the Corrupted World",
    "20  Showdown with Nightmare",
    "21  Living Cave",
    "22  Legendary Battle",
    "23  Mother's Guide",
};

constexpr int kModeValues[] = {2, 3, 5, 6};
const char* kModeNames[] = {"Easy", "Normal", "Hard", "Dante Must Die"};

constexpr WORD kMenuButtons = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x1000 | 0x2000;
constexpr int kCell = 40;
constexpr int kCols = 16;
constexpr int kAtlas = kCell * kCols;

void log_line(const char* fmt, ...) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) {
        return;
    }
    wcscpy(slash + 1, L"dmc1-switch.log");
    FILE* file = _wfopen(path, L"a");
    if (!file) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vfprintf(file, fmt, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

struct Vertex {
    float x, y, u, v, r, g, b, a;
};

std::atomic<WORD> g_buttons{0};
int g_mission = 0;
int g_mode = 1;
int g_glyph_w[95] = {};

enum class Screen { None, Title, Missions };
Screen g_screen = Screen::None;
int g_title_index = 0;
int g_game_index = 0;
int g_menu_lock = 0;
int g_title_miss = 0;
int g_title_hits = 0;
float g_menu_anchor = 0.662f;
std::atomic<bool> g_capture{false};
std::atomic<bool> g_inject_on{false};
std::atomic<WORD> g_inject_buttons{0};
WORD g_inject_words[12] = {};
int g_inject_count = 0;
int g_inject_pos = 0;
int g_inject_frames = 0;
int g_probe_cooldown = 0;
bool g_logged_menu = false;
WORD g_prev_buttons = 0;
bool g_key_down[7] = {};

using CreateFn = HRESULT(WINAPI*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT, const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

CreateFn g_create = nullptr;
PresentFn g_present = nullptr;
void** g_vtable_copy = nullptr;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
ID3D11VertexShader* g_vs = nullptr;
ID3D11PixelShader* g_ps = nullptr;
ID3D11InputLayout* g_layout = nullptr;
ID3D11Buffer* g_vb = nullptr;
ID3D11Buffer* g_cb = nullptr;
ID3D11ShaderResourceView* g_font_view = nullptr;
ID3D11ShaderResourceView* g_bg_view = nullptr;
int g_bg_w = 0;
int g_bg_h = 0;
ID3D11Texture2D* g_probe = nullptr;
bool g_probe_pending = false;
int g_probe_w = 0;
