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
