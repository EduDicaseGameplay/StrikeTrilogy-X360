/*
 * Widescreen playfield test and minimap for the Strike Trilogy X360 port.
 * Ported from the reference host (Reference/StrikeHDCollection/src/host/main.c, MIT):
 *   strike_ws_playfield  <- render_frame's playfield test, map check and settle delays
 *   strike_mm_*          <- mm_rect, mm_capture, mm_update, mm_step, draw_minimap
 * The markers come from the reference's games/strike_minimap.c through game_module->minimap.
 * Differences: the terrain cache is a raw file instead of a PNG, and the panel is drawn in
 * software into an A8R8G8B8 overlay that the video layer puts over the picture in screen pixels.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef X360
#include <xtl.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "genesis.h"
#include "cpu.h"
#include "sound.h"
#include "vdp_render.h"
#include "strike_features.h"
#include "strike_render.h"
#include "strike_sound_x360.h"
#include "strike_osd.h"

int strike_sandbox;
unsigned strike_sandbox_pad;
jmp_buf strike_sandbox_jmp;

/* ================= widescreen ================= */
void strike_ws_apply(const game_module *g, int on)
{
    int ext = on && g && g->ws_max_ext >= STRIKE_WS_EXT ? STRIKE_WS_EXT : 0;
    g_ws_ext = ext;
    g_ws_win = ext < 56 ? ext : 56;
}

int strike_ws_ext(void) { return g_ws_ext; }

int strike_ws_playfield(const game_module *g, int *wide_sprites)
{
    static vdp_world_plane wp[2];
    static int settle, plain;
    int ext = g_ws_ext;
    int play = g && g->ws_active && g->ws_active(), map_fail = 0;
    /* map check: ws_world returns 0 when the planes hold something other than the map */
    if (play && g->ws_world && !g->ws_world(wp, ext, ext)) { play = 0; map_fail = 1; }
    if (map_fail) { settle = 30; plain = 0; }
    else if (!play) { settle = ++plain >= 20 ? 6 : (settle > 6 ? settle : 6); }
    else if (settle > 0) { settle--; play = 0; }
    if (play) plain = 0;
    *wide_sprites = ext && play;
    return play;
}

/* ================= minimap ================= */
static const game_module *game;
static char cache_dir[200];
static game_minimap mmd;
static int mmd_ok;
static struct {
    uint32_t level;
    int state;                    /* 0 not yet, 1 terrain ready, 2 capture failed */
    int tries;
    uint64_t retry_frame;
    int r[4];                     /* crop on the pause screen, 1x px */
    int w, h;
    uint32_t *px;                 /* r[2] x r[3] terrain, 0x00RRGGBB */
    uint32_t name_until;          /* ms: show the entry name highlighted until then */
    int have_level;
} mm;
static uint32_t mmfb[VDP_MAX_W * VDP_H];
static uint32_t prevfb[VDP_MAX_W * VDP_H];
static uint32_t ms_last;

void strike_mm_init(const game_module *g, const char *dir)
{
    game = g;
    strncpy(cache_dir, dir, sizeof cache_dir - 1);
    CreateDirectoryA(dir, NULL);
}

static int mm_rect(const game_minimap *d, int r[4], int screen_w)
{
    const game_mm_view *v = game->mm_view;
    int s = d->shift ? d->shift : v->shift, round = (1 << s) - 1;
    int x0 = v->origin_x + (d->x0 >> s), y0 = v->origin_y + (d->y0 >> s);
    int x1 = v->origin_x + ((d->x1 + round) >> s), y1 = v->origin_y + ((d->y1 + round) >> s);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > screen_w) x1 = screen_w;
    if (y1 > VDP_H) y1 = VDP_H;
    r[0] = x0; r[1] = y0; r[2] = x1 - x0; r[3] = y1 - y0;
    return r[2] >= 16 && r[3] >= 16;
}

/* Hidden frames: open the pause map and grab its picture into out (r[2] x r[3]). */
static int mm_capture(uint32_t *out, int r[4])
{
    const game_mm_view *v = game->mm_view;
    size_t n = cpu_state_size();
    uint8_t *st = (uint8_t *)malloc(n);
    uint16_t pad_save = g_pad_buttons[0];
    static uint16_t prev_cram[64];
    volatile int ok = 0;
    int stable = 0, have_prev = 0;
    if (!st) return 0;
    snd_thread_drain();
    cpu_state_save(st);
    uint32_t sa = v->sentinel & 0xFFFF;
    g_ram[sa] = (uint8_t)~(v->sentinel_val >> 8);
    g_ram[(sa + 1) & 0xFFFF] = (uint8_t)~v->sentinel_val;
    if (v->page) {
        g_ram[v->page & 0xFFFF] = (uint8_t)(v->page_map >> 8);
        g_ram[(v->page + 1) & 0xFFFF] = (uint8_t)v->page_map;
    }
    snd_set_discard(1);
    strike_sandbox = 1;
    if (setjmp(strike_sandbox_jmp)) goto restore;     /* the game stopped during the hidden frames */
    for (int k = 0; k < 240 && !ok; k++) {
        g_pad_buttons[0] = strike_sandbox_pad = k < 3 ? GB_START : 0;
        cpu_run_frame();
        snd_thread_flush();
        if ((g_ram[sa] << 8 | g_ram[(sa + 1) & 0xFFFF]) != v->sentinel_val) {
            /* X360: the map opens within a few frames of Start when the game allows it (not during
             * Desert's take-off); give up after 60 instead of the reference's 240, the retry comes
             * 10 s later. Hidden frames cost several times more on the Xenon than on a PC. */
            if (k >= 60 && !have_prev) break;
            continue;
        }
        /* hide the icons: end the sprite list at an off-screen first entry */
        uint32_t sat = (uint32_t)(g_vreg[5] & 0x7F) << 9;
        for (int i = 0; i < 8; i++) g_vram[(sat + i) & 0xFFFF] = 0;
        int dw = strike_render_capture(mmfb);
        int rr[4];
        if (!mm_rect(&mmd, rr, dw)) break;
        int lit = 0, same = have_prev && rr[2] == r[2] && rr[3] == r[3];
        for (int y = 0; y < rr[3]; y++)
            for (int x = 0; x < rr[2]; x++) {
                uint32_t c = mmfb[(rr[1] + y) * STRIKE_FB_PITCH + rr[0] + x];
                if ((c & 0xF0F0F0) != 0) lit++;
                if (same && prevfb[y * rr[2] + x] != c) same = 0;
                prevfb[y * rr[2] + x] = c;
            }
        /* the pause screen fades in: wait for the palette to hold too */
        if (memcmp(prev_cram, g_cram, sizeof prev_cram)) { same = 0; memcpy(prev_cram, g_cram, sizeof prev_cram); }
        memcpy(r, rr, sizeof rr);
        have_prev = 1;
        stable = same && lit > rr[2] * rr[3] / 4 ? stable + 1 : 0;
#ifndef X360
        if (getenv("STRIKE_MM_LOG")) fprintf(stderr, "mm k %d rect %d,%d %dx%d lit %d same %d stable %d\n", k, rr[0], rr[1], rr[2], rr[3], lit, same, stable);
#endif
        if (stable >= 12) {
            for (int y = 0; y < r[3]; y++) memcpy(out + y * r[2], mmfb + (r[1] + y) * STRIKE_FB_PITCH + r[0], (size_t)r[2] * 4);
            ok = 1;
        }
    }
restore:
    strike_sandbox = 0;
    strike_sandbox_pad = 0;
    cpu_state_load(st);
    free(st);
    snd_set_discard(0);
    g_pad_buttons[0] = pad_save;
    return ok;
}

/* ---- terrain cache: "MMC1", w, h (16-bit big endian), then R G B per pixel ---- */
static void cache_path(char *out, int n)
{
    static const char hex[] = "0123456789ABCDEF";
    char name[48];
    size_t l = strlen(game->id);
    memcpy(name, game->id, l);
    name[l++] = '_';
    for (int i = 5; i >= 0; i--) name[l++] = hex[(mm.level >> (i * 4)) & 15];
    memcpy(name + l, ".mmc", 5);
    strncpy(out, cache_dir, (size_t)n - 1);
    out[n - 1] = 0;
    strncat(out, name, (size_t)n - strlen(out) - 1);
}

static int cache_load(int w, int h)
{
    char path[260];
    cache_path(path, sizeof path);
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    size_t bytes = 8 + (size_t)w * h * 3;
    uint8_t *b = (uint8_t *)malloc(bytes);
    DWORD got = 0;
    int ok = 0;
    if (b && ReadFile(f, b, (DWORD)bytes, &got, NULL) && got == bytes && !memcmp(b, "MMC1", 4) &&
        (b[4] << 8 | b[5]) == w && (b[6] << 8 | b[7]) == h) {
        for (int i = 0; i < w * h; i++) mm.px[i] = (uint32_t)b[8 + i * 3] << 16 | (uint32_t)b[9 + i * 3] << 8 | b[10 + i * 3];
        ok = 1;
    }
    free(b);
    CloseHandle(f);
    return ok;
}

static void cache_save(int w, int h)
{
    char path[260];
    cache_path(path, sizeof path);
    size_t bytes = 8 + (size_t)w * h * 3;
    uint8_t *b = (uint8_t *)malloc(bytes);
    if (!b) return;
    memcpy(b, "MMC1", 4);
    b[4] = (uint8_t)(w >> 8); b[5] = (uint8_t)w; b[6] = (uint8_t)(h >> 8); b[7] = (uint8_t)h;
    for (int i = 0; i < w * h; i++) { b[8 + i * 3] = (uint8_t)(mm.px[i] >> 16); b[9 + i * 3] = (uint8_t)(mm.px[i] >> 8); b[10 + i * 3] = (uint8_t)mm.px[i]; }
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD put; WriteFile(f, b, (DWORD)bytes, &put, NULL); CloseHandle(f); }
    free(b);
}

void strike_mm_update(int enabled, int playfield)
{
    mmd_ok = 0;
    if (!game || !game->minimap || !game->mm_view || strike_sandbox) return;
    if (!playfield || !game->minimap(&mmd)) return;
    mmd_ok = 1;
#ifndef X360
    if (getenv("STRIKE_MM_LOG") && (g_frame % 100) == 0) fprintf(stderr, "mm frame %llu level %06X state %d tries %d\n", (unsigned long long)g_frame, (unsigned)mmd.level, mm.state, mm.tries);
#endif
    if (!enabled) return;
    if (!mm.have_level || mm.level != mmd.level) {
        free(mm.px);
        memset(&mm, 0, sizeof mm);
        mm.level = mmd.level;
        mm.have_level = 1;
    }
    if (mm.state == 1 || (mm.state == 2 && (mm.tries >= 3 || g_frame < mm.retry_frame))) return;
    int r[4];
    if (!mm_rect(&mmd, r, 320)) return;
    mm.px = (uint32_t *)malloc((size_t)VDP_MAX_W * VDP_H * 4);
    if (!mm.px) return;
    if (cache_load(r[2], r[3])) {
        memcpy(mm.r, r, sizeof r);
        mm.w = r[2]; mm.h = r[3];
        mm.state = 1;
        return;
    }
    int ok = mm_capture(mm.px, r);
    if (!ok) { free(mm.px); mm.px = NULL; mm.state = 2; mm.tries++; mm.retry_frame = g_frame + 600; return; }
    memcpy(mm.r, r, sizeof r);
    mm.w = r[2]; mm.h = r[3];
    mm.state = 1;
    cache_save(r[2], r[3]);
}

void strike_mm_step(int dir)
{
    if (!mmd_ok || !game->minimap_select || mmd.count <= 0) return;
    int i = (mmd.sel + dir + mmd.count) % mmd.count;
    game->minimap_select(i);
    mmd.sel = i;
    mm.name_until = ms_last + 2500;
}

/* ---- drawing into the A8R8G8B8 overlay ---- */
static uint32_t *O;
static int OP, OW, OH;

static void blend(int x, int y, uint32_t argb)
{
    if (x < 0 || y < 0 || x >= OW || y >= OH) return;
    uint32_t a = argb >> 24, d = O[y * OP + x], da = d >> 24;
    if (a == 255 || !da) { O[y * OP + x] = argb; return; }
    /* x / 255 as (x * 257 + 257) >> 16 (exact for 0..65025): integer division is slow on the Xenon */
#define DIV255(x) ((((x) + 1) * 257) >> 16)
    uint32_t r = DIV255(((argb >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a));
    uint32_t g = DIV255(((argb >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a));
    uint32_t b = DIV255((argb & 255) * a + (d & 255) * (255 - a));
    uint32_t oa = a + DIV255(da * (255 - a));
    O[y * OP + x] = oa << 24 | r << 16 | g << 8 | b;
}

static void rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) blend(i, j, c);
}

static void diamond(int cx, int cy, int s, uint32_t c)
{
    for (int dy = -s; dy <= s; dy++) {
        int span = s - (dy < 0 ? -dy : dy);
        for (int dx = -span; dx <= span; dx++) blend(cx + dx, cy + dy, c);
    }
}

static void text(int x, int y, const char *s, uint32_t c, int scale)
{
    /* strike_osd_text writes opaque pixels: draw into the overlay with full alpha */
    strike_osd_text(O, OP, OW, OH, x, y, s, c | 0xFF000000u, scale);
}

int strike_mm_compose(uint32_t *ovl, int pitch, int max_w, int max_h, int screen_h, int *w, int *h, uint32_t ms_now)
{
    ms_last = ms_now;
    if (!mmd_ok || !game || !game->mm_view) return 0;
    const game_mm_view *v = game->mm_view;
    int r[4];
    if (mm.state == 1) memcpy(r, mm.r, sizeof r);
    else if (!mm_rect(&mmd, r, 320)) return 0;
    int u = screen_h >= 700 ? 2 : 1;                  /* text and marker scale */
    int pad = 4 * u, lh = 9 * u;
    int mh = screen_h * 30 / 100, mw = r[2] * mh / r[3];
    if (mw > max_w - pad * 2) { mw = max_w - pad * 2; mh = r[3] * mw / r[2]; }
    if (mh > max_h - pad * 2 - lh) { mh = max_h - pad * 2 - lh; mw = r[2] * mh / r[3]; }
    if (mw < 16 || mh < 16) return 0;
    int pw = mw + pad * 2, ph = mh + pad * 2 + lh;
    int mx = pad, my = pad;
    /* The panel and the scaled terrain only change with the level, the capture or the size: keep
     * them in base[] and copy it each frame (console build 3: redrawing it every frame cost ~7 ms
     * of the main thread). */
    static uint32_t base[512 * 512];
    static struct { uint32_t level; int state, pw, ph; const uint32_t *px; int valid; } key;
    if (!key.valid || key.level != mm.level || key.state != mm.state || key.pw != pw || key.ph != ph || key.px != mm.px) {
        O = base; OP = 512; OW = pw; OH = ph;
        for (int y = 0; y < ph; y++) memset(O + y * OP, 0, (size_t)pw * 4);
        /* panel: dark olive fill, tan edge (reference colours) */
        rect(0, 0, pw, ph, 0xAA0E110Cu);
        rect(0, 0, pw, 1, 0xC8C4A462u); rect(0, ph - 1, pw, 1, 0xC8C4A462u);
        rect(0, 0, 1, ph, 0xC8C4A462u); rect(pw - 1, 0, 1, ph, 0xC8C4A462u);
        if (mm.state == 1 && mm.px) {
            for (int y = 0; y < mh; y++) {
                int sy = y * r[3] / mh;
                for (int x = 0; x < mw; x++)
                    blend(mx + x, my + y, 0xCD000000u | mm.px[sy * r[2] + x * r[2] / mw]);
            }
        } else rect(mx, my, mw, mh, 0x961E2418u);
        key.level = mm.level; key.state = mm.state; key.pw = pw; key.ph = ph; key.px = mm.px; key.valid = 1;
    }
    O = ovl; OP = pitch; OW = pw; OH = ph;
    for (int y = 0; y < ph; y++) memcpy(O + y * OP, base + y * 512, (size_t)pw * 4);
    /* world -> panel */
    int sh = mmd.shift ? mmd.shift : v->shift;
#define MMX(wx) (mx + (int)(((long)(v->origin_x + ((wx) >> sh) - r[0]) * 2 + 1) * mw / (2 * r[2])))
#define MMY(wy) (my + (int)(((long)(v->origin_y + ((wy) >> sh) - r[1]) * 2 + 1) * mh / (2 * r[3])))
    int blink = (ms_now / 250) & 1;
    int ms = 2 * u;
    for (int i = 0; i < mmd.n; i++) {
        const game_mm_mark *m = &mmd.m[i];
        int ax = MMX(m->x), ay = MMY(m->y);
        if (ax < mx || ay < my || ax > mx + mw || ay > my + mh) continue;
        int sel = m->obj == mmd.sel;
        uint32_t c = m->main ? (sel ? 0xFFFFD648u : 0xFFDC6034u) : 0xFF78DCE6u;
        if (sel && blink) c = (c & 0x00FFFFFFu) | 0x59000000u;     /* the pause map blinks the selection */
        int s = sel ? ms + u : ms;
        diamond(ax, ay, s + 1, 0xE6080A06u);
        diamond(ax, ay, s, c);
    }
    if (mmd.have_home) {
        int hx = MMX(mmd.hx), hy = MMY(mmd.hy), s = ms;
        rect(hx - s - 1, hy - s - 1, s * 2 + 2, s * 2 + 2, 0xE6080A06u);
        rect(hx - s, hy - s, s * 2, s * 2, 0xFF5A96E6u);
    }
    if (mmd.have_player) {
        int ax = MMX(mmd.px), ay = MMY(mmd.py), s = ms + 1;
        rect(ax - s - 1, ay - s - 1, s * 2 + 2, s * 2 + 2, 0xF0080A06u);
        rect(ax - s, ay - s, s * 2, s * 2, 0xFFFAFAF0u);
    }
#undef MMX
#undef MMY
    /* selected entry name */
    char name[64], line[80];
    int st = game->minimap_name ? game->minimap_name(mmd.sel, name, sizeof name) : -1;
    if (st >= 0) {
        size_t l = strlen(name);
        if (l > 60) l = 60;
        memcpy(line, name, l);
        line[l] = 0;
        if (st == 2) memcpy(line + l, " (?)", 5);
        int maxc = (mw - 4 * u) / (6 * u);
        if (maxc > 0 && (int)strlen(line) > maxc) line[maxc] = 0;
        int tw = strike_osd_text_width(line, u);
        uint32_t c = (int)(mm.name_until - ms_now) > 0 ? 0xFFD648u : 0xC4A462u;
        text(mx + (mw - tw) / 2, my + mh + pad / 2 + u, line, c, u);
    }
    *w = pw; *h = ph;
    return 1;
}
