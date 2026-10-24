#include "style_hud.h"
#include "style_assets.h"
#include "style_trickster.h"
#include "the_void_menu.h"
#include "mod.h"

#include <d3d11.h>
#include <gdiplus.h>
#include <ole2.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

// Drawing helpers & engine functions from ui.cpp
extern void draw_rect(std::vector<Vertex>& verts, float x, float y, float w, float h, float r, float g, float b, float a);
extern void draw_text(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a);
extern void draw_text_outlined(std::vector<Vertex>& verts, float x, float y, const char* text, float scale, float tracking, float r, float g, float b, float a);
extern float text_width(const char* text, float scale, float tracking);
extern void submit(std::vector<Vertex>& verts, float width, float height, float tex_mode, ID3D11ShaderResourceView* texture);
extern ID3D11ShaderResourceView* ui_get_font_view();

namespace {

constexpr float kAtlas = 640.0f;
constexpr float kWhiteUv = 4.0f / kAtlas;
constexpr float kPi = 3.14159265358979323846f;

ID3D11ShaderResourceView* g_glass_view = nullptr;
int g_glass_w = 0;
int g_glass_h = 0;

StyleType g_prev_style = StyleType::None;
uint64_t g_switch_tick = 0;
constexpr float kSwitchAnimDurationMs = 280.0f;

inline uint64_t get_time_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void push_vertex(std::vector<Vertex>& verts, float x, float y, float u, float v, float r, float g, float b, float a) {
    verts.push_back(Vertex{x, y, u, v, r, g, b, a});
}

inline void push_textured_quad(std::vector<Vertex>& verts,
                              float x, float y, float w, float h,
                              float u0, float v0, float u1, float v1,
                              float r, float g, float b, float a) {
    Vertex q[6] = {
        {x,     y,     u0, v0, r, g, b, a},
        {x + w, y,     u1, v0, r, g, b, a},
        {x,     y + h, u0, v1, r, g, b, a},
        {x,     y + h, u0, v1, r, g, b, a},
        {x + w, y,     u1, v0, r, g, b, a},
        {x + w, y + h, u1, v1, r, g, b, a},
    };
    verts.insert(verts.end(), q, q + 6);
}

void push_solid_quad(std::vector<Vertex>& verts,
                     float x0, float y0, float x1, float y1,
                     float x2, float y2, float x3, float y3,
                     float r, float g, float b, float a) {
    Vertex q[6] = {
        {x0, y0, kWhiteUv, kWhiteUv, r, g, b, a},
        {x1, y1, kWhiteUv, kWhiteUv, r, g, b, a},
        {x2, y2, kWhiteUv, kWhiteUv, r, g, b, a},
        {x0, y0, kWhiteUv, kWhiteUv, r, g, b, a},
        {x2, y2, kWhiteUv, kWhiteUv, r, g, b, a},
        {x3, y3, kWhiteUv, kWhiteUv, r, g, b, a},
    };
    verts.insert(verts.end(), q, q + 6);
}

void push_thick_line(std::vector<Vertex>& verts,
                     float x0, float y0, float x1, float y1,
                     float thickness, float r, float g, float b, float a) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    float nx = -dy / len * (thickness * 0.5f);
    float ny =  dx / len * (thickness * 0.5f);

    push_solid_quad(verts,
                    x0 + nx, y0 + ny,
                    x1 + nx, y1 + ny,
                    x1 - nx, y1 - ny,
                    x0 - nx, y0 - ny,
                    r, g, b, a);
}

void push_ring_arc(std::vector<Vertex>& verts,
                   float cx, float cy, float r_in, float r_out,
                   float a_start, float a_end, int segments,
                   float r, float g, float b, float a) {
    if (segments < 1) segments = 1;
    float da = (a_end - a_start) / static_cast<float>(segments);
    for (int i = 0; i < segments; ++i) {
        float a0 = a_start + static_cast<float>(i) * da;
        float a1 = a0 + da;
        float c0 = std::cos(a0), s0 = std::sin(a0);
        float c1 = std::cos(a1), s1 = std::sin(a1);

        float p0_x = cx + c0 * r_in,  p0_y = cy + s0 * r_in;
        float p1_x = cx + c0 * r_out, p1_y = cy + s0 * r_out;
        float p2_x = cx + c1 * r_out, p2_y = cy + s1 * r_out;
        float p3_x = cx + c1 * r_in,  p3_y = cy + s1 * r_in;

        push_solid_quad(verts, p0_x, p0_y, p1_x, p1_y, p2_x, p2_y, p3_x, p3_y, r, g, b, a);
    }
}

struct StyleColor {
    float r, g, b;
};

StyleColor get_active_style_color(StyleType type) {
    switch (type) {
        case StyleType::Trickster:   return {1.00f, 0.68f, 0.10f}; // Radiant gold / fiery amber
        case StyleType::Swordmaster: return {0.98f, 0.15f, 0.15f}; // Fierce crimson blood
        case StyleType::Gunslinger:  return {0.20f, 0.65f, 1.00f}; // Cobalt / electric cyan
        case StyleType::RoyalGuard:  return {0.25f, 0.95f, 0.35f}; // Emerald jade
        default:                     return {0.80f, 0.80f, 0.80f};
    }
}

// ---------------------------------------------------------------------------
// Dedicated high-res style-cross letter atlas
// ---------------------------------------------------------------------------
// The shared 40px UI atlas clips Heavy Metal Rocking's uppercase swashes (T and R
// overhang far to the left of their pen origin, and uppercase S is a slanted bolt).
// The cross therefore uses the font's clean, upright small-cap lowercase glyphs,
// rendered large and ink-centered inside 128px cells so nothing is ever clipped.
enum CrossGlyph { kCrossT = 0, kCrossG = 1, kCrossS = 2, kCrossR = 3, kCrossCount = 4 };
constexpr int kCrossCell = 160;
constexpr int kCrossAtlasW = kCrossCell * kCrossCount;
constexpr int kCrossAtlasH = kCrossCell;
ID3D11ShaderResourceView* g_cross_view = nullptr;
float g_cross_ink_w[kCrossCount] = {};
float g_cross_ink_h[kCrossCount] = {};

void build_cross_atlas(ID3D11Device* device) {
    static const wchar_t kChars[kCrossCount] = {L'T', L'g', L's', L'r'};
    constexpr int kScratch = 256;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kScratch;
    info.bmiHeader.biHeight = -kScratch;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(dc);
        return;
    }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);

    DWORD num_fonts = 0;
    HANDLE mem_font = AddFontMemResourceEx((void*)kDmc5FontBytes, static_cast<DWORD>(kDmc5FontSize), nullptr, &num_fonts);
    HFONT font = CreateFontW(-60, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Heavy Metal Rocking");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));

    // White RGB everywhere, coverage in alpha (same convention as the UI font atlas)
    std::vector<uint8_t> atlas(static_cast<size_t>(kCrossAtlasW) * kCrossAtlasH * 4, 0);
    for (size_t i = 0; i < atlas.size(); i += 4) {
        atlas[i + 0] = atlas[i + 1] = atlas[i + 2] = 255;
    }

    auto* src_px = static_cast<uint8_t*>(bits);
    for (int gi = 0; gi < kCrossCount; ++gi) {
        std::memset(bits, 0, static_cast<size_t>(kScratch) * kScratch * 4);
        RECT rc{80, 80, kScratch, kScratch};
        wchar_t text[2] = {kChars[gi], 0};
        DrawTextW(dc, text, 1, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP | DT_NOPREFIX);
        GdiFlush();

        int min_x = kScratch, min_y = kScratch, max_x = -1, max_y = -1;
        for (int y = 0; y < kScratch; ++y) {
            for (int x = 0; x < kScratch; ++x) {
                const uint8_t* p = src_px + (y * kScratch + x) * 4;
                uint8_t lum = std::max(p[0], std::max(p[1], p[2]));
                if (lum > 24) {
                    min_x = std::min(min_x, x);
                    max_x = std::max(max_x, x);
                    min_y = std::min(min_y, y);
                    max_y = std::max(max_y, y);
                }
            }
        }
        if (max_x < 0) continue;

        int w = max_x - min_x + 1;
        int h = max_y - min_y + 1;
        g_cross_ink_w[gi] = static_cast<float>(w);
        g_cross_ink_h[gi] = static_cast<float>(h);

        // Ink-center the glyph in its cell so the quad center == visual center
        int cell_x0 = gi * kCrossCell;
        int dst_x0 = cell_x0 + (kCrossCell - w) / 2;
        int dst_y0 = (kCrossCell - h) / 2;
        for (int y = 0; y < h; ++y) {
            int dy = dst_y0 + y;
            if (dy < 0 || dy >= kCrossAtlasH) continue;
            for (int x = 0; x < w; ++x) {
                int dx = dst_x0 + x;
                if (dx < cell_x0 || dx >= cell_x0 + kCrossCell) continue;
                const uint8_t* p = src_px + ((min_y + y) * kScratch + (min_x + x)) * 4;
                uint8_t lum = std::max(p[0], std::max(p[1], p[2]));
                atlas[(static_cast<size_t>(dy) * kCrossAtlasW + dx) * 4 + 3] = lum;
            }
        }
    }

    if (old_font) SelectObject(dc, old_font);
    if (font) DeleteObject(font);
    if (mem_font) RemoveFontMemResourceEx(mem_font);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = kCrossAtlasW;
    desc.Height = kCrossAtlasH;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = atlas.data();
    sub.SysMemPitch = kCrossAtlasW * 4;

    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &texture))) {
        device->CreateShaderResourceView(texture, nullptr, &g_cross_view);
        texture->Release();
        mod_log("style_hud: cross atlas ready (t %.0fx%.0f, g %.0fx%.0f, s %.0fx%.0f, r %.0fx%.0f)",
                g_cross_ink_w[0], g_cross_ink_h[0], g_cross_ink_w[1], g_cross_ink_h[1],
                g_cross_ink_w[2], g_cross_ink_h[2], g_cross_ink_w[3], g_cross_ink_h[3]);
    }
}

// Rotated cross glyph (pivot = ink center) with vertical directional gradient.
// k = screen pixels per atlas pixel.
inline void push_cross_glyph(std::vector<Vertex>& verts,
                             float cx, float cy, int idx,
                             float k, float rot_rad,
                             float r_top, float g_top, float b_top,
                             float r_bot, float g_bot, float b_bot,
                             float a) {
    if (idx < 0 || idx >= kCrossCount) return;
    float u0 = static_cast<float>(idx * kCrossCell) / static_cast<float>(kCrossAtlasW);
    float u1 = static_cast<float>((idx + 1) * kCrossCell) / static_cast<float>(kCrossAtlasW);
    float v0 = 0.0f, v1 = 1.0f;

    float h = 0.5f * static_cast<float>(kCrossCell) * k;
    float c = std::cos(rot_rad);
    float s = std::sin(rot_rad);
    auto xf = [&](float lx, float ly, float& ox, float& oy) {
        ox = cx + lx * c - ly * s;
        oy = cy + lx * s + ly * c;
    };

    float x0, y0, x1, y1, x2, y2, x3, y3;
    xf(-h, -h, x0, y0); // top-left
    xf( h, -h, x1, y1); // top-right
    xf(-h,  h, x2, y2); // bottom-left
    xf( h,  h, x3, y3); // bottom-right

    Vertex q[6] = {
        {x0, y0, u0, v0, r_top, g_top, b_top, a},
        {x1, y1, u1, v0, r_top, g_top, b_top, a},
        {x2, y2, u0, v1, r_bot, g_bot, b_bot, a},
        {x2, y2, u0, v1, r_bot, g_bot, b_bot, a},
        {x1, y1, u1, v0, r_top, g_top, b_top, a},
        {x3, y3, u1, v1, r_bot, g_bot, b_bot, a},
    };
    verts.insert(verts.end(), q, q + 6);
}

// Calculate dynamic upright rotation for each rune (randomized on switch while guaranteed strictly upright)
inline float get_upright_random_rotation(char ch, uint64_t tick, float base_angle_rad, float max_var_deg = 1.5f) {
    uint32_t seed = static_cast<uint32_t>(tick ^ (tick >> 16)) * 2654435761u + static_cast<uint32_t>(ch) * 1013904223u;
    float norm = static_cast<float>(seed & 0xFFFF) / 65535.0f; // [0, 1]
    float var_rad = (norm * 2.0f - 1.0f) * (max_var_deg * (kPi / 180.0f));
    float total = base_angle_rad + var_rad;
    // Clamp within +/- 6 degrees: a subtle cant that always reads as upright
    constexpr float kMaxUpright = 6.0f * (kPi / 180.0f);
    return std::clamp(total, -kMaxUpright, kMaxUpright);
}

// 3D Chiseled metallic glyph rendering with volumetric depth, bevels, elevations, and specular rims
// k  = atlas->screen factor for the glyph, ui = HUD pixel scale for layer offsets
void draw_3d_chiseled_glyph(std::vector<Vertex>& verts,
                            float cx, float cy, int idx,
                            float k, float ui, float rot_rad,
                            float r, float g, float b, float a,
                            float outline_w, float depth_len,
                            bool is_active) {
    // 1. ELEVATION: Ambient cast drop shadow onto the glass
    float shadow_alpha = (is_active ? 0.75f : 0.55f) * a;
    push_cross_glyph(verts, cx + 2.0f * ui, cy + 3.0f * ui, idx, k * 1.03f, rot_rad,
                     0.02f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f, shadow_alpha);

    // 2. 3D DEPTH: Volumetric side extrusion slices stepped along the depth vector
    int depth_steps = is_active ? 5 : 4;
    float step_dx = (0.75f * depth_len) / static_cast<float>(depth_steps);
    float step_dy = (1.00f * depth_len) / static_cast<float>(depth_steps);
    float side_r = r * 0.38f, side_g = g * 0.38f, side_b = b * 0.38f;
    for (int s = depth_steps; s >= 1; --s) {
        float fade = 0.50f + 0.50f * (1.0f - static_cast<float>(s) / static_cast<float>(depth_steps));
        push_cross_glyph(verts, cx + static_cast<float>(s) * step_dx, cy + static_cast<float>(s) * step_dy, idx, k, rot_rad,
                         side_r * fade, side_g * fade, side_b * fade,
                         side_r * fade * 0.6f, side_g * fade * 0.6f, side_b * fade * 0.6f,
                         0.95f * a);
    }

    // 3. BEVEL: Dark chiseled chamfer contour
    const float ox[8] = {-outline_w, outline_w, 0, 0, -outline_w, outline_w, -outline_w, outline_w};
    const float oy[8] = {0, 0, -outline_w, outline_w, -outline_w, -outline_w, outline_w, outline_w};
    for (int i = 0; i < 8; ++i) {
        push_cross_glyph(verts, cx + ox[i], cy + oy[i], idx, k, rot_rad,
                         0.02f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f, 0.98f * a);
    }

    // 4. BEVEL: Specular top-left chamfer highlight (light catching the upper rim)
    float spec_r, spec_g, spec_b;
    if (is_active) {
        spec_r = std::min(1.0f, r * 1.5f + 0.35f);
        spec_g = std::min(1.0f, g * 1.5f + 0.35f);
        spec_b = std::min(1.0f, b * 1.5f + 0.35f);
    } else {
        spec_r = 0.95f; spec_g = 0.65f; spec_b = 0.65f;
    }
    push_cross_glyph(verts, cx - 0.8f * ui, cy - 1.0f * ui, idx, k, rot_rad,
                     spec_r, spec_g, spec_b,
                     spec_r * 0.7f, spec_g * 0.7f, spec_b * 0.7f,
                     0.88f * a);

    // 5. FRONT 3D CHISELED FACE: Directional metallic gradient
    push_cross_glyph(verts, cx, cy, idx, k, rot_rad,
                     std::min(1.0f, r * 1.25f), std::min(1.0f, g * 1.25f), std::min(1.0f, b * 1.25f),
                     r * 0.80f, g * 0.80f, b * 0.80f,
                     a);
}

} // namespace

void style_hud_init() {
    g_prev_style = StyleType::None;
    g_switch_tick = 0;
}

void style_hud_on_switch(StyleType new_style) {
    if (new_style != g_prev_style) {
        g_prev_style = new_style;
        g_switch_tick = get_time_ms();
    }
}

void style_hud_update() {
    StyleType cur = style_current();
    if (cur != g_prev_style) {
        style_hud_on_switch(cur);
    }
}

void style_hud_load_assets(ID3D11Device* device) {
    style_hud_release_assets();
    if (!device) return;

    // High-res, ink-centered style cross letters (independent of the glass texture)
    build_cross_atlas(device);

    // Load glass texture: try memory stream first, fallback to disk file
    struct GdiSession {
        ULONG_PTR token = 0;
        ~GdiSession() {
            if (token) Gdiplus::GdiplusShutdown(token);
        }
    } session;

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&session.token, &input, nullptr) != Gdiplus::Ok) {
        return;
    }

    Gdiplus::Bitmap* bitmap = nullptr;

    // Load from embedded PNG memory stream
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, kGlassHudPngSize);
    if (hMem) {
        void* pMem = GlobalLock(hMem);
        if (pMem) {
            std::memcpy(pMem, kGlassHudPngBytes, kGlassHudPngSize);
            GlobalUnlock(hMem);
            IStream* stream = nullptr;
            if (SUCCEEDED(CreateStreamOnHGlobal(hMem, TRUE, &stream))) {
                bitmap = new Gdiplus::Bitmap(stream);
                stream->Release();
            }
        }
    }

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0) {
        delete bitmap;
        bitmap = new Gdiplus::Bitmap(L"glass_hud.png");
    }

    if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok || bitmap->GetWidth() == 0) {
        delete bitmap;
        mod_log("style_hud: failed to load glass_hud.png");
        return;
    }

    UINT bw = bitmap->GetWidth();
    UINT bh = bitmap->GetHeight();
    Gdiplus::Rect rect(0, 0, static_cast<INT>(bw), static_cast<INT>(bh));
    Gdiplus::BitmapData data{};
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        delete bitmap;
        return;
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(bw) * bh * 4);
    for (UINT y = 0; y < bh; ++y) {
        auto* src = static_cast<uint8_t*>(data.Scan0) + y * data.Stride;
        uint8_t* dst = pixels.data() + static_cast<size_t>(y) * bw * 4;
        for (UINT x = 0; x < bw; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2]; // R
            dst[x * 4 + 1] = src[x * 4 + 1]; // G
            dst[x * 4 + 2] = src[x * 4 + 0]; // B
            dst[x * 4 + 3] = src[x * 4 + 3]; // A
        }
    }
    bitmap->UnlockBits(&data);
    delete bitmap;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = bw;
    desc.Height = bh;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = bw * 4;

    ID3D11Texture2D* texture = nullptr;
    if (SUCCEEDED(device->CreateTexture2D(&desc, &sub, &texture))) {
        device->CreateShaderResourceView(texture, nullptr, &g_glass_view);
        texture->Release();
        g_glass_w = static_cast<int>(bw);
        g_glass_h = static_cast<int>(bh);
        mod_log("style_hud: glass texture loaded successfully %dx%d", g_glass_w, g_glass_h);
    }
}

void style_hud_release_assets() {
    if (g_cross_view) {
        g_cross_view->Release();
        g_cross_view = nullptr;
    }
    if (g_glass_view) {
        g_glass_view->Release();
        g_glass_view = nullptr;
    }
    g_glass_w = 0;
    g_glass_h = 0;
}

void style_hud_render(float screen_w, float screen_h) {
    StyleType active_style = style_current();
    if (active_style == StyleType::None) {
        return;
    }

    style_hud_update();

    float scale = screen_h / 1080.0f;
    if (scale < 0.50f) scale = 0.50f;
    if (scale > 1.40f) scale = 1.40f;

    // Cross center position: calibrated compact top-left offset matching DMC5 proportions
    float cx = 94.0f * scale;
    float cy = 100.0f * scale;

    // Transition animation
    uint64_t now = get_time_ms();
    float anim_factor = 0.0f;
    if (g_switch_tick > 0 && now >= g_switch_tick) {
        float elapsed = static_cast<float>(now - g_switch_tick);
        if (elapsed < kSwitchAnimDurationMs) {
            float t = elapsed / kSwitchAnimDurationMs;
            anim_factor = (1.0f - t) * (1.0f - t);
        }
    }

    // =========================================================================
    // PASS 1: Dual-Layer Shattered Glass (Outer Crystal Wing + Inner Crater)
    // =========================================================================
    if (g_glass_view) {
        std::vector<Vertex> glass_verts;
        glass_verts.reserve(6);

        // Extended 1376x768 corner glass texture (anchored to top-left screen borders matching DMC5)
        // Crater (transparent breach) center measured from alpha: u=0.2158, v=0.3109
        float gw = 620.0f * scale;
        float gh = gw * (768.0f / 1376.0f); // ~295.8f * scale

        // Align the crater center directly with the style cross (cx, cy)
        // This extends the glass off-screen past the top (gy < 0) and left (gx < 0)
        float gx = cx - gw * 0.2158f;
        float gy = cy - gh * 0.3109f;

        // Translucent glass body with switch flash modulation
        float glass_alpha = 0.78f + anim_factor * 0.15f;
        float flash = 1.0f + anim_factor * 0.35f;
        push_textured_quad(glass_verts, gx, gy, gw, gh, 0.0f, 0.0f, 1.0f, 1.0f, flash, flash, flash, glass_alpha);

        submit(glass_verts, screen_w, screen_h, 1.0f, g_glass_view);
    }

    std::vector<Vertex> hud_verts;   // solid geometry (font atlas white texel)
    std::vector<Vertex> cross_verts; // style letters (dedicated cross atlas)
    hud_verts.reserve(1024);
    cross_verts.reserve(512);

    StyleColor act_col = get_active_style_color(active_style);

    // --- Letter sizing (prominent active letter enlargement) ---
    float ref_h = 78.0f;
    float k_inact = (30.0f * scale) / ref_h;
    float k_act   = ((46.0f + anim_factor * 8.0f) * scale) / ref_h;

    const StyleType kSlotStyle[kCrossCount] = {
        StyleType::Trickster, StyleType::Gunslinger, StyleType::Swordmaster, StyleType::RoyalGuard};

    float gk[kCrossCount];      // per-letter atlas->screen factor
    float hw_in[kCrossCount];   // inactive half extents
    float hh_in[kCrossCount];
    float hw[kCrossCount];      // actual half extents
    float hh[kCrossCount];
    for (int i = 0; i < kCrossCount; ++i) {
        float iw = g_cross_ink_w[i] > 0.0f ? g_cross_ink_w[i] : ref_h * 0.8f;
        float ih = g_cross_ink_h[i] > 0.0f ? g_cross_ink_h[i] : ref_h;
        bool act = (kSlotStyle[i] == active_style);
        gk[i] = act ? k_act : k_inact;
        hw_in[i] = 0.5f * iw * k_inact;
        hh_in[i] = 0.5f * ih * k_inact;
        hw[i] = 0.5f * iw * gk[i];
        hh[i] = 0.5f * ih * gk[i];
    }

    // --- Equal diamond spacing (symmetric clearance & distance across all 4 neighbors) ---
    float dist = 23.0f * scale;
    float pop  = 2.5f * scale;

    float lx[kCrossCount], ly[kCrossCount];
    lx[kCrossT] = cx;
    ly[kCrossT] = cy - dist - (active_style == StyleType::Trickster ? ((hh[kCrossT] - hh_in[kCrossT]) + pop) : 0.0f);

    lx[kCrossG] = cx - dist - (active_style == StyleType::Gunslinger ? ((hw[kCrossG] - hw_in[kCrossG]) + pop) : 0.0f);
    ly[kCrossG] = cy;

    lx[kCrossS] = cx + dist + (active_style == StyleType::Swordmaster ? ((hw[kCrossS] - hw_in[kCrossS]) + pop) : 0.0f);
    ly[kCrossS] = cy;

    lx[kCrossR] = cx;
    ly[kCrossR] = cy + dist + (active_style == StyleType::RoyalGuard ? ((hh[kCrossR] - hh_in[kCrossR]) + pop) : 0.0f);

    // 1. Lower-Left Arc Dial (between G and R), hugging the outside of the cluster
    float dial_r = dist + 14.0f * scale;
    float rad_start = 100.0f * (kPi / 180.0f);
    float rad_end   = 195.0f * (kPi / 180.0f);
    push_ring_arc(hud_verts, cx, cy, dial_r - 1.8f * scale, dial_r + 1.8f * scale, rad_start, rad_end, 20, 0.15f, 0.18f, 0.22f, 0.85f);
    for (int deg = 105; deg <= 195; deg += 15) {
        float rad = static_cast<float>(deg) * (kPi / 180.0f);
        float ca = std::cos(rad), sa = std::sin(rad);
        push_thick_line(hud_verts,
                        cx + ca * (dial_r - 3.2f * scale), cy + sa * (dial_r - 3.2f * scale),
                        cx + ca * (dial_r + 3.2f * scale), cy + sa * (dial_r + 3.2f * scale),
                        1.2f * scale, 0.70f, 0.78f, 0.88f, 0.85f);
    }

    // 2. Calibration Bracket below the cluster
    float by = cy + dial_r + 4.5f * scale;
    float bw = 15.0f * scale;
    push_thick_line(hud_verts, cx - bw, by, cx + bw, by, 1.2f * scale, 0.60f, 0.65f, 0.72f, 0.75f);
    push_thick_line(hud_verts, cx - bw, by - 2.5f * scale, cx - bw, by + 2.5f * scale, 1.2f * scale, 0.75f, 0.78f, 0.82f, 0.85f);
    push_thick_line(hud_verts, cx + bw, by - 2.5f * scale, cx + bw, by + 2.5f * scale, 1.2f * scale, 0.75f, 0.78f, 0.82f, 0.85f);
    for (int i : {-10, 0, 10}) {
        float nx = cx + static_cast<float>(i) * scale;
        float th = (i == 0) ? 2.5f * scale : 1.5f * scale;
        push_thick_line(hud_verts, nx, by - th, nx, by + th, 1.0f * scale, 0.65f, 0.70f, 0.78f, 0.65f);
    }

    // 3. Dial Ring around R (always visible, illuminates when Royalguard is active)
    bool is_rg = (active_style == StyleType::RoyalGuard);
    float aegis_r_in  = std::max(hw[kCrossR], hh[kCrossR]) * 1.15f;
    float aegis_r_out = aegis_r_in + 1.6f * scale;
    float ring_r = is_rg ? 0.25f : 0.72f;
    float ring_g = is_rg ? 0.95f : 0.12f;
    float ring_b = is_rg ? 0.35f : 0.12f;
    float ring_a = is_rg ? 0.95f : 0.90f;
    float rx = lx[kCrossR], ry = ly[kCrossR];
    push_ring_arc(hud_verts, rx, ry, aegis_r_in - 1.2f * scale, aegis_r_out + 1.2f * scale, 0.0f, kPi * 2.0f, 40, 0.02f, 0.01f, 0.01f, 0.92f);
    push_ring_arc(hud_verts, rx, ry, aegis_r_in, aegis_r_out, 0.0f, kPi * 2.0f, 40, ring_r, ring_g, ring_b, ring_a);
    float rot = is_rg ? (static_cast<float>(now % 8000) / 8000.0f * (kPi * 2.0f)) : 0.0f;
    float tick_r = is_rg ? 1.0f : 0.88f;
    float tick_g = is_rg ? 1.0f : 0.28f;
    float tick_b = is_rg ? 1.0f : 0.28f;
    for (int i = 0; i < 12; ++i) {
        float a = rot + static_cast<float>(i) * (kPi * 2.0f / 12.0f);
        float ca = std::cos(a), sa = std::sin(a);
        push_thick_line(hud_verts,
                        rx + ca * (aegis_r_in - 2.0f * scale), ry + sa * (aegis_r_in - 2.0f * scale),
                        rx + ca * (aegis_r_out + 2.0f * scale), ry + sa * (aegis_r_out + 2.0f * scale),
                        1.2f * scale, tick_r, tick_g, tick_b, 0.92f);
    }

    // 4. Subtle upright cant per letter (strictly upright, S slanting fixed, no wobble on switch)
    float rots[kCrossCount];
    rots[kCrossT] = get_upright_random_rotation('T', g_switch_tick, -0.020f, 1.0f);
    rots[kCrossG] = get_upright_random_rotation('g', g_switch_tick, -0.025f, 1.0f);
    rots[kCrossS] = get_upright_random_rotation('s', g_switch_tick, -0.015f, 1.0f);
    rots[kCrossR] = get_upright_random_rotation('r', g_switch_tick, -0.020f, 1.0f);

    int active_idx = -1;
    for (int i = 0; i < kCrossCount; ++i) {
        if (kSlotStyle[i] == active_style) active_idx = i;
    }

    // 5. Inactive 3D letters, then the active letter on top (enlarges prominently with deep 3D bevels)
    const float inact_r = 0.72f, inact_g = 0.12f, inact_b = 0.12f;
    for (int i = 0; i < kCrossCount; ++i) {
        if (i == active_idx) continue;
        draw_3d_chiseled_glyph(cross_verts, lx[i], ly[i], i, gk[i], scale, rots[i],
                               inact_r, inact_g, inact_b, 0.95f,
                               1.2f * scale, 1.6f * scale, false);
    }
    if (active_idx >= 0) {
        draw_3d_chiseled_glyph(cross_verts, lx[active_idx], ly[active_idx], active_idx, gk[active_idx], scale, rots[active_idx],
                               act_col.r, act_col.g, act_col.b, 1.0f,
                               1.8f * scale, (3.2f + anim_factor * 1.5f) * scale, true);
    }

    // Submit: HUD geometry (dial arc, bracket, Aegis ring) -> 3D style cross letters
    ID3D11ShaderResourceView* font_view = ui_get_font_view();
    if (font_view && !hud_verts.empty()) {
        submit(hud_verts, screen_w, screen_h, 0.0f, font_view);
    }
    if (g_cross_view && !cross_verts.empty()) {
        submit(cross_verts, screen_w, screen_h, 0.0f, g_cross_view);
    }
}
