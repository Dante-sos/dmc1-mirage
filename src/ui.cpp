#include "mod.h"
#include "style.h"
#include "style_trickster.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <gdiplus.h>

#include <atomic>
#include <cmath>
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

enum class Screen { None, Launcher, Missions, BossRush, Settings, Title };
Screen g_screen = Screen::Launcher;
int g_launcher_index = 0;
int g_title_index = 0;
int g_game_index = 0;
int g_menu_lock = 0;
int g_title_miss = 0;
int g_title_hits = 0;
float g_menu_anchor = 0.662f;
bool g_setting_hud_enabled = true;
bool g_setting_fast_dash = true;
bool g_setting_quick_switch = true;
int g_settings_row = 0;
bool g_launcher_dismissed = false;
std::atomic<bool> g_capture{true};
std::atomic<bool> g_inject_on{false};
bool g_inject_keep = false;
std::atomic<bool> g_wait_release{false};
std::atomic<WORD> g_inject_buttons{0};
WORD g_inject_words[12] = {};
int g_inject_count = 0;
int g_inject_pos = 0;
int g_inject_frames = 0;
int g_probe_cooldown = 0;
bool g_seek = false;
int g_seek_pulse = 0;
int g_seek_gap = 0;
int g_seek_tries = 0;
WORD g_seek_word = 0;
SHORT g_seek_stick = 0;
bool g_seek_confirming = false;
bool g_logged_menu = false;
WORD g_prev_buttons = 0;
bool g_key_down[16] = {};

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
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) {
        return;
    }
    wcscpy(slash + 1, L"dmc1-mission-bg.jpg");
    struct GdiSession {
        ULONG_PTR token = 0;
        ~GdiSession() {
            if (token) {
                Gdiplus::GdiplusShutdown(token);
            }
        }
    } session;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&session.token, &input, nullptr) != Gdiplus::Ok) {
        session.token = 0;
        return;
    }
    Gdiplus::Bitmap bitmap(path);
    if (bitmap.GetLastStatus() != Gdiplus::Ok || bitmap.GetWidth() == 0) {
        log_line("mission background missing");
        return;
    }
    Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()));
    Gdiplus::BitmapData data{};
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(data.Width) * data.Height * 4);
    for (UINT y = 0; y < data.Height; ++y) {
        auto* src = static_cast<uint8_t*>(data.Scan0) + y * data.Stride;
        uint8_t* dst = pixels.data() + static_cast<size_t>(y) * data.Width * 4;
        for (UINT x = 0; x < data.Width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    g_bg_w = static_cast<int>(data.Width);
    g_bg_h = static_cast<int>(data.Height);
    bitmap.UnlockBits(&data);
    UINT bg_w = data.Width;
    UINT bg_h = data.Height;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = bg_w;
    desc.Height = bg_h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = bg_w * 4;
    ID3D11Texture2D* texture = nullptr;
    HRESULT result = device->CreateTexture2D(&desc, &sub, &texture);
    if (FAILED(result)) {
        log_line("mission background texture failed");
        return;
    }
    result = device->CreateShaderResourceView(texture, nullptr, &g_bg_view);
    texture->Release();
    if (SUCCEEDED(result)) {
        log_line("mission background ready %dx%d", g_bg_w, g_bg_h);
    }
}

bool init_renderer(ID3D11Device* device) {
    release_renderer();
    g_device = device;
    g_device->AddRef();
    device->GetImmediateContext(&g_context);
    const char* source = R"(
cbuffer Screen : register(b0) { float2 screen; float texMode; float pad; };
struct Vin { float2 pos : POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
struct Vout { float4 pos : SV_POSITION; float2 uv : TEXCOORD; float4 col : COLOR; };
Vout vs_main(Vin i) {
    Vout o;
    o.pos = float4(i.pos.x / screen.x * 2.0 - 1.0, 1.0 - i.pos.y / screen.y * 2.0, 0.0, 1.0);
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
Texture2D font : register(t0);
SamplerState samp : register(s0);
float4 ps_main(Vout i) : SV_TARGET {
    float4 t = font.Sample(samp, i.uv);
    float3 rgb = lerp(i.col.rgb, t.rgb * i.col.rgb, texMode);
    return float4(rgb, i.col.a * t.a);
}
)";
    ID3DBlob* vs_blob = nullptr;
    ID3DBlob* ps_blob = nullptr;
    if (!compile_shader(source, "vs_main", "vs_4_0", &vs_blob) || !compile_shader(source, "ps_main", "ps_4_0", &ps_blob)) {
        release_renderer();
        return false;
    }
    device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vs);
    device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &g_ps);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device->CreateInputLayout(layout, 3, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_layout);
    vs_blob->Release();
    ps_blob->Release();
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = 16;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device->CreateBuffer(&cb, nullptr, &g_cb);
    D3D11_SAMPLER_DESC samp{};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    device->CreateSamplerState(&samp, &g_sampler);
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    device->CreateBlendState(&blend, &g_blend);
    D3D11_DEPTH_STENCIL_DESC depth{};
    device->CreateDepthStencilState(&depth, &g_depth);
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    device->CreateRasterizerState(&raster, &g_raster);
    if (!build_font(device) || !g_vs || !g_ps || !g_layout || !g_cb) {
        log_line("overlay renderer init failed");
        release_renderer();
        return false;
    }
    load_background(device);
    g_ready = true;
    log_line("overlay renderer ready");
    return true;
}

constexpr UINT kSrvSlots = 16;

struct DxState {
    D3D11_PRIMITIVE_TOPOLOGY topology;
    ID3D11InputLayout* layout;
    ID3D11Buffer* vb;
    UINT stride;
    UINT offset;
    ID3D11Buffer* ib;
    DXGI_FORMAT ib_format;
    UINT ib_offset;
    ID3D11VertexShader* vs;
    ID3D11Buffer* vs_cb;
    ID3D11PixelShader* ps;
    ID3D11Buffer* ps_cb;
    ID3D11GeometryShader* gs;
    ID3D11ShaderResourceView* vs_srvs[kSrvSlots];
    ID3D11ShaderResourceView* gs_srvs[kSrvSlots];
    ID3D11ShaderResourceView* ps_srvs[kSrvSlots];
    ID3D11SamplerState* ps_samplers[kSrvSlots];
    ID3D11BlendState* blend;
    float blend_factor[4];
    UINT sample_mask;
    ID3D11DepthStencilState* depth;
    UINT stencil_ref;
    ID3D11RasterizerState* raster;
    UINT viewport_count;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT scissor_count;
    D3D11_RECT scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    ID3D11RenderTargetView* rtvs[8];
    ID3D11DepthStencilView* dsv;
};

void save_state(ID3D11DeviceContext* context, DxState& state) {
    std::memset(&state, 0, sizeof(state));
    context->IAGetPrimitiveTopology(&state.topology);
    context->IAGetInputLayout(&state.layout);
    context->IAGetVertexBuffers(0, 1, &state.vb, &state.stride, &state.offset);
    context->IAGetIndexBuffer(&state.ib, &state.ib_format, &state.ib_offset);
    context->VSGetShader(&state.vs, nullptr, nullptr);
    context->VSGetConstantBuffers(0, 1, &state.vs_cb);
    context->VSGetShaderResources(0, kSrvSlots, state.vs_srvs);
    context->PSGetShader(&state.ps, nullptr, nullptr);
    context->PSGetConstantBuffers(0, 1, &state.ps_cb);
    context->PSGetShaderResources(0, kSrvSlots, state.ps_srvs);
    context->PSGetSamplers(0, kSrvSlots, state.ps_samplers);
    context->GSGetShader(&state.gs, nullptr, nullptr);
    context->GSGetShaderResources(0, kSrvSlots, state.gs_srvs);
    context->OMGetBlendState(&state.blend, state.blend_factor, &state.sample_mask);
    context->OMGetDepthStencilState(&state.depth, &state.stencil_ref);
    context->RSGetState(&state.raster);
    state.viewport_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->RSGetViewports(&state.viewport_count, state.viewports);
    state.scissor_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    context->RSGetScissorRects(&state.scissor_count, state.scissors);
    context->OMGetRenderTargets(8, state.rtvs, &state.dsv);
}

void restore_state(ID3D11DeviceContext* context, DxState& state) {
    ID3D11ShaderResourceView* unbound[kSrvSlots] = {};
    context->PSSetShaderResources(0, kSrvSlots, unbound);
    context->VSSetShaderResources(0, kSrvSlots, unbound);
    context->GSSetShaderResources(0, kSrvSlots, unbound);
    context->OMSetRenderTargets(8, state.rtvs, state.dsv);
    context->IASetPrimitiveTopology(state.topology);
    context->IASetInputLayout(state.layout);
    context->IASetVertexBuffers(0, 1, &state.vb, &state.stride, &state.offset);
    context->IASetIndexBuffer(state.ib, state.ib_format, state.ib_offset);
    context->VSSetShader(state.vs, nullptr, 0);
    context->VSSetConstantBuffers(0, 1, &state.vs_cb);
    context->VSSetShaderResources(0, kSrvSlots, state.vs_srvs);
    context->PSSetShader(state.ps, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &state.ps_cb);
    context->PSSetShaderResources(0, kSrvSlots, state.ps_srvs);
    context->PSSetSamplers(0, kSrvSlots, state.ps_samplers);
    context->GSSetShader(state.gs, nullptr, 0);
    context->GSSetShaderResources(0, kSrvSlots, state.gs_srvs);
    context->OMSetBlendState(state.blend, state.blend_factor, state.sample_mask);
    context->OMSetDepthStencilState(state.depth, state.stencil_ref);
    context->RSSetState(state.raster);
    context->RSSetViewports(state.viewport_count, state.viewports);
    context->RSSetScissorRects(state.scissor_count, state.scissors);
    auto drop = [](auto*& object) {
        if (object) {
            object->Release();
            object = nullptr;
        }
    };
    drop(state.layout);
    drop(state.vb);
    drop(state.ib);
    drop(state.vs);
    drop(state.vs_cb);
    drop(state.ps);
    drop(state.ps_cb);
    drop(state.gs);
    for (auto& srv : state.vs_srvs) {
        drop(srv);
    }
    for (auto& srv : state.gs_srvs) {
        drop(srv);
    }
    for (auto& srv : state.ps_srvs) {
        drop(srv);
    }
    for (auto& sampler : state.ps_samplers) {
        drop(sampler);
    }
    drop(state.blend);
    drop(state.depth);
    drop(state.raster);
    for (auto& rtv : state.rtvs) {
        drop(rtv);
    }
    drop(state.dsv);
}

void push_quad(std::vector<Vertex>& verts, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float r, float g, float b, float a) {
    Vertex q[6] = {
        {x, y, u0, v0, r, g, b, a},
        {x + w, y, u1, v0, r, g, b, a},
        {x, y + h, u0, v1, r, g, b, a},
        {x, y + h, u0, v1, r, g, b, a},
        {x + w, y, u1, v0, r, g, b, a},
        {x + w, y + h, u1, v1, r, g, b, a},
    };
    verts.insert(verts.end(), q, q + 6);
}

void draw_rect(std::vector<Vertex>& verts, float x, float y, float w, float h, float r, float g, float b, float a) {
    float u = 4.0f / static_cast<float>(kAtlas);
    push_quad(verts, x, y, w, h, u, u, u, u, r, g, b, a);
}

float text_width(const char* text, float scale, float tracking) {
    float width = 0;
    for (const char* ch = text; *ch; ++ch) {
        unsigned char value = static_cast<unsigned char>(*ch);
        int advance = (value >= 32 && value <= 126) ? g_glyph_w[value - 32] : kCell / 2;
        if (advance <= 0) {
            advance = kCell / 2;
        }
        width += static_cast<float>(advance) * scale + tracking;
    }
    return width;
}

void draw_text(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a) {
    float cursor = x;
    float size = static_cast<float>(kCell) * scale;
    for (const char* ch = text; *ch; ++ch) {
        unsigned char value = static_cast<unsigned char>(*ch);
        int advance = kCell / 2;
        if (value >= 32 && value <= 126) {
            int cell = value - 31;
            int col = cell % kCols;
            int row = cell / kCols;
            float u0 = static_cast<float>(col * kCell) / kAtlas;
            float v0 = static_cast<float>(row * kCell) / kAtlas;
            float u1 = static_cast<float>((col + 1) * kCell) / kAtlas;
            float v1 = static_cast<float>((row + 1) * kCell) / kAtlas;
            push_quad(verts, cursor, y, size, size, u0, v0, u1, v1, r, g, b, a);
            advance = g_glyph_w[value - 32];
            if (advance <= 0) {
                advance = kCell / 2;
            }
        }
        cursor += static_cast<float>(advance) * scale + tracking;
    }
}

void draw_text_outlined(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a) {
    const float offset = 1.6f * scale;
    const float ox[8] = {-offset, offset, 0, 0, -offset, offset, -offset, offset};
    const float oy[8] = {0, 0, -offset, offset, -offset, -offset, offset, offset};
    for (int i = 0; i < 8; ++i) {
        draw_text(verts, x + ox[i], y + oy[i], text, scale, tracking, 0.02f, 0.0f, 0.0f, 0.95f);
    }
    draw_text(verts, x, y, text, scale, tracking, r, g, b, a);
}

void submit(std::vector<Vertex>& verts, float width, float height, float tex_mode, ID3D11ShaderResourceView* texture) {
    if (verts.empty() || !texture) {
        verts.clear();
        return;
    }
    if (verts.size() > g_vb_capacity) {
        if (g_vb) {
            g_vb->Release();
            g_vb = nullptr;
        }
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(verts.size() * sizeof(Vertex) * 2);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(g_device->CreateBuffer(&desc, nullptr, &g_vb))) {
            verts.clear();
            return;
        }
        g_vb_capacity = verts.size() * 2;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_vb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        verts.clear();
        return;
    }
    std::memcpy(mapped.pData, verts.data(), verts.size() * sizeof(Vertex));
    g_context->Unmap(g_vb, 0);
    if (FAILED(g_context->Map(g_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        verts.clear();
        return;
    }
    float screen[4] = {width, height, tex_mode, 0};
    std::memcpy(mapped.pData, screen, sizeof(screen));
    g_context->Unmap(g_cb, 0);
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    g_context->IASetInputLayout(g_layout);
    g_context->IASetVertexBuffers(0, 1, &g_vb, &stride, &offset);
    g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_context->VSSetShader(g_vs, nullptr, 0);
    g_context->VSSetConstantBuffers(0, 1, &g_cb);
    g_context->PSSetShader(g_ps, nullptr, 0);
    g_context->PSSetShaderResources(0, 1, &texture);
    g_context->PSSetSamplers(0, 1, &g_sampler);
    float blend_factor[4] = {0, 0, 0, 0};
    g_context->OMSetBlendState(g_blend, blend_factor, 0xFFFFFFFF);
    g_context->OMSetDepthStencilState(g_depth, 0);
    g_context->RSSetState(g_raster);
    D3D11_VIEWPORT viewport{};
    viewport.Width = width;
    viewport.Height = height;
    viewport.MaxDepth = 1;
    g_context->RSSetViewports(1, &viewport);
    g_context->Draw(static_cast<UINT>(verts.size()), 0);
    verts.clear();
}

void draw_background(std::vector<Vertex>& verts, float width, float height) {
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if (g_bg_w > 0 && g_bg_h > 0 && height > 0) {
        float screen_aspect = width / height;
        float image_aspect = static_cast<float>(g_bg_w) / static_cast<float>(g_bg_h);
        if (image_aspect > screen_aspect) {
            float visible = screen_aspect / image_aspect;
            float pad = (1.0f - visible) * 0.5f;
            u0 = pad;
            u1 = 1.0f - pad;
        } else {
            float visible = image_aspect / screen_aspect;
            float pad = (1.0f - visible) * 0.5f;
            v0 = pad;
            v1 = 1.0f - pad;
        }
    }
    push_quad(verts, 0, 0, width, height, u0, v0, u1, v1, 1, 1, 1, 1);
}

constexpr int kLauncherItemCount = 7;
const char* kLauncherItems[kLauncherItemCount] = {
    "LOAD DATA",
    "MISSION SELECT",
    "THE VOID",
    "BOSS RUSH",
    "SETTINGS",
    "START GAME",
    "EXIT TO HD COLLECTION"
};

const char* kLauncherSubtext[kLauncherItemCount] = {
    "Load saved game data and resume your adventure.",
    "Select any mission from 1 to 23 and choose difficulty.",
    "Combo practice in the Mission 8 arena with infinite HP and Devil Trigger.",
    "Consecutive boss trial against all bosses in DMC1.",
    "Style Switcher key bindings, HUD display, and controls.",
    "Start a new game from the prologue.",
    "Quit Devil May Cry 1 and return to HD Collection launcher."
};

const char* kTitleItems[] = {"LOAD DATA", "MISSION SELECT", "NEW GAME", "OPTION"};
constexpr float kTitleRows[] = {0.662f, 0.735f, 0.808f, 0.881f};

void draw_launcher_menu(float width, float height) {
    std::vector<Vertex> verts;
    verts.reserve(5000);

    if (g_bg_view) {
        draw_background(verts, width, height);
        submit(verts, width, height, 1.0f, g_bg_view);
    } else {
        draw_rect(verts, 0, 0, width, height, 0.05f, 0.02f, 0.02f, 1.0f);
        submit(verts, width, height, 0.0f, g_font_view);
    }

    // Dark gothic vignette overlay
    draw_rect(verts, 0, 0, width, height, 0.02f, 0.01f, 0.02f, 0.70f);

    float scale = height / 900.0f;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.55f) scale = 1.55f;

    // Top Crimson Accent Banner
    draw_rect(verts, 0, 0, width, 4.0f * scale, 0.85f, 0.12f, 0.18f, 1.0f);

    // Header Title
    const char* title_main = "DEVIL MAY CRY";
    const char* title_sub = "CRIMSON LAUNCHER  //  STYLE SWITCH EDITION";

    float title_scale = scale * 0.95f;
    float title_track = 1.6f * scale;
    float title_w = text_width(title_main, title_scale, title_track);
    float title_x = (width - title_w) * 0.5f;
    float title_y = height * 0.065f;
    draw_text_outlined(verts, title_x, title_y, title_main, title_scale, title_track, 0.98f, 0.92f, 0.84f, 1.0f);

    float sub_scale = scale * 0.38f;
    float sub_track = 1.1f * scale;
    float sub_w = text_width(title_sub, sub_scale, sub_track);
    float sub_x = (width - sub_w) * 0.5f;
    float sub_y = title_y + 42.0f * scale;
    draw_text(verts, sub_x, sub_y, title_sub, sub_scale, sub_track, 0.88f, 0.20f, 0.22f, 1.0f);

    // Decorative separator bar under title
    float sep_w = width * 0.55f;
    float sep_x = (width - sep_w) * 0.5f;
    float sep_y = sub_y + 24.0f * scale;
    draw_rect(verts, sep_x, sep_y, sep_w, 1.5f * scale, 0.75f, 0.18f, 0.20f, 0.70f);

    // Center Menu Card
    float card_w = width * 0.44f;
    if (card_w < 440.0f * scale) card_w = 440.0f * scale;
    float card_h = height * 0.62f;
    float card_x = (width - card_w) * 0.5f;
    float card_y = sep_y + 18.0f * scale;

    // Card background panel
    draw_rect(verts, card_x, card_y, card_w, card_h, 0.02f, 0.02f, 0.03f, 0.84f);
    // Card crimson top accent line
    draw_rect(verts, card_x, card_y, card_w, 2.5f * scale, 0.85f, 0.12f, 0.18f, 0.95f);
    // Border lines
    draw_rect(verts, card_x, card_y, 1.0f * scale, card_h, 0.25f, 0.08f, 0.10f, 0.60f);
    draw_rect(verts, card_x + card_w - 1.0f * scale, card_y, 1.0f * scale, card_h, 0.25f, 0.08f, 0.10f, 0.60f);

    // Menu Items
    float item_area_top = card_y + 24.0f * scale;
    float item_area_h = card_h - 100.0f * scale;
    float row_h = item_area_h / static_cast<float>(kLauncherItemCount);
    float row_y = item_area_top;
    float item_scale = scale * 0.48f;
    float item_track = 0.90f * scale;

    for (int i = 0; i < kLauncherItemCount; ++i) {
        bool selected = (i == g_launcher_index);
        float text_y = row_y + (row_h - 32.0f * item_scale) * 0.5f;

        if (selected) {
            draw_rect(verts, card_x + 14.0f * scale, row_y + 2.0f * scale, card_w - 28.0f * scale, row_h - 4.0f * scale, 0.60f, 0.08f, 0.10f, 0.92f);
            draw_rect(verts, card_x + 14.0f * scale, row_y + 2.0f * scale, 3.5f * scale, row_h - 4.0f * scale, 1.0f, 0.85f, 0.30f, 1.0f);

            char label[64];
            std::snprintf(label, sizeof(label), ">   %s", kLauncherItems[i]);
            draw_text_outlined(verts, card_x + 30.0f * scale, text_y, label, item_scale, item_track, 1.0f, 0.98f, 0.92f, 1.0f);

            if (i == 2) {
                float rw = text_width(label, item_scale, item_track);
                draw_text(verts, card_x + 34.0f * scale + rw, text_y + 2.0f * scale, "[COMBO ARENA]", item_scale * 0.78f, item_track * 0.8f, 1.0f, 0.82f, 0.32f, 1.0f);
            } else if (i == 3) {
                float rw = text_width(label, item_scale, item_track);
                draw_text(verts, card_x + 34.0f * scale + rw, text_y + 2.0f * scale, "[COMING SOON]", item_scale * 0.78f, item_track * 0.8f, 1.0f, 0.78f, 0.25f, 1.0f);
            }
        } else {
            draw_text(verts, card_x + 38.0f * scale, text_y, kLauncherItems[i], item_scale, item_track, 0.72f, 0.68f, 0.65f, 1.0f);
            if (i == 2) {
                float rw = text_width(kLauncherItems[i], item_scale, item_track);
                draw_text(verts, card_x + 46.0f * scale + rw, text_y + 2.0f * scale, "[COMBO ARENA]", item_scale * 0.78f, item_track * 0.8f, 0.82f, 0.68f, 0.25f, 0.85f);
            } else if (i == 3) {
                float rw = text_width(kLauncherItems[i], item_scale, item_track);
                draw_text(verts, card_x + 46.0f * scale + rw, text_y + 2.0f * scale, "[COMING SOON]", item_scale * 0.78f, item_track * 0.8f, 0.75f, 0.55f, 0.20f, 0.8f);
            }
        }

        row_y += row_h;
    }

    // Context / Subtext Box at bottom of card
    float subbox_y = card_y + card_h - 68.0f * scale;
    draw_rect(verts, card_x + 18.0f * scale, subbox_y, card_w - 36.0f * scale, 1.0f * scale, 0.50f, 0.15f, 0.18f, 0.60f);

    float desc_scale = scale * 0.36f;
    float desc_track = 0.5f * scale;
    draw_text(verts, card_x + 24.0f * scale, subbox_y + 16.0f * scale, kLauncherSubtext[g_launcher_index], desc_scale, desc_track, 0.85f, 0.80f, 0.65f, 1.0f);

    // Footer prompt
    const char* footer = "[D-PAD / UP-DOWN] Navigate     [A / ENTER] Select     [F7] In-Game Menu";
    float foot_scale = scale * 0.34f;
    float foot_track = 0.6f * scale;
    float foot_w = text_width(footer, foot_scale, foot_track);
    draw_text(verts, (width - foot_w) * 0.5f, height * 0.94f, footer, foot_scale, foot_track, 0.60f, 0.58f, 0.55f, 1.0f);

    submit(verts, width, height, 0.0f, g_font_view);
}

void draw_boss_rush_screen(float width, float height) {
    std::vector<Vertex> verts;
    verts.reserve(5000);

    if (g_bg_view) {
        draw_background(verts, width, height);
        submit(verts, width, height, 1.0f, g_bg_view);
    } else {
        draw_rect(verts, 0, 0, width, height, 0.05f, 0.02f, 0.02f, 1.0f);
        submit(verts, width, height, 0.0f, g_font_view);
    }

    draw_rect(verts, 0, 0, width, height, 0.02f, 0.01f, 0.02f, 0.72f);

    float scale = height / 900.0f;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.55f) scale = 1.55f;

    draw_rect(verts, 0, 0, width, 4.0f * scale, 0.85f, 0.12f, 0.18f, 1.0f);

    float card_w = width * 0.56f;
    if (card_w < 550.0f * scale) card_w = 550.0f * scale;
    float card_h = height * 0.80f;
    float card_x = (width - card_w) * 0.5f;
    float card_y = height * 0.08f;

    draw_rect(verts, card_x, card_y, card_w, card_h, 0.02f, 0.02f, 0.03f, 0.88f);
    draw_rect(verts, card_x, card_y, card_w, 2.5f * scale, 0.85f, 0.12f, 0.18f, 0.95f);

    float title_scale = scale * 0.68f;
    float track = 0.8f * scale;
    draw_text_outlined(verts, card_x + 30.0f * scale, card_y + 22.0f * scale, "BOSS RUSH MODE", title_scale, track, 0.98f, 0.92f, 0.80f, 1.0f);
    draw_text(verts, card_x + 30.0f * scale, card_y + 56.0f * scale, "GAUNTLET OF MALICE  //  UNDER ACTIVE DEVELOPMENT", scale * 0.38f, track, 0.95f, 0.65f, 0.20f, 1.0f);

    draw_rect(verts, card_x + 30.0f * scale, card_y + 86.0f * scale, card_w - 60.0f * scale, 1.5f * scale, 0.70f, 0.15f, 0.18f, 0.70f);

    // Status box
    float sb_y = card_y + 102.0f * scale;
    draw_rect(verts, card_x + 30.0f * scale, sb_y, card_w - 60.0f * scale, 38.0f * scale, 0.40f, 0.06f, 0.08f, 0.85f);
    draw_text(verts, card_x + 44.0f * scale, sb_y + 9.0f * scale, "STATUS: COMING SOON IN FUTURE DMC1 SWITCH UPDATE", scale * 0.40f, track, 1.0f, 0.88f, 0.35f, 1.0f);

    float text_y = sb_y + 56.0f * scale;
    float body_sc = scale * 0.38f;
    draw_text(verts, card_x + 32.0f * scale, text_y, "Consecutive combat trial featuring all 5 major DMC1 bosses:", body_sc, track, 0.88f, 0.84f, 0.80f, 1.0f);
    text_y += 30.0f * scale;

    const char* bosses[] = {
        "1.  PHANTOM       - Magma Scorpion of Mallet Castle",
        "2.  GRIFFON        - Winged Beast of Lightning & Wind",
        "3.  NELO ANGELO   - The Dark Knight Duel",
        "4.  NIGHTMARE     - Living Bioweapon Fluid Core",
        "5.  MUNDUS         - Lord of the Underworld Finale"
    };

    for (const char* b : bosses) {
        draw_rect(verts, card_x + 36.0f * scale, text_y - 2.0f * scale, card_w - 72.0f * scale, 24.0f * scale, 0.08f, 0.04f, 0.05f, 0.70f);
        draw_text(verts, card_x + 48.0f * scale, text_y + 2.0f * scale, b, body_sc * 0.95f, track * 0.7f, 0.95f, 0.80f, 0.50f, 1.0f);
        text_y += 32.0f * scale;
    }

    text_y += 8.0f * scale;
    draw_text(verts, card_x + 32.0f * scale, text_y, "Features being implemented:", body_sc, track, 0.80f, 0.78f, 0.75f, 1.0f);
    text_y += 24.0f * scale;
    draw_text(verts, card_x + 44.0f * scale, text_y, "* Continuous boss transitions with persistent health bar", body_sc * 0.9f, track * 0.6f, 0.70f, 0.68f, 0.65f, 1.0f);
    text_y += 22.0f * scale;
    draw_text(verts, card_x + 44.0f * scale, text_y, "* Real-time style rank progression and speedrun timer", body_sc * 0.9f, track * 0.6f, 0.70f, 0.68f, 0.65f, 1.0f);
    text_y += 22.0f * scale;
    draw_text(verts, card_x + 44.0f * scale, text_y, "* Hot-swapping Trickster, Swordmaster, Gunslinger, RoyalGuard", body_sc * 0.9f, track * 0.6f, 0.70f, 0.68f, 0.65f, 1.0f);

    float btn_y = card_y + card_h - 48.0f * scale;
    draw_rect(verts, card_x + 30.0f * scale, btn_y, card_w - 60.0f * scale, 34.0f * scale, 0.55f, 0.06f, 0.08f, 0.90f);
    const char* ret_text = "<  [B / ESC / A / ENTER] Return to Launcher  >";
    float rw = text_width(ret_text, scale * 0.40f, track);
    draw_text(verts, card_x + (card_w - rw) * 0.5f, btn_y + 7.0f * scale, ret_text, scale * 0.40f, track, 1.0f, 0.98f, 0.92f, 1.0f);

    submit(verts, width, height, 0.0f, g_font_view);
}

void draw_settings_screen(float width, float height) {
    std::vector<Vertex> verts;
    verts.reserve(6000);

    if (g_bg_view) {
        draw_background(verts, width, height);
        submit(verts, width, height, 1.0f, g_bg_view);
    } else {
        draw_rect(verts, 0, 0, width, height, 0.05f, 0.02f, 0.02f, 1.0f);
        submit(verts, width, height, 0.0f, g_font_view);
    }

    draw_rect(verts, 0, 0, width, height, 0.02f, 0.01f, 0.02f, 0.72f);

    float scale = height / 900.0f;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.55f) scale = 1.55f;

    draw_rect(verts, 0, 0, width, 4.0f * scale, 0.85f, 0.12f, 0.18f, 1.0f);

    float card_w = width * 0.58f;
    if (card_w < 560.0f * scale) card_w = 560.0f * scale;
    float card_h = height * 0.82f;
    float card_x = (width - card_w) * 0.5f;
    float card_y = height * 0.07f;

    draw_rect(verts, card_x, card_y, card_w, card_h, 0.02f, 0.02f, 0.03f, 0.88f);
    draw_rect(verts, card_x, card_y, card_w, 2.5f * scale, 0.85f, 0.12f, 0.18f, 0.95f);

    float title_scale = scale * 0.68f;
    float track = 0.8f * scale;
    draw_text_outlined(verts, card_x + 30.0f * scale, card_y + 20.0f * scale, "SETTINGS & CONTROLS", title_scale, track, 0.98f, 0.92f, 0.80f, 1.0f);
    draw_text(verts, card_x + 30.0f * scale, card_y + 54.0f * scale, "CRIMSON STYLE SWITCH CONFIGURATION", scale * 0.38f, track, 0.85f, 0.20f, 0.22f, 1.0f);

    draw_rect(verts, card_x + 30.0f * scale, card_y + 80.0f * scale, card_w - 60.0f * scale, 1.5f * scale, 0.70f, 0.15f, 0.18f, 0.70f);

    // Section 1: Gameplay Options
    float cur_y = card_y + 94.0f * scale;
    draw_text(verts, card_x + 32.0f * scale, cur_y, "GAMEPLAY OPTIONS", scale * 0.44f, track, 0.95f, 0.85f, 0.45f, 1.0f);
    cur_y += 28.0f * scale;

    const char* toggle_names[3] = {
        "Style HUD Display",
        "Trickster Fast Dash",
        "Weapon Quick Switch"
    };

    const char* toggle_vals[3] = {
        g_setting_hud_enabled ? "<   ON   >" : "<   OFF   >",
        g_setting_fast_dash ? "<  ENABLED  >" : "<  DISABLED  >",
        g_setting_quick_switch ? "<  ENABLED  >" : "<  DISABLED  >"
    };

    for (int i = 0; i < 3; ++i) {
        bool selected = (i == g_settings_row);
        float row_h = 30.0f * scale;
        if (selected) {
            draw_rect(verts, card_x + 30.0f * scale, cur_y - 2.0f * scale, card_w - 60.0f * scale, row_h, 0.55f, 0.08f, 0.10f, 0.90f);
            draw_text(verts, card_x + 44.0f * scale, cur_y + 3.0f * scale, toggle_names[i], scale * 0.40f, track, 1.0f, 0.98f, 0.90f, 1.0f);
            float vw = text_width(toggle_vals[i], scale * 0.40f, track);
            draw_text(verts, card_x + card_w - 50.0f * scale - vw, cur_y + 3.0f * scale, toggle_vals[i], scale * 0.40f, track, 1.0f, 0.95f, 0.45f, 1.0f);
        } else {
            draw_text(verts, card_x + 44.0f * scale, cur_y + 3.0f * scale, toggle_names[i], scale * 0.40f, track, 0.75f, 0.72f, 0.68f, 1.0f);
            float vw = text_width(toggle_vals[i], scale * 0.40f, track);
            draw_text(verts, card_x + card_w - 50.0f * scale - vw, cur_y + 3.0f * scale, toggle_vals[i], scale * 0.40f, track, 0.70f, 0.68f, 0.65f, 1.0f);
        }
        cur_y += 34.0f * scale;
    }

    cur_y += 10.0f * scale;
    draw_rect(verts, card_x + 30.0f * scale, cur_y, card_w - 60.0f * scale, 1.0f * scale, 0.40f, 0.10f, 0.12f, 0.60f);
    cur_y += 16.0f * scale;

    // Section 2: Controls Guide
    draw_text(verts, card_x + 32.0f * scale, cur_y, "STYLE SWITCH CONTROLS (D-PAD / KEYBOARD)", scale * 0.44f, track, 0.95f, 0.85f, 0.45f, 1.0f);
    cur_y += 28.0f * scale;

    const char* controls[] = {
        "[D-PAD UP]    / [Z]  -  TRICKSTER   (Dash, Sky Star, Air Trick)",
        "[D-PAD RIGHT] / [X]  -  SWORDMASTER (Advanced melee combat)",
        "[D-PAD LEFT]  / [C]  -  GUNSLINGER  (Firearm special abilities)",
        "[D-PAD DOWN]  / [V]  -  ROYALGUARD  (Guard block & Release)",
        "[B / CIRCLE]  / [L]  -  STYLE ACTION (Execute style move)",
        "[LEFT TRIGGER TAP]   -  CYCLE MELEE (Force Edge / Alastor / Ifrit / Sparda)",
        "[RIGHT TRIGGER TAP]  -  CYCLE GUNS   (Handguns / Shotgun / Grenadegun / Nightmare-B)"
    };

    for (const char* c : controls) {
        draw_text(verts, card_x + 40.0f * scale, cur_y, c, scale * 0.36f, track * 0.7f, 0.85f, 0.80f, 0.75f, 1.0f);
        cur_y += 24.0f * scale;
    }

    // Return button
    float btn_y = card_y + card_h - 48.0f * scale;
    draw_rect(verts, card_x + 30.0f * scale, btn_y, card_w - 60.0f * scale, 34.0f * scale, 0.55f, 0.06f, 0.08f, 0.90f);
    const char* ret_text = "<  [UP / DOWN] Select    [LEFT / RIGHT / ENTER] Toggle    [B / ESC] Return  >";
    float rw = text_width(ret_text, scale * 0.38f, track * 0.6f);
    draw_text(verts, card_x + (card_w - rw) * 0.5f, btn_y + 7.0f * scale, ret_text, scale * 0.38f, track * 0.6f, 1.0f, 0.98f, 0.92f, 1.0f);

    submit(verts, width, height, 0.0f, g_font_view);
}

void draw_title_menu(float width, float height) {
    draw_launcher_menu(width, height);
}

void draw_mission_screen(float width, float height) {
    std::vector<Vertex> verts;
    verts.reserve(6000);
    if (g_bg_view) {
        draw_background(verts, width, height);
        submit(verts, width, height, 1.0f, g_bg_view);
    } else {
        draw_rect(verts, 0, 0, width, height, 0.08f, 0.0f, 0.0f, 1.0f);
        submit(verts, width, height, 0.0f, g_font_view);
    }
    float scale = height / 900.0f;
    if (scale < 0.7f) {
        scale = 0.7f;
    }
    if (scale > 1.45f) {
        scale = 1.45f;
    }
    float panel_w = width * 0.38f;
    float panel_x = width - panel_w - width * 0.035f;
    float panel_y = height * 0.045f;
    float panel_h = height * 0.91f;
    draw_rect(verts, panel_x, panel_y, panel_w, panel_h, 0.02f, 0.0f, 0.0f, 0.72f);
    draw_text(verts, panel_x + 24, panel_y + 14, "MISSION SELECT", scale * 0.62f, 0.8f * scale, 0.93f, 0.78f, 0.42f, 1.0f);

    float list_top = panel_y + 58;
    float list_bottom = panel_y + panel_h - 88;
    float row_h = (list_bottom - list_top) / static_cast<float>(kMissionCount);
    float row_scale = (row_h - 4.0f) / static_cast<float>(kCell);
    if (row_scale > scale * 0.48f) {
        row_scale = scale * 0.48f;
    }
    if (row_scale < 0.32f) {
        row_scale = 0.32f;
    }
    float row_y = list_top;
    for (int i = 0; i < kMissionCount; ++i) {
        if (i == g_mission) {
            draw_rect(verts, panel_x + 12, row_y - 1, panel_w - 24, row_h - 2, 0.55f, 0.05f, 0.04f, 0.92f);
            draw_text(verts, panel_x + 22, row_y, kMissions[i], row_scale, 0.15f, 1.0f, 0.97f, 0.92f, 1.0f);
        } else {
            draw_text(verts, panel_x + 22, row_y, kMissions[i], row_scale, 0.15f, 0.72f, 0.68f, 0.64f, 1.0f);
        }
        row_y += row_h;
    }
    char difficulty[64];
    std::snprintf(difficulty, sizeof(difficulty), "<   %s   >", kModeNames[g_mode]);
    draw_text(verts, panel_x + 22, panel_y + panel_h - 72, "DIFFICULTY", scale * 0.4f, 0.6f * scale, 0.75f, 0.62f, 0.36f, 1.0f);
    draw_text(verts, panel_x + 22, panel_y + panel_h - 42, difficulty, scale * 0.52f, 0.4f, 0.96f, 0.94f, 0.88f, 1.0f);
    submit(verts, width, height, 0.0f, g_font_view);
}

void draw_style_hud(float width, float height) {
    StyleType style = style_current();
    if (style == StyleType::None) {
        return;
    }

    std::vector<Vertex> verts;
    verts.reserve(256);

    float scale = height / 1080.0f;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.4f) scale = 1.4f;

    const char* s_name = style_name(style);
    const char* a_name = style_action_name();

    char label[64];
    if (a_name && a_name[0] != '\0') {
        std::snprintf(label, sizeof(label), "%s [%s]", s_name, a_name);
    } else {
        std::snprintf(label, sizeof(label), "%s", s_name);
    }

    float text_sc = scale * 0.44f;
    float tracking = 0.85f * scale;
    float text_w = text_width(label, text_sc, tracking);

    float pad_x = 12.0f * scale;
    float pad_y = 6.0f * scale;
    float box_w = text_w + pad_x * 2.0f;
    float box_h = 24.0f * scale + pad_y * 2.0f;

    float x = 36.0f * scale;
    float y = height - box_h - 26.0f * scale;

    draw_rect(verts, x, y, box_w, box_h, 0.04f, 0.04f, 0.06f, 0.72f);

    float r = 1.0f, g = 0.84f, b = 0.22f; // Trickster Gold
    if (style == StyleType::RoyalGuard) {
        r = 0.92f; g = 0.22f; b = 0.22f;
    } else if (style == StyleType::Gunslinger) {
        r = 0.25f; g = 0.75f; b = 1.0f;
    } else if (style == StyleType::Swordmaster) {
        r = 1.0f; g = 0.45f; b = 0.15f;
    }
    draw_rect(verts, x, y, box_w, 2.0f * scale, r, g, b, 0.90f);

    draw_text_outlined(verts, x + pad_x, y + pad_y, label, text_sc, tracking, r, g, b, 1.0f);

    if (mod_is_the_void()) {
        const char* void_title = "THE VOID  //  COMBO ARENA";
        char void_sub[128] = "[F5 / SELECT] Reset Position  |  Infinite HP & DT";
        char coord_buf[64] = {};
        if (mod_format_coords(coord_buf, sizeof(coord_buf))) {
            std::snprintf(void_sub, sizeof(void_sub), "[F5] Reset | %s", coord_buf);
        }
        float v_sc = scale * 0.38f;
        float v_sub_sc = scale * 0.30f;
        float v_track = 0.8f * scale;
        float v_w1 = text_width(void_title, v_sc, v_track);
        float v_w2 = text_width(void_sub, v_sub_sc, 0.4f * scale);
        float v_box_w = (v_w1 > v_w2 ? v_w1 : v_w2) + pad_x * 2.0f;
        float v_box_h = 36.0f * scale + pad_y * 2.0f;
        float v_y = y - v_box_h - 10.0f * scale;

        draw_rect(verts, x, v_y, v_box_w, v_box_h, 0.03f, 0.02f, 0.04f, 0.78f);
        draw_rect(verts, x, v_y, v_box_w, 2.0f * scale, 0.85f, 0.15f, 0.20f, 0.95f);
        draw_text_outlined(verts, x + pad_x, v_y + pad_y, void_title, v_sc, v_track, 1.0f, 0.88f, 0.40f, 1.0f);
        draw_text(verts, x + pad_x, v_y + pad_y + 18.0f * scale, void_sub, v_sub_sc, 0.4f * scale, 1.0f, 0.85f, 0.40f, 0.95f);
    }

    submit(verts, width, height, 0.0f, g_font_view);
}

void draw_screen(float width, float height) {
    if (g_screen == Screen::Launcher) {
        draw_launcher_menu(width, height);
    } else if (g_screen == Screen::Missions) {
        draw_mission_screen(width, height);
    } else if (g_screen == Screen::BossRush) {
        draw_boss_rush_screen(width, height);
    } else if (g_screen == Screen::Settings) {
        draw_settings_screen(width, height);
    } else if (g_screen == Screen::Title) {
        draw_launcher_menu(width, height);
    } else if (mod_in_game() && g_setting_hud_enabled) {
        draw_style_hud(width, height);
    }
}

void move_mission(int delta) {
    g_mission += delta;
    if (g_mission < 0) {
        g_mission = 0;
    }
    if (g_mission >= kMissionCount) {
        g_mission = kMissionCount - 1;
    }
}

void move_mode(int delta) {
    g_mode += delta;
    if (g_mode < 0) {
        g_mode = 0;
    }
    if (g_mode > 3) {
        g_mode = 3;
    }
}

void start_inject(const WORD* words, int count, bool keep_menu) {
    if (count > 12) {
        count = 12;
    }
    g_inject_keep = keep_menu;
    g_inject_count = count;
    g_inject_pos = 0;
    g_inject_frames = 0;
    for (int i = 0; i < count; ++i) {
        g_inject_words[i] = words[i];
    }
    g_inject_buttons.store(count > 0 ? words[0] : 0);
    g_inject_on.store(count > 0);
    if (!keep_menu) {
        g_screen = Screen::None;
        g_probe_cooldown = 50;
    }
}

void tick_inject() {
    if (!g_inject_on.load()) {
        return;
    }
    g_inject_frames++;
    if (g_inject_frames < 10) {
        return;
    }
    g_inject_frames = 0;
    g_inject_pos++;
    if (g_inject_pos >= g_inject_count) {
        g_inject_on.store(false);
        g_wait_release.store(true);
        if (!g_inject_keep) {
            g_capture.store(false);
            g_screen = Screen::None;
        }
        return;
    }
    g_inject_buttons.store(g_inject_words[g_inject_pos]);
}

void activate_game_item(int game_index) {
    WORD steps[12] = {};
    int count = 0;
    int delta = game_index - g_game_index;
    WORD move = delta > 0 ? static_cast<WORD>(0x0002) : static_cast<WORD>(0x0001);
    int taps = delta < 0 ? -delta : delta;
    for (int i = 0; i < taps && count + 2 < 12; ++i) {
        steps[count++] = move;
        steps[count++] = 0;
    }
    steps[count++] = 0x1000;
    steps[count++] = 0;
    g_capture.store(true);
    start_inject(steps, count, false);
}

void begin_load_seek() {
    g_seek = true;
    g_seek_pulse = 0;
    g_seek_gap = 2;
    g_seek_tries = 0;
    g_seek_word = 0x1000;
    g_seek_stick = 0;
    g_seek_confirming = false;
    g_capture.store(true);
    log_line("confirming load data");
}

void finish_load_seek() {
    g_seek = false;
    g_seek_pulse = 0;
    g_seek_confirming = false;
    g_screen = Screen::None;
    g_probe_cooldown = 45;
    g_capture.store(false);
    g_wait_release.store(true);
}

void show_launcher(bool force = false) {
    if (g_launcher_dismissed && !force) {
        return;
    }
    g_launcher_index = 0;
    g_screen = Screen::Launcher;
    g_capture.store(true);
    g_menu_lock = 10;
    g_prev_buttons = g_buttons.load();
    if (!g_logged_menu) {
        g_logged_menu = true;
        log_line("launcher menu shown");
    }
}

void show_title() {
    show_launcher(false);
}

void hide_menu() {
    g_screen = Screen::None;
    g_capture.store(false);
}

int pixel_lum(const uint8_t* px) {
    int lum = px[0];
    if (px[1] > lum) {
        lum = px[1];
    }
    if (px[2] > lum) {
        lum = px[2];
    }
    return lum;
}

float average_lum(const uint8_t* pixels, int width, int height, int pitch, int y0, int y1) {
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > height) {
        y1 = height;
    }
    int x0 = width / 5;
    int x1 = width - x0;
    long long sum = 0;
    int count = 0;
    for (int y = y0; y < y1; y += 2) {
        const uint8_t* row = pixels + y * pitch;
        for (int x = x0; x < x1; x += 4) {
            sum += pixel_lum(row + x * 4);
            count++;
        }
    }
    if (count == 0) {
        return 255.0f;
    }
    return static_cast<float>(sum) / static_cast<float>(count);
}

void tick_seek() {
    if (!g_seek) {
        return;
    }
    if (g_seek_pulse > 0) {
        g_seek_pulse--;
        if (g_seek_pulse == 0 && g_seek_confirming) {
            log_line("load data confirmed");
            finish_load_seek();
        }
        return;
    }
    if (g_seek_gap > 0) {
        g_seek_gap--;
        return;
    }
    g_seek_word = 0x1000;
    g_seek_stick = 0;
    g_seek_confirming = true;
    g_seek_pulse = 2;
}

bool text_line(const uint8_t* pixels, int width, int height, int pitch, int y0, int y1, int min_lum, float min_width, float max_width) {
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > height) {
        y1 = height;
    }
    if (y1 - y0 < 4) {
        return false;
    }
    int run = 0;
    int best = 0;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = pixels + y * pitch;
        int left = width;
        int right = -1;
        int bright = 0;
        for (int x = 0; x < width; ++x) {
            const uint8_t* px = row + x * 4;
            int hi = pixel_lum(px);
            int lo = px[0];
            if (px[1] < lo) {
                lo = px[1];
            }
            if (px[2] < lo) {
                lo = px[2];
            }
            if (hi >= min_lum && hi - lo <= 28) {
                bright++;
                if (x < left) {
                    left = x;
                }
                right = x;
            }
        }
        int span_px = right >= left ? right - left + 1 : 0;
        float span = span_px > 0 ? static_cast<float>(span_px) / static_cast<float>(width) : 0.0f;
        float fill = span_px > 0 ? static_cast<float>(bright) / static_cast<float>(span_px) : 0.0f;
        int hole = 0;
        int widest_hole = 0;
        int mid_bright = 0;
        if (span_px > 0) {
            int mid0 = left + span_px / 3;
            int mid1 = left + (span_px * 2) / 3;
            for (int x = left; x <= right; ++x) {
                const uint8_t* px = row + x * 4;
                int hi = pixel_lum(px);
                int lo = px[0];
                if (px[1] < lo) {
                    lo = px[1];
                }
                if (px[2] < lo) {
                    lo = px[2];
                }
                bool on = hi >= min_lum && hi - lo <= 28;
                if (on) {
                    hole = 0;
                    if (x >= mid0 && x <= mid1) {
                        mid_bright++;
                    }
                } else if (++hole > widest_hole) {
                    widest_hole = hole;
                }
            }
        }
        int mid_span = span_px > 0 ? span_px / 3 : 1;
        float mid_fill = static_cast<float>(mid_bright) / static_cast<float>(mid_span);
        bool split_labels = widest_hole > width / 8;
        bool hit = span >= min_width && span <= max_width && fill >= 0.05f && fill <= 0.48f && mid_fill >= 0.08f && !split_labels &&
            (left + right) > width / 3 && (left + right) < width * 5 / 3;
        if (hit) {
            run++;
            if (run > best) {
                best = run;
            }
        } else if (run > 0) {
            run = 0;
        }
    }
    int window = y1 - y0;
    return best >= window / 8 && best <= window * 2 / 3;
}

bool split_bottom_labels(const uint8_t* pixels, int width, int height, int pitch, int y0, int y1) {
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 > height) {
        y1 = height;
    }
    int split_rows = 0;
    for (int y = y0; y < y1; ++y) {
        const uint8_t* row = pixels + y * pitch;
        int left = -1;
        int right = -1;
        int hole = 0;
        int widest = 0;
        for (int x = 0; x < width; ++x) {
            const uint8_t* px = row + x * 4;
            int hi = pixel_lum(px);
            int lo = px[0];
            if (px[1] < lo) {
                lo = px[1];
            }
            if (px[2] < lo) {
                lo = px[2];
            }
            if (hi >= 140 && hi - lo <= 40) {
                if (left < 0) {
                    left = x;
                }
                right = x;
                hole = 0;
            } else if (left >= 0 && ++hole > widest) {
                widest = hole;
            }
        }
        if (left >= 0 && right - left > width / 2 && widest > width / 7) {
            split_rows++;
        }
    }
    return split_rows >= 3;
}

void classify_probe(const uint8_t* pixels, int width, int height, int pitch) {
    if (g_launcher_dismissed || g_screen != Screen::None || g_inject_on.load() || g_probe_cooldown > 0) {
        return;
    }
    if (width < 16 || height < 16) {
        return;
    }
    auto row_at = [&](float global) {
        float local = (global - 0.14f) / 0.84f;
        int y = static_cast<int>(local * static_cast<float>(height));
        if (y < 0) {
            y = 0;
        }
        if (y > height) {
            y = height;
        }
        return y;
    };
    if (split_bottom_labels(pixels, width, height, pitch, row_at(0.86f), row_at(0.99f))) {
        g_title_hits = 0;
        g_title_miss = 0;
        if (g_screen == Screen::Title) {
            hide_menu();
        }
        return;
    }
    float backdrop = average_lum(pixels, width, height, pitch, row_at(0.70f), row_at(0.88f));
    bool title = backdrop < 58.0f && text_line(pixels, width, height, pitch, row_at(0.90f), row_at(0.975f), 145, 0.35f, 1.0f);
    if (title) {
        g_title_miss = 0;
        if (g_screen == Screen::Title) {
            g_title_hits = 10;
            return;
        }
        if (++g_title_hits >= 10) {
            show_title();
        }
        return;
    }
    g_title_hits = 0;
    if (g_screen == Screen::Title && ++g_title_miss > 8) {
        hide_menu();
    }
}

bool edge_key(int index, int vk) {
    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    bool pressed = down && !g_key_down[index];
    g_key_down[index] = down;
    return pressed;
}

void update_menu_input() {
    WORD buttons = g_buttons.load();
    WORD pressed = buttons & ~g_prev_buttons;
    g_prev_buttons = buttons;
    bool up = (pressed & 0x0001) || edge_key(0, VK_UP) || edge_key(7, 'W');
    bool down = (pressed & 0x0002) || edge_key(1, VK_DOWN) || edge_key(8, 'S');
    bool left = (pressed & 0x0004) || edge_key(2, VK_LEFT) || edge_key(9, 'A');
    bool right = (pressed & 0x0008) || edge_key(3, VK_RIGHT) || edge_key(10, 'D');
    bool confirm = (pressed & 0x1000) || edge_key(4, VK_RETURN) || edge_key(11, VK_SPACE);
    bool close = (pressed & 0x2000) || edge_key(5, VK_ESCAPE) || edge_key(12, VK_BACK);
    bool reopen = edge_key(6, VK_F7);
    if (g_inject_on.load() || g_seek) {
        return;
    }
    if (reopen) {
        show_launcher(true);
        return;
    }
    if (g_screen == Screen::None) {
        g_capture.store(false);
        return;
    }
    g_capture.store(true);
    if (g_menu_lock > 0) {
        g_menu_lock--;
        return;
    }

    if (g_screen == Screen::Launcher || g_screen == Screen::Title) {
        if (up && g_launcher_index > 0) {
            g_launcher_index--;
        }
        if (down && g_launcher_index < kLauncherItemCount - 1) {
            g_launcher_index++;
        }
        if (confirm) {
            switch (g_launcher_index) {
                case 0: // LOAD DATA
                    g_launcher_dismissed = true;
                    mod_clear_mission();
                    mod_release_hold();
                    mod_open_load_menu();
                    g_screen = Screen::None;
                    g_capture.store(false);
                    g_wait_release.store(true);
                    break;
                case 1: // MISSION SELECT
                    g_screen = Screen::Missions;
                    g_menu_lock = 12;
                    break;
                case 2: // THE VOID
                    g_launcher_dismissed = true;
                    mod_arm_the_void(kModeValues[g_mode]);
                    g_screen = Screen::None;
                    g_capture.store(false);
                    g_probe_cooldown = 45;
                    g_wait_release.store(true);
                    mod_begin_mission();
                    break;
                case 3: // BOSS RUSH
                    g_screen = Screen::BossRush;
                    g_menu_lock = 10;
                    break;
                case 4: // SETTINGS
                    g_screen = Screen::Settings;
                    g_menu_lock = 10;
                    break;
                case 5: // START GAME
                    g_launcher_dismissed = true;
                    mod_arm_mission(1, kModeValues[g_mode]);
                    g_screen = Screen::None;
                    g_capture.store(false);
                    g_wait_release.store(true);
                    mod_begin_mission();
                    break;
                case 6: // EXIT TO HD COLLECTION
                    ExitProcess(0);
                    break;
            }
        }
        return;
    }

    if (g_screen == Screen::BossRush) {
        if (close || confirm) {
            g_screen = Screen::Launcher;
            g_menu_lock = 10;
        }
        return;
    }

    if (g_screen == Screen::Settings) {
        if (up && g_settings_row > 0) {
            g_settings_row--;
        }
        if (down && g_settings_row < 2) {
            g_settings_row++;
        }
        if (left || right || confirm) {
            if (g_settings_row == 0) {
                g_setting_hud_enabled = !g_setting_hud_enabled;
            } else if (g_settings_row == 1) {
                g_setting_fast_dash = !g_setting_fast_dash;
            } else if (g_settings_row == 2) {
                g_setting_quick_switch = !g_setting_quick_switch;
            }
        }
        if (close) {
            g_screen = Screen::Launcher;
            g_menu_lock = 10;
        }
        return;
    }

    if (g_screen != Screen::Missions) {
        return;
    }
    if (up) {
        move_mission(-1);
    }
    if (down) {
        move_mission(1);
    }
    if (left) {
        move_mode(-1);
    }
    if (right) {
        move_mode(1);
    }
    if (confirm) {
        g_launcher_dismissed = true;
        mod_arm_mission(g_mission + 1, kModeValues[g_mode]);
        g_screen = Screen::None;
        g_capture.store(false);
        g_probe_cooldown = 45;
        g_wait_release.store(true);
        mod_begin_mission();
        return;
    }
    if (close) {
        g_screen = Screen::Launcher;
        g_menu_lock = 10;
    }
}

void queue_probe(ID3D11Texture2D* back, const D3D11_TEXTURE2D_DESC& desc) {
    if (desc.SampleDesc.Count != 1 || desc.Width < 64 || desc.Height < 64) {
        return;
    }
    D3D11_BOX box{};
    box.left = desc.Width * 25 / 100;
    box.right = desc.Width * 75 / 100;
    box.top = desc.Height * 14 / 100;
    box.bottom = desc.Height * 98 / 100;
    box.back = 1;
    UINT width = box.right - box.left;
    UINT height = box.bottom - box.top;
    if (!g_probe || g_probe_w != static_cast<int>(width) || g_probe_h != static_cast<int>(height)) {
        if (g_probe) {
            g_probe->Release();
            g_probe = nullptr;
        }
        D3D11_TEXTURE2D_DESC stage = desc;
        stage.Width = width;
        stage.Height = height;
        stage.MipLevels = 1;
        stage.ArraySize = 1;
        stage.SampleDesc.Count = 1;
        stage.SampleDesc.Quality = 0;
        stage.Usage = D3D11_USAGE_STAGING;
        stage.BindFlags = 0;
        stage.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stage.MiscFlags = 0;
        if (FAILED(g_device->CreateTexture2D(&stage, nullptr, &g_probe))) {
            return;
        }
        g_probe_w = static_cast<int>(width);
        g_probe_h = static_cast<int>(height);
    }
    g_context->CopySubresourceRegion(g_probe, 0, 0, 0, 0, back, 0, &box);
    g_probe_pending = true;
}

void consume_probe() {
    if (!g_probe_pending || !g_probe) {
        return;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_context->Map(g_probe, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
        return;
    }
    classify_probe(static_cast<const uint8_t*>(mapped.pData), g_probe_w, g_probe_h, static_cast<int>(mapped.RowPitch));
    g_context->Unmap(g_probe, 0);
    g_probe_pending = false;
}

using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
ResizeFn g_resize = nullptr;

void paint(IDXGISwapChain* swap) {
    ID3D11Device* device = nullptr;
    if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device))) || !device) {
        return;
    }
    if (device != g_device && !init_renderer(device)) {
        device->Release();
        return;
    }
    device->Release();
    if (!g_ready || !g_context) {
        return;
    }
    tick_inject();
    tick_seek();
    if (g_probe_cooldown > 0) {
        g_probe_cooldown--;
    }
    consume_probe();
    update_menu_input();
    ID3D11Texture2D* back = nullptr;
    if (FAILED(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back)))) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    if (!g_inject_on.load() && g_probe_cooldown == 0 && !g_probe_pending) {
        queue_probe(back, desc);
    }
    if (g_screen == Screen::None && !mod_in_game()) {
        back->Release();
        return;
    }
    ID3D11RenderTargetView* rtv = nullptr;
    HRESULT rtv_result = g_device->CreateRenderTargetView(back, nullptr, &rtv);
    back->Release();
    if (FAILED(rtv_result) || !rtv) {
        return;
    }
    DxState saved{};
    save_state(g_context, saved);
    ID3D11ShaderResourceView* unbound[kSrvSlots] = {};
    g_context->PSSetShaderResources(0, kSrvSlots, unbound);
    g_context->VSSetShaderResources(0, kSrvSlots, unbound);
    g_context->GSSetShaderResources(0, kSrvSlots, unbound);
    g_context->OMSetRenderTargets(1, &rtv, nullptr);
    static bool logged_frame = false;
    if (!logged_frame && g_screen == Screen::Title) {
        logged_frame = true;
        float vx = 0, vy = 0, vw = 0, vh = 0;
        if (saved.viewport_count > 0) {
            vx = saved.viewports[0].TopLeftX;
            vy = saved.viewports[0].TopLeftY;
            vw = saved.viewports[0].Width;
            vh = saved.viewports[0].Height;
        }
        log_line("title overlay buffer %u x %u viewport %.0f,%.0f %.0fx%.0f anchor %.3f", desc.Width, desc.Height, vx, vy, vw, vh, g_menu_anchor);
    }
    draw_screen(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
    restore_state(g_context, saved);
    rtv->Release();
}

HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain* swap, UINT sync, UINT flags) {
    paint(swap);
    HRESULT result = g_present(swap, sync, flags);
    if (FAILED(result) && !g_logged_present_fail) {
        g_logged_present_fail = true;
        log_line("Present failed 0x%08lX", static_cast<unsigned long>(result));
        if (g_device) {
            log_line("device removed reason 0x%08lX", static_cast<unsigned long>(g_device->GetDeviceRemovedReason()));
        }
    }
    return result;
}

HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain* swap, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    log_line("swapchain resize %ux%u", width, height);
    HRESULT result = g_resize(swap, count, width, height, format, flags);
    if (FAILED(result)) {
        log_line("swapchain resize failed 0x%08lX", static_cast<unsigned long>(result));
    }
    return result;
}

void hook_swap_chain(IDXGISwapChain* swap) {
    if (!swap || g_present) {
        return;
    }
    auto** vtable = *reinterpret_cast<void***>(swap);
    g_vtable_copy = new void*[18];
    std::memcpy(g_vtable_copy, vtable, sizeof(void*) * 18);
    g_present = reinterpret_cast<PresentFn>(g_vtable_copy[8]);
    g_resize = reinterpret_cast<ResizeFn>(g_vtable_copy[13]);
    g_vtable_copy[8] = reinterpret_cast<void*>(present_hook);
    g_vtable_copy[13] = reinterpret_cast<void*>(resize_hook);
    *reinterpret_cast<void***>(swap) = g_vtable_copy;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (SUCCEEDED(swap->GetDesc(&desc))) {
        log_line("present hook installed %ux%u effect %u", desc.BufferDesc.Width, desc.BufferDesc.Height, desc.SwapEffect);
    } else {
        log_line("present hook installed");
    }
}

HRESULT WINAPI create_hook(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver, HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* levels, UINT level_count, UINT sdk, const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swap, ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context) {
    HRESULT result = g_create(adapter, driver, software, flags, levels, level_count, sdk, desc, swap, device, level, context);
    if (SUCCEEDED(result) && swap && *swap) {
        hook_swap_chain(*swap);
    }
    return result;
}

bool hook_import(const char* dll_name, const char* function_name, void* detour, void** original) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return false;
    }
    auto* import_desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; import_desc->Name; ++import_desc) {
        const char* name = reinterpret_cast<const char*>(base + import_desc->Name);
        if (_stricmp(name, dll_name) != 0) {
            continue;
        }
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + import_desc->FirstThunk);
        auto* original_thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (import_desc->OriginalFirstThunk ? import_desc->OriginalFirstThunk : import_desc->FirstThunk));
        for (; original_thunk->u1.AddressOfData; ++original_thunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal)) {
                continue;
            }
            auto* entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + original_thunk->u1.AddressOfData);
            if (std::strcmp(entry->Name, function_name) != 0) {
                continue;
            }
            DWORD old = 0;
            VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
            *original = reinterpret_cast<void*>(thunk->u1.Function);
            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(detour);
            VirtualProtect(&thunk->u1.Function, sizeof(void*), old, &old);
            return true;
        }
    }
    return false;
}

HHOOK g_key_hook = nullptr;

LRESULT CALLBACK keyboard_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if (g_capture.load() && !g_inject_on.load()) {
            switch (info->vkCode) {
            case VK_UP:
            case VK_DOWN:
            case VK_LEFT:
            case VK_RIGHT:
            case VK_RETURN:
            case VK_SPACE:
            case VK_ESCAPE:
            case VK_BACK:
            case 'W':
            case 'S':
            case 'A':
            case 'D':
                return 1;
            }
        }
        if (mod_in_game()) {
            bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
            switch (info->vkCode) {
            case 'L':
                if (down) {
                    style_notify_key_action();
                }
                return 1;
            case 'Z':
                if (down) {
                    style_set(StyleType::Trickster);
                }
                return 1;
            case 'X':
                if (down) {
                    style_set(StyleType::Swordmaster);
                }
                return 1;
            case 'C':
                if (down) {
                    style_set(StyleType::Gunslinger);
                }
                return 1;
            case 'V':
                if (down) {
                    style_set(StyleType::RoyalGuard);
                }
                return 1;
            }
        }
    }
    return CallNextHookEx(g_key_hook, code, wp, lp);
}

DWORD WINAPI keyboard_thread(void*) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(keyboard_hook), &module);
    g_key_hook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, module, 0);
    if (!g_key_hook) {
        log_line("keyboard hook failed");
    }
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void start_keyboard_hook() {
    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    CreateThread(nullptr, 0, keyboard_thread, nullptr, 0, nullptr);
}

}  // namespace

void overlay_on_pad(XInputState* state) {
    if (!state) {
        return;
    }
    WORD buttons = state->gamepad.buttons;
    g_buttons.store(buttons);
    if (g_seek_pulse > 0) {
        state->gamepad.leftX = 0;
        state->gamepad.leftY = g_seek_stick;
        state->gamepad.buttons = g_seek_word;
        return;
    }
    bool held = g_capture.load() || g_inject_on.load() || g_wait_release.load() || g_seek;
    if (held) {
        state->gamepad.leftX = 0;
        state->gamepad.leftY = 0;
    }
    if (g_inject_on.load()) {
        state->gamepad.buttons = g_inject_buttons.load();
        return;
    }
    if (g_wait_release.load()) {
        state->gamepad.buttons = static_cast<WORD>(buttons & ~kMenuButtons);
        if ((buttons & kMenuButtons) == 0) {
            g_wait_release.store(false);
        }
    }
    if (g_capture.load()) {
        state->gamepad.buttons = static_cast<WORD>(buttons & ~(kMenuButtons | 0x0010));
    }
}

void overlay_install() {
    start_keyboard_hook();
    if (!hook_import("d3d11.dll", "D3D11CreateDeviceAndSwapChain", reinterpret_cast<void*>(create_hook), reinterpret_cast<void**>(&g_create))) {
        log_line("could not hook D3D11CreateDeviceAndSwapChain");
        return;
    }
    log_line("swap chain create hooked");
}
