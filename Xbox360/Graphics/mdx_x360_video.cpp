// MDX360-Runtime video: D3D9 on Xenos through the XDK's C entry points (D3DDevice_*).
// Device creation, the textured-quad shaders, the two streaming textures with fences and the
// vertical-blank wait follow SoR2-X360 port/x360/platform/sdl_x360_video.cpp (console-tested).
// The Mega Drive frame is a linear X8R8G8B8 texture (the big-endian 0x00RRGGBB word is the
// console's own X8R8G8B8), drawn as one quad in the largest 4:3 rectangle that fits.
#include <xtl.h>
#include <xgraphics.h>
#include <string.h>

#include "mdx_x360.h"
#include "present_ps.h"
#include "present_vs.h"

namespace {
struct Vertex { float x, y, z, w; float u, v; float r, g, b, a; };

const int kTexW = 1024, kTexH = 256;        // room for the widest frame (320 + 2*80 + 64) x 240
D3DDevice *g_dev = nullptr;
D3DVertexShader *g_vs = nullptr;
D3DPixelShader *g_ps = nullptr;
D3DVertexDeclaration *g_decl = nullptr;
D3DTexture *g_tex[2] = { nullptr, nullptr };
DWORD g_fence[2] = { 0, 0 };
int g_cur = 0;
int g_width = 1280, g_height = 720;
int g_aspect = MDX_ASPECT_4_3;
int g_linear = 0;
// Strike Trilogy X360: an overlay drawn over the picture in screen pixels (the minimap), A8R8G8B8,
// alpha-blended; double-buffered with the same fences as the frame.
const int kOvlW = 512, kOvlH = 512;
D3DTexture *g_ovl[2] = { nullptr, nullptr };
const uint32_t *g_ovl_px = nullptr;
int g_ovl_w = 0, g_ovl_h = 0, g_ovl_pitch = 0, g_ovl_x = 0, g_ovl_y = 0;
} // namespace

extern "C" {

int mdx_video_width(void) { return g_width; }
int mdx_video_height(void) { return g_height; }
void mdx_video_set_aspect(int mode) { g_aspect = mode; }
void mdx_video_set_filter(int linear) { g_linear = linear; }

int mdx_video_init(void) {
    if (g_dev) return 1;
    XVIDEO_MODE mode;
    memset(&mode, 0, sizeof mode);
    XGetVideoMode(&mode);
    const bool hd = mode.fIsHiDef && mode.dwDisplayHeight >= 720;
    g_width = hd ? 1280 : 640;
    g_height = hd ? 720 : 480;

    D3DPRESENT_PARAMETERS pp;
    memset(&pp, 0, sizeof pp);
    pp.BackBufferWidth = g_width;
    pp.BackBufferHeight = g_height;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    if (Direct3D_CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) < 0 || !g_dev) {
        mdx_log("[video] Direct3D_CreateDevice failed");
        g_dev = nullptr;
        return 0;
    }
    g_vs = D3DDevice_CreateVertexShader((const DWORD *)g_presentVs);
    g_ps = D3DDevice_CreatePixelShader((const DWORD *)g_presentPs);
    static const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 24, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    g_decl = D3DDevice_CreateVertexDeclaration(elements);
    for (int i = 0; i < 2; i++)
        g_tex[i] = (D3DTexture *)D3DDevice_CreateTexture(kTexW, kTexH, 1, 1, 0, D3DFMT_LIN_X8R8G8B8, D3DPOOL_DEFAULT,
                                                         D3DRTYPE_TEXTURE);
    for (int i = 0; i < 2; i++)
        g_ovl[i] = (D3DTexture *)D3DDevice_CreateTexture(kOvlW, kOvlH, 1, 1, 0, D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT,
                                                         D3DRTYPE_TEXTURE);
    if (!g_vs || !g_ps || !g_decl || !g_tex[0] || !g_tex[1] || !g_ovl[0] || !g_ovl[1]) {
        mdx_log("[video] shaders, declaration or textures failed");
        return 0;
    }
    mdx_log_int("[video] device width", g_width);
    mdx_log_int("[video] device height", g_height);
    return 1;
}

// Returns at the start of the next vertical blank, by the raster position: one emulated frame per
// TV refresh (SoR2-X360, KNOWN_ISSUES SOR-3). Sleeps while the beam is far from the bottom, spins
// near it; gives up after 20 ms. YieldProcessor() does not link with clang (an MSVC __emit).
void mdx_wait_vblank(void) {
    if (!g_dev) return;
    D3DRASTER_STATUS rs;
    DWORD t0 = GetTickCount();
    D3DDevice_GetRasterStatus(g_dev, &rs);
    while (rs.InVBlank && GetTickCount() - t0 < 20) D3DDevice_GetRasterStatus(g_dev, &rs);
    UINT last = rs.ScanLine;
    for (;;) {
        D3DDevice_GetRasterStatus(g_dev, &rs);
        if (rs.InVBlank || rs.ScanLine < last) return;
        last = rs.ScanLine;
        if (GetTickCount() - t0 >= 20) return;
        UINT lines = g_height > 0 ? (UINT)g_height : 720;
        if (rs.ScanLine + 140 < lines) Sleep(1);
        else __asm__ __volatile__("nop");
    }
}

void mdx_video_set_overlay(const uint32_t *px, int w, int h, int pitch, int x, int y) {
    g_ovl_px = px; g_ovl_w = w; g_ovl_h = h; g_ovl_pitch = pitch; g_ovl_x = x; g_ovl_y = y;
}

int mdx_video_is_widescreen(void) {
    XVIDEO_MODE mode;
    memset(&mode, 0, sizeof mode);
    XGetVideoMode(&mode);
    return mode.fIsWideScreen ? 1 : 0;
}

// Screen rectangle of the last presented picture (the 4:3 core plus sides), for overlays.
static int g_pic[4];
void mdx_video_picture_rect(int r[4]) { r[0] = g_pic[0]; r[1] = g_pic[1]; r[2] = g_pic[2]; r[3] = g_pic[3]; }

void mdx_video_present(const uint32_t *fb, int w, int h, int pitch, int extra_side) {
    if (!g_dev || !fb || w <= 0 || h <= 0) return;
    const int texW = kTexW, texH = kTexH;
    D3DTexture **texs = g_tex;
    if (w > texW) w = texW;
    if (h > texH) h = texH;
    // The other texture of the pair: the GPU may still read the current one.
    const int next = g_cur ^ 1;
    if (g_fence[next]) D3DDevice_BlockOnFence(g_fence[next]);
    g_cur = next;
    D3DLOCKED_RECT locked;
    D3DTexture_LockRect(texs[g_cur], 0, &locked, nullptr, 0);
    for (int y = 0; y < h; y++)                        // write-only: texture memory is write-combined
        memcpy((uint8_t *)locked.pBits + y * locked.Pitch, fb + (size_t)y * pitch, (size_t)w * 4);
    D3DTexture_UnlockRect(texs[g_cur], 0);

    // Destination: the 4:3 picture (the centre 320 or 256 columns) fits the screen height; the
    // widescreen columns extend it sideways at the same pixel shape.
    float dw, dh;
    int core_w = w - extra_side * 2;
    if (g_aspect == MDX_ASPECT_STRETCH) { dw = (float)g_width; dh = (float)g_height; }
    else {
        double par = g_aspect == MDX_ASPECT_PIXEL ? 1.0 : (4.0 / 3.0) / ((double)core_w / h);
        dh = (float)g_height;
        dw = (float)(w * par * g_height / h);
        if (dw > g_width) { dh = dh * g_width / dw; dw = (float)g_width; }
    }
    const float dx = (g_width - dw) * 0.5f, dy = (g_height - dh) * 0.5f;
    g_pic[0] = (int)dx; g_pic[1] = (int)dy; g_pic[2] = (int)dw; g_pic[3] = (int)dh;
    const float x0 = dx / g_width * 2 - 1, x1 = (dx + dw) / g_width * 2 - 1;
    const float y0 = 1 - dy / g_height * 2, y1 = 1 - (dy + dh) / g_height * 2;
    const float u1 = (float)w / texW, v1 = (float)h / texH;
    const Vertex quad[4] = {
        {x0, y0, 0, 1, 0, 0, 1, 1, 1, 1},
        {x1, y0, 0, 1, u1, 0, 1, 1, 1, 1},
        {x0, y1, 0, 1, 0, v1, 1, 1, 1, 1},
        {x1, y1, 0, 1, u1, v1, 1, 1, 1, 1},
    };
    D3DDevice_Clear(g_dev, 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0, FALSE);
    D3DDevice_SetVertexDeclaration(g_dev, g_decl);
    D3DDevice_SetVertexShader(g_dev, g_vs);
    D3DDevice_SetPixelShader(g_dev, g_ps);
    D3DDevice_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);
    D3DDevice_SetRenderState(g_dev, D3DRS_ZENABLE, FALSE);
    D3DDevice_SetRenderState(g_dev, D3DRS_ZWRITEENABLE, FALSE);
    D3DDevice_SetRenderState(g_dev, D3DRS_ALPHABLENDENABLE, FALSE);
    // The sampler from a volatile: with a literal 0 the inline's 64-bit sampler mask became two
    // 32-bit stores (one the constant 0x80000000) and a 64-bit load, which Xenia's JIT rejects
    // ("Invalid handling of constant", then a breakpoint at the first present).
    static volatile DWORD sampler0 = 0;
    D3DDevice_SetTexture_Inline(g_dev, sampler0, (D3DBaseTexture *)texs[g_cur]);
    const DWORD filter = g_linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MINFILTER, filter);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MAGFILTER, filter);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    D3DDevice_DrawVerticesUP(g_dev, D3DPT_TRIANGLESTRIP, 4, quad, sizeof(Vertex));
    if (g_ovl_px && g_ovl_w > 0 && g_ovl_h > 0) {
        int ow = g_ovl_w < kOvlW ? g_ovl_w : kOvlW, oh = g_ovl_h < kOvlH ? g_ovl_h : kOvlH;
        D3DLOCKED_RECT ol;
        D3DTexture_LockRect(g_ovl[g_cur], 0, &ol, nullptr, 0);
        for (int y = 0; y < oh; y++)
            memcpy((uint8_t *)ol.pBits + y * ol.Pitch, g_ovl_px + (size_t)y * g_ovl_pitch, (size_t)ow * 4);
        D3DTexture_UnlockRect(g_ovl[g_cur], 0);
        const float ox0 = (float)g_ovl_x / g_width * 2 - 1, ox1 = (float)(g_ovl_x + ow) / g_width * 2 - 1;
        const float oy0 = 1 - (float)g_ovl_y / g_height * 2, oy1 = 1 - (float)(g_ovl_y + oh) / g_height * 2;
        const float ou1 = (float)ow / kOvlW, ov1 = (float)oh / kOvlH;
        const Vertex oq[4] = {
            {ox0, oy0, 0, 1, 0, 0, 1, 1, 1, 1},
            {ox1, oy0, 0, 1, ou1, 0, 1, 1, 1, 1},
            {ox0, oy1, 0, 1, 0, ov1, 1, 1, 1, 1},
            {ox1, oy1, 0, 1, ou1, ov1, 1, 1, 1, 1},
        };
        D3DDevice_SetRenderState(g_dev, D3DRS_ALPHABLENDENABLE, TRUE);
        D3DDevice_SetRenderState(g_dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        D3DDevice_SetRenderState(g_dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        D3DDevice_SetTexture_Inline(g_dev, sampler0, (D3DBaseTexture *)g_ovl[g_cur]);
        D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        D3DDevice_SetSamplerState(g_dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        D3DDevice_DrawVerticesUP(g_dev, D3DPT_TRIANGLESTRIP, 4, oq, sizeof(Vertex));
        D3DDevice_SetRenderState(g_dev, D3DRS_ALPHABLENDENABLE, FALSE);
    }
    g_fence[g_cur] = D3DDevice_InsertFence(g_dev);
    D3DDevice_Present(g_dev);                          // only queues the flip (PORTING_PLAYBOOK 18)
}

} // extern "C"
