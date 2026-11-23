/*
 * Render thread for the Strike Trilogy X360 port.
 *
 * The reference renders the whole frame at the start of vblank (the games use no raster effects:
 * their HBlank vector is a bare RTE, see vdp_render.h). Here the vblank callback copies the four
 * VDP memories (VRAM 64 KB, CRAM, VSRAM, registers) and wakes a thread on another core, which runs
 * the reference renderer on the copy (vdp_render_snap.c) into one of two framebuffers. The 68000
 * goes on with vblank and the next frame meanwhile; the host presents the finished frame at the
 * next refresh (one frame of latency, the same as the threaded sound).
 * Portable Win32 (Xbox 360 XDK and Windows PC, for Tools/pc_check).
 */
#include <string.h>
#ifdef X360
#include <xtl.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "genesis.h"
#include "vdp_render.h"
#include "strike_render.h"
#include "strike_vdp_fast.h"

uint8_t  snap_vram[0x10000];
uint16_t snap_cram[64];
uint16_t snap_vsram[40];
uint8_t  snap_vreg[32];

static uint32_t fbs[2][VDP_H * STRIKE_FB_PITCH];
static int fb_w[2], fb_ext[2];
static int cur_ext[2], cur_active[2], cur_wide[2];
static int target;                    /* buffer the next render writes */
static int ready = -1;                /* last finished buffer, -1 none */
static int threaded, busy;
static HANDLE ev_start, ev_done;
static volatile LONG render_us;
static int sprite_limits = 1;
static LARGE_INTEGER qpf;

void strike_render_set_sprite_limits(int on) { sprite_limits = on; }

/* ---- band workers: the fast renderer draws lines [l0, l1) on extra hardware threads ---- */
#define MAX_WORKERS 3
static int use_reference;             /* 1: the reference vdp_render (pc_check comparison) */
static int n_workers;
static strike_vdp_frame vframe;
static uint32_t *band_fb;
static int band_pitch;
static struct { HANDLE start, done; int l0, l1; } workers[MAX_WORKERS];

void strike_render_use_reference(int on) { use_reference = on; }

static void band(int l0, int l1) { strike_vdp_fast_lines(&vframe, band_fb, band_pitch, l0, l1); }

static DWORD WINAPI band_thread(LPVOID arg)
{
    int k = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(workers[k].start, INFINITE);
        band(workers[k].l0, workers[k].l1);
        SetEvent(workers[k].done);
    }
    return 0;
}


int strike_render_add_workers(const int *hw_threads, int count)
{
    for (int k = 0; k < count && n_workers < MAX_WORKERS; k++) {
        int i = n_workers;
        workers[i].start = CreateEvent(NULL, FALSE, FALSE, NULL);
        workers[i].done = CreateEvent(NULL, FALSE, FALSE, NULL);
        HANDLE h = CreateThread(NULL, 64 * 1024, band_thread, (LPVOID)(intptr_t)i, CREATE_SUSPENDED, NULL);
        if (!h) break;
#ifdef X360
        if (hw_threads[k] >= 0 && hw_threads[k] < 6) XSetThreadProcessor(h, (DWORD)hw_threads[k]);
#else
        (void)hw_threads;
#endif
        ResumeThread(h);
        CloseHandle(h);
        n_workers++;
    }
    return n_workers;
}

static void render_fast(uint32_t *fb, int pitch, int ext, int limits, int wide)
{
    int bands = n_workers + 1;
    strike_vdp_fast_prepare(&vframe, ext, limits, wide);
    band_fb = fb;
    band_pitch = pitch;
    for (int k = 0; k < n_workers; k++) {
        workers[k].l0 = VDP_H * (k + 1) / bands;
        workers[k].l1 = VDP_H * (k + 2) / bands;
        SetEvent(workers[k].start);
    }
    band(0, VDP_H / bands);
    for (int k = 0; k < n_workers; k++) WaitForSingleObject(workers[k].done, INFINITE);
}

static void render_now(int buf)
{
    LARGE_INTEGER t0, t1;
    vdp_render_opts o;
    QueryPerformanceCounter(&t0);
    memset(&o, 0, sizeof o);
    o.sprite_limits = sprite_limits;
    o.ext_left = o.ext_right = cur_ext[buf];
    o.wide_sprites = cur_active[buf] && cur_wide[buf];
    /* vdp_display_width() reads the copied registers through the snap renderer's view. */
    int core = (snap_vreg[12] & 0x81) ? 320 : 256, ext = cur_ext[buf];
    uint32_t *out = fbs[buf];
    fb_w[buf] = core + 2 * ext;
    fb_ext[buf] = ext;
    if (use_reference) vdp_render(out, STRIKE_FB_PITCH, &o);
    else render_fast(out, STRIKE_FB_PITCH, ext, o.sprite_limits, o.wide_sprites);
    if (ext && !cur_active[buf])           /* not the playfield: 4:3 with black bars (reference) */
        for (int y = 0; y < VDP_H; y++) {
            uint32_t *row = out + (size_t)y * STRIKE_FB_PITCH;
            for (int x = 0; x < ext; x++) row[x] = 0xFF000000u;
            for (int x = ext + core; x < fb_w[buf]; x++) row[x] = 0xFF000000u;
        }
    QueryPerformanceCounter(&t1);
    InterlockedExchangeAdd(&render_us, (LONG)((t1.QuadPart - t0.QuadPart) * 1000000 / qpf.QuadPart));
}

static DWORD WINAPI render_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(ev_start, INFINITE);
        render_now(target);
        SetEvent(ev_done);
    }
    return 0;
}

int strike_render_start(int hw_thread)
{
    QueryPerformanceFrequency(&qpf);
    if (hw_thread < 0) return 1;
    ev_start = CreateEvent(NULL, FALSE, FALSE, NULL);
    ev_done = CreateEvent(NULL, FALSE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 64 * 1024, render_thread, NULL, CREATE_SUSPENDED, NULL);
    if (!h) return 0;
#ifdef X360
    if (hw_thread < 6) XSetThreadProcessor(h, (DWORD)hw_thread);
#endif
    ResumeThread(h);
    CloseHandle(h);
    threaded = 1;
    return 1;
}

static void finish(void)
{
    if (!busy) return;
    WaitForSingleObject(ev_done, INFINITE);
    busy = 0;
    ready = target;
    target ^= 1;
}

void strike_render_vblank(int ext, int active, int wide_sprites)
{
    finish();
    cur_ext[target] = ext; cur_active[target] = active; cur_wide[target] = wide_sprites;                          /* never copy over a frame still being drawn */
    memcpy(snap_vram, g_vram, sizeof snap_vram);
    memcpy(snap_cram, g_cram, sizeof snap_cram);
    memcpy(snap_vsram, g_vsram, sizeof snap_vsram);
    memcpy(snap_vreg, g_vreg, sizeof snap_vreg);
    if (threaded) {
        busy = 1;
        SetEvent(ev_start);
    } else {
        render_now(target);
        ready = target;
        target ^= 1;
    }
}

int strike_render_take(const uint32_t **fb, int *w, int *h, int *ext, int *pitch, int *scale)
{
    finish();
    if (ready < 0) return 0;
    *fb = fbs[ready];
    *w = fb_w[ready];
    *h = VDP_H;
    *ext = fb_ext[ready];
    *pitch = STRIKE_FB_PITCH;
    *scale = 1;
    return 1;
}

uint32_t strike_render_take_us(void) { return (uint32_t)InterlockedExchange(&render_us, 0); }

/* Synchronous render of the live VDP state into out (pitch STRIKE_FB_PITCH), plain 4:3, for the
 * minimap's hidden frames. The render thread is idle afterwards; the shown frame is untouched. */
int strike_render_capture(uint32_t *out)
{
    vdp_render_opts o;
    finish();
    memcpy(snap_vram, g_vram, sizeof snap_vram);
    memcpy(snap_cram, g_cram, sizeof snap_cram);
    memcpy(snap_vsram, g_vsram, sizeof snap_vsram);
    memcpy(snap_vreg, g_vreg, sizeof snap_vreg);
    memset(&o, 0, sizeof o);
    vdp_render(out, STRIKE_FB_PITCH, &o);
    return (snap_vreg[12] & 0x81) ? 320 : 256;
}
