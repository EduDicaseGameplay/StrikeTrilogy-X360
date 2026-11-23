/*
 * Fast VDP renderer for the Strike Trilogy X360 port.
 *
 * Same output, pixel for pixel, as the reference's vdp_render() 1x path
 * (Reference/StrikeHDCollection/src/hw/vdp_render.c, MIT) without HD art and without world planes,
 * which is all the port draws. Console build 2 measured the reference renderer at 19 ms per
 * frame on the Xenon, which made the game run at ~48 fps and starved the audio.
 * Changes (PORTING_PLAYBOOK 18, SoR2-X360 VDP): one byte per layer pixel instead of 32-bit refs;
 * name-table entry and pattern row fetched once per cell; lines independent (bands on threads).
 * Reads the vblank copy (snap_*). Checked against the reference on PC (pc_check --vdp-check).
 */
#include <string.h>
#include "genesis.h"
#include "vdp_render.h"
#include "strike_vdp_fast.h"

extern uint8_t  snap_vram[0x10000];
extern uint16_t snap_cram[64];
extern uint16_t snap_vsram[40];
extern uint8_t  snap_vreg[32];

static const uint8_t lv_norm[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
static const uint8_t lv_shad[8] = { 0, 29, 52, 70, 87, 101, 116, 130 };
static const uint8_t lv_high[8] = { 130, 144, 158, 172, 187, 206, 228, 255 };
static const int plane_cells[4] = { 32, 64, 32, 128 };

static uint32_t mkcol(uint16_t c, const uint8_t *lv)
{
    return 0xFF000000u | (uint32_t)lv[(c >> 1) & 7] << 16 | (uint32_t)lv[(c >> 5) & 7] << 8 | lv[(c >> 9) & 7];
}

static inline uint32_t vw(uint32_t a) { a &= 0xFFFE; return (uint32_t)snap_vram[a] << 8 | snap_vram[a + 1]; }

static inline uint32_t pat_row(uint32_t tile, int py)
{
    const uint8_t *p = snap_vram + ((tile * 32 + (uint32_t)py * 4) & 0xFFFF);
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void strike_vdp_fast_prepare(strike_vdp_frame *f, int ext, int limits, int wide)
{
    for (int i = 0; i < 64; i++) {
        f->pal[i] = mkcol(snap_cram[i], lv_norm);
        f->pal_s[i] = mkcol(snap_cram[i], lv_shad);
        f->pal_h[i] = mkcol(snap_cram[i], lv_high);
    }
    f->h40 = (snap_vreg[12] & 0x81) != 0;
    f->W = f->h40 ? 320 : 256;
    f->ext = ext;
    f->n = ext * 2 + f->W;
    if (f->n > VDP_MAX_W) f->n = VDP_MAX_W;
    f->limits = limits;
    f->wide = wide;
}

static void draw_plane(uint8_t *out, int line, int plane, uint32_t base, int x0, int n, int W)
{
    int pw = plane_cells[snap_vreg[16] & 3], ph = plane_cells[(snap_vreg[16] >> 4) & 3];
    if (pw == 128 && ph > 32) ph = 32;
    if (ph == 128 && pw > 32) pw = 32;
    uint32_t hs = (uint32_t)(snap_vreg[13] & 0x3F) << 10;
    int hmode = snap_vreg[11] & 3, vcol = (snap_vreg[11] >> 2) & 1;
    uint32_t ha;
    switch (hmode) {
    case 0:  ha = hs; break;
    case 1:  ha = hs + (uint32_t)(line & 7) * 4; break;
    case 2:  ha = hs + (uint32_t)(line & ~7) * 4; break;
    default: ha = hs + (uint32_t)line * 4; break;
    }
    int hsc = (int)(vw(ha + (uint32_t)plane * 2) & 0x3FF);
    int wpx = pw * 8, hpx = ph * 8;
    int vs_fixed = snap_vsram[plane];
    int last_cx = -1, last_sy = -1, hf = 0;
    uint32_t row = 0, attr = 0;
    for (int i = 0; i < n; i++) {
        int x = x0 + i;
        int vs;
        if (vcol) {
            int col = x < 0 ? 0 : x >= W ? (W / 16) - 1 : x / 16;
            vs = snap_vsram[(col * 2 + plane) % 40];
        } else vs = vs_fixed;
        int sy = (line + (vs & 0x3FF)) & (hpx - 1);
        int sx = (x - hsc) & (wpx - 1);
        int cx = sx >> 3;
        if (cx != last_cx || sy != last_sy) {
            uint32_t ent = vw(base + (uint32_t)(((sy >> 3) * pw + cx) * 2));
            int py = sy & 7;
            if (ent & 0x1000) py = 7 - py;
            row = pat_row(ent & 0x7FF, py);
            hf = (ent & 0x0800) != 0;
            attr = (ent & 0x8000 ? 0x80u : 0u) | ((ent >> 13) & 3) << 4;
            last_cx = cx; last_sy = sy;
        }
        int px = sx & 7;
        if (hf) px = 7 - px;
        uint32_t idx = (row >> (28 - 4 * px)) & 15;
        out[i] = (uint8_t)(idx ? attr | idx : 0);
    }
}

static int window_active(void)
{
    return (snap_vreg[17] & 0x9F) != 0 || (snap_vreg[18] & 0x9F) != 0;
}

static void draw_window(uint8_t *out, int line, int x0, int n, int W, int h40)
{
    int hp = (snap_vreg[17] & 0x1F) * 16, right = snap_vreg[17] & 0x80;
    int vp = (snap_vreg[18] & 0x1F) * 8, down = snap_vreg[18] & 0x80;
    int full_line = down ? line >= vp : line < vp;
    uint32_t base = h40 ? (uint32_t)(snap_vreg[3] & 0x3C) << 10 : (uint32_t)(snap_vreg[3] & 0x3E) << 10;
    int wcells = h40 ? 64 : 32;
    int last = -1, hf = 0;
    uint32_t row = 0, attr = 0;
    for (int i = 0; i < n; i++) {
        int x = x0 + i;
        if (x < 0 || x >= W) continue;
        int inwin = full_line || (right ? x >= hp : x < hp);
        if (!inwin) continue;
        if ((x >> 3) != last) {
            uint32_t ent = vw(base + (uint32_t)(((line >> 3) * wcells + (x >> 3)) * 2));
            int py = line & 7;
            if (ent & 0x1000) py = 7 - py;
            row = pat_row(ent & 0x7FF, py);
            hf = (ent & 0x0800) != 0;
            attr = (ent & 0x8000 ? 0x80u : 0u) | ((ent >> 13) & 3) << 4;
            last = x >> 3;
        }
        int px = x & 7;
        if (hf) px = 7 - px;
        uint32_t idx = (row >> (28 - 4 * px)) & 15;
        out[i] = (uint8_t)(idx ? attr | idx : 0);
    }
}

static void draw_sprites(uint8_t *out, int line, int x0, int n, int h40, int limits, int wide)
{
    uint32_t sat = h40 ? (uint32_t)(snap_vreg[5] & 0x7E) << 9 : (uint32_t)(snap_vreg[5] & 0x7F) << 9;
    int max_spr = h40 ? 80 : 64;
    int max_line = h40 ? 20 : 16;
    int max_dots = h40 ? 320 : 256;
    int link = 0, count = 0, dots = 0, any_drawn = 0;
    int ly = line + 128;
    memset(out, 0, (size_t)n);
    for (int k = 0; k < max_spr; k++) {
        uint32_t e = sat + (uint32_t)link * 8;
        int y = (int)(vw(e) & 0x3FF);
        uint32_t s = vw(e + 2), t = vw(e + 4);
        int x = wide ? (int)(int16_t)vw(e + 6) : (int)(vw(e + 6) & 0x1FF);
        int w = (int)((s >> 10) & 3) + 1, h = (int)((s >> 8) & 3) + 1;
        link = (int)(s & 0x7F);
        if (ly >= y && ly < y + h * 8) {
            if (limits && ++count > max_line) break;
            if (x == 0) {
                if (any_drawn) break;          /* sprite masking */
            } else {
                any_drawn = 1;
                int ty = ly - y;
                if (t & 0x1000) ty = h * 8 - 1 - ty;
                int sw = w * 8;
                if (limits && dots + sw > max_dots) sw = max_dots - dots;
                uint32_t attr = (t & 0x8000 ? 0x80u : 0u) | ((t >> 13) & 3) << 4;
                int last = -1;
                uint32_t row = 0;
                for (int tx = 0; tx < sw; tx++) {
                    int i = x - 128 + tx - x0;
                    if (i < 0 || i >= n || (out[i] & 15)) continue;
                    int cx = (t & 0x0800) ? w * 8 - 1 - tx : tx;
                    if ((cx >> 3) != last) {
                        uint32_t tile = ((t & 0x7FF) + (uint32_t)(cx >> 3) * (uint32_t)h + (uint32_t)(ty >> 3)) & 0x7FF;
                        row = pat_row(tile, ty & 7);
                        last = cx >> 3;
                    }
                    uint32_t idx = (row >> (28 - 4 * (cx & 7))) & 15;
                    if (idx) out[i] = (uint8_t)(attr | idx);
                }
            }
            if (limits) { dots += w * 8; if (dots >= max_dots) break; }
        }
        if (link == 0 || link >= max_spr) break;
    }
}

void strike_vdp_fast_lines(const strike_vdp_frame *f, uint32_t *fb, int pitch, int l0, int l1)
{
    uint8_t la[VDP_MAX_W], lb[VDP_MAX_W], ls[VDP_MAX_W];
    int n = f->n, W = f->W, h40 = f->h40, x0 = -f->ext;
    int sh = (snap_vreg[12] & 0x08) != 0;
    int bgi = snap_vreg[7] & 0x3F;
    uint32_t base_a = (uint32_t)(snap_vreg[2] & 0x38) << 10;
    uint32_t base_b = (uint32_t)(snap_vreg[4] & 0x07) << 13;
    int win = window_active();
    const uint32_t *pal = f->pal, *pal_s = f->pal_s, *pal_h = f->pal_h;
    for (int line = l0; line < l1; line++) {
        uint32_t *row = fb + (size_t)line * pitch;
        if (!(snap_vreg[1] & 0x40)) {             /* display disabled: backdrop */
            for (int i = 0; i < n; i++) row[i] = pal[bgi];
            continue;
        }
        draw_plane(lb, line, 1, base_b, x0, n, W);
        draw_plane(la, line, 0, base_a, x0, n, W);
        if (win) draw_window(la, line, x0, n, W, h40);
        draw_sprites(ls, line, x0, n, h40, f->limits, f->wide);
        if (!sh) {
            for (int i = 0; i < n; i++) {
                uint32_t a = la[i], b = lb[i], s = ls[i], px;
                if ((s & 0x80) && (s & 15)) px = s;
                else if ((a & 0x80) && (a & 15)) px = a;
                else if ((b & 0x80) && (b & 15)) px = b;
                else if (s & 15) px = s;
                else if (a & 15) px = a;
                else if (b & 15) px = b;
                else { row[i] = pal[bgi]; continue; }
                row[i] = pal[((px >> 4) & 3) * 16 + (px & 15)];
            }
            continue;
        }
        for (int i = 0; i < n; i++) {
            uint8_t a = la[i], b = lb[i], s = ls[i];
            uint8_t hi = 0, lo = 0;
            if (s & 0x80 && s & 15) hi = s;
            else if (a & 0x80 && a & 15) hi = a;
            else if (b & 0x80 && b & 15) hi = b;
            if (!hi) {
                if (s & 15 && !(s & 0x80)) lo = s;
                else if (a & 15 && !(a & 0x80)) lo = a;
                else if (b & 15 && !(b & 0x80)) lo = b;
            }
            uint8_t px = hi ? hi : lo;
            int idx = px ? ((px >> 4) & 3) * 16 + (px & 15) : bgi;
            int shadowed = !((a & 0x80) || (b & 0x80));
            int op = 0;
            if ((s & 0x3F) == 0x3E || (s & 0x3F) == 0x3F) {
                op = (s & 0x3F) == 0x3E ? 1 : 2;
                uint8_t u = 0;
                if (a & 0x80 && a & 15) u = a;
                else if (b & 0x80 && b & 15) u = b;
                else if (a & 15) u = a;
                else if (b & 15) u = b;
                idx = u ? ((u >> 4) & 3) * 16 + (u & 15) : bgi;
            } else if (px == s && (s & 15) && (s & 0x80)) {
                shadowed = 0;
            } else if (px == s && (s & 15) && (s & 0x3F) == 0x0E) {
                shadowed = 0;
            }
            int level = shadowed ? -1 : 0;
            if (op == 1) level++;
            if (op == 2) level--;
            if (level < -1) level = -1;
            if (level > 1) level = 1;
            row[i] = level < 0 ? pal_s[idx] : level > 0 ? pal_h[idx] : pal[idx];
        }
    }
}
