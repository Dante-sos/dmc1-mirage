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
int g_probe_h = 0;
ID3D11SamplerState* g_sampler = nullptr;
ID3D11BlendState* g_blend = nullptr;
ID3D11DepthStencilState* g_depth = nullptr;
ID3D11RasterizerState* g_raster = nullptr;
size_t g_vb_capacity = 0;
bool g_ready = false;
bool g_logged_present_fail = false;

void release_renderer() {
    auto drop = [](auto*& object) {
        if (object) {
            object->Release();
            object = nullptr;
        }
    };
    drop(g_vs);
    drop(g_ps);
    drop(g_layout);
    drop(g_vb);
    drop(g_cb);
    drop(g_font_view);
    drop(g_bg_view);
    drop(g_probe);
    g_bg_w = 0;
    g_bg_h = 0;
    g_probe_pending = false;
    g_probe_w = 0;
    g_probe_h = 0;
    drop(g_sampler);
    drop(g_blend);
    drop(g_depth);
    drop(g_raster);
    drop(g_context);
    drop(g_device);
    g_vb_capacity = 0;
    g_ready = false;
}

bool compile_shader(const char* source, const char* entry, const char* target, ID3DBlob** blob) {
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!compiler) {
        log_line("d3dcompiler_47.dll missing");
        return false;
    }
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    auto compile = reinterpret_cast<CompileFn>(GetProcAddress(compiler, "D3DCompile"));
    if (!compile) {
        return false;
    }
    ID3DBlob* errors = nullptr;
    HRESULT result = compile(source, std::strlen(source), nullptr, nullptr, nullptr, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, blob, &errors);
    if (FAILED(result)) {
        if (errors) {
            log_line("shader: %s", static_cast<const char*>(errors->GetBufferPointer()));
            errors->Release();
        }
        return false;
    }
    if (errors) {
        errors->Release();
    }
    return true;
}

bool build_font(ID3D11Device* device) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kAtlas;
    info.bmiHeader.biHeight = -kAtlas;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        DeleteDC(dc);
        return false;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    RECT all{0, 0, kAtlas, kAtlas};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &all, black);
    DeleteObject(black);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Times New Roman");
    HGDIOBJ old_font = SelectObject(dc, font);
    for (int ch = 32; ch <= 126; ++ch) {
        int cell = ch - 31;
        int col = cell % kCols;
        int row = cell / kCols;
        RECT rect{col * kCell + 1, row * kCell, (col + 1) * kCell, (row + 1) * kCell};
        wchar_t text[2] = {static_cast<wchar_t>(ch), 0};
        DrawTextW(dc, text, 1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SIZE size{};
        GetTextExtentPoint32W(dc, text, 1, &size);
        int width = size.cx + 1;
        if (width < 4) {
            width = 4;
        }
        if (width > kCell - 1) {
            width = kCell - 1;
        }
        g_glyph_w[ch - 32] = width;
    }
    auto* pixels = static_cast<uint8_t*>(bits);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            uint8_t* solid = pixels + (y * kAtlas + x) * 4;
            solid[0] = solid[1] = solid[2] = solid[3] = 255;
        }
    }
    for (int i = 0; i < kAtlas * kAtlas; ++i) {
        int x = i % kAtlas;
        int y = i / kAtlas;
        if (x < 8 && y < 8) {
            continue;
        }
        uint8_t* px = pixels + i * 4;
        uint8_t lum = px[0] > px[1] ? px[0] : px[1];
        if (px[2] > lum) {
            lum = px[2];
        }
        px[0] = 255;
        px[1] = 255;
        px[2] = 255;
        px[3] = lum;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kAtlas;
    desc.Height = kAtlas;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = bits;
    data.SysMemPitch = kAtlas * 4;
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &data, &texture);
    SelectObject(dc, old_font);
    SelectObject(dc, old);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(dc);
    if (FAILED(result)) {
        return false;
    }
    result = device->CreateShaderResourceView(texture, nullptr, &g_font_view);
    texture->Release();
    return SUCCEEDED(result);
}

void load_background(ID3D11Device* device) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
