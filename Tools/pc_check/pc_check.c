/*
 * Strike Trilogy X360 - PC check of the port's own emulation code.
 *
 * Runs a game headless on Windows with the SAME modules the XEX uses for sound and picture
 * (Xbox360/Audio/strike_sound_x360.c + ym2612_x360.cpp with the sound thread, and
 * Xbox360/Graphics/strike_render.c + vdp_render_snap.c with the render thread), and writes:
 *   outdir/frames.log  the reference's lockstep format (compare with ds_ref: tools/lockstep.py)
 *   outdir/audio.wav   the threaded mix (compare with the reference: ds_test ... --wav)
 *   outdir/shot_NNNNNN.png  the threaded render every N frames (look at it / compare with an emulator)
 *
 * usage: pc_check rom frames inputs outdir [--shot-every N] [--no-thread] [--frame-insns N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "cpu.h"
#include "game.h"
#include "sound.h"
#include "strike_sound_x360.h"
#include "strike_render.h"
#include "strike_features.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static FILE *flog, *wav;
static uint32_t wav_frames;
static const char *outdir;
static int shot_every, ws_on, mm_on, last_play;
static const game_module *game;
static uint32_t ovl[512 * 512];
static int mm_shots;

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t rate = 53267, bytes = frames * 4, v;
    uint16_t s;
    uint8_t h[44] = { 'R','I','F','F' };
    v = 36 + bytes; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    s = 1; memcpy(h + 20, &s, 2);
    s = 2; memcpy(h + 22, &s, 2);
    memcpy(h + 24, &rate, 4);
    v = rate * 4; memcpy(h + 28, &v, 4);
    s = 4; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
}

static void drain_audio(void)
{
    int16_t buf[4096 * 2];
    int n;
    while ((n = snd_read(buf, 4096)) > 0)
        if (wav) { fwrite(buf, 4, (size_t)n, wav); wav_frames += (uint32_t)n; }
}

static void log_state(void)
{
    uint32_t c[4];
    gen_state_crcs(c);
    fprintf(flog, "%llu pc=%06X sr=%04X", (unsigned long long)g_frame, cpu_cur_pc, cpu_get_sr());
    for (int i = 0; i < 8; i++) fprintf(flog, " d%d=%08X", i, R.d[i]);
    for (int i = 0; i < 8; i++) fprintf(flog, " a%d=%08X", i, R.a[i]);
    fprintf(flog, " ram=%08X vram=%08X vdp=%08X zram=%08X\n", c[0], c[1], c[2], c[3]);
}

static void on_frame(void)
{
    log_state();
    cpu_frame_yield();
}

static void on_vblank(void)
{
    int wide;
    if (strike_sandbox) return;
    last_play = strike_ws_playfield(game, &wide);
    strike_render_vblank(strike_ws_ext(), last_play, wide);
}
static void on_fatal(void) { if (strike_sandbox) longjmp(strike_sandbox_jmp, 1); }

static void save_shot(uint64_t frame)
{
    const uint32_t *fb;
    int w, h, ext, pitch, scale;
    if (!strike_render_take(&fb, &w, &h, &ext, &pitch, &scale)) return;
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t p = fb[(size_t)y * pitch + x];
            uint8_t *o = rgb + (y * w + x) * 3;
            o[0] = (uint8_t)(p >> 16); o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)p;
        }
    char path[512];
    snprintf(path, sizeof path, "%s/shot_%06llu.png", outdir, (unsigned long long)frame);
    stbi_write_png(path, w, h, 3, rgb, w * 3);
    free(rgb);
    int pw, ph;
    if (mm_on && last_play && strike_mm_compose(ovl, 512, 512, 512, 720, &pw, &ph, (uint32_t)frame * 16)) {
        uint8_t *rgba = malloc((size_t)pw * ph * 4);
        for (int i = 0; i < pw * ph; i++) {
            uint32_t p = ovl[(i / pw) * 512 + i % pw];
            rgba[i * 4] = (uint8_t)(p >> 16); rgba[i * 4 + 1] = (uint8_t)(p >> 8); rgba[i * 4 + 2] = (uint8_t)p; rgba[i * 4 + 3] = (uint8_t)(p >> 24);
        }
        snprintf(path, sizeof path, "%s/minimap_%06llu.png", outdir, (unsigned long long)frame);
        stbi_write_png(path, pw, ph, 4, rgba, pw * 4);
        free(rgba);
        mm_shots++;
    }
}

int main(int argc, char **argv)
{
    int threads = 1, ref_render = 0;
    FILE *rlog;
    if (argc < 5) {
        fprintf(stderr, "usage: %s rom frames inputs outdir [--shot-every N] [--no-thread] [--frame-insns N]\n", argv[0]);
        return 1;
    }
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--shot-every") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-thread")) threads = 0;
        else if (!strcmp(argv[i], "--frame-insns") && i + 1 < argc) g_frame_insns = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ws")) ws_on = 1;
        else if (!strcmp(argv[i], "--minimap")) mm_on = 1;
        else if (!strcmp(argv[i], "--ref-render")) ref_render = 1;
        else if (!strcmp(argv[i], "--sprite-limits") && i + 1 < argc) strike_render_set_sprite_limits(atoi(argv[++i]));
    }
    if (!gen_load_rom(argv[1])) { perror("rom"); return 1; }
    uint64_t max_frames = strtoull(argv[2], 0, 10);
    gen_load_inputs(argv[3]);
    outdir = argv[4];
    char path[512];
    snprintf(path, sizeof path, "%s/frames.log", outdir);
    flog = fopen(path, "w");
    snprintf(path, sizeof path, "%s/audio.wav", outdir);
    wav = fopen(path, "wb");
    if (!flog || !wav) { perror(outdir); return 1; }
    wav_header(wav, 0);
    game = game_identify_rom();
    if (!game) { fprintf(stderr, "%s: not a supported ROM\n", argv[1]); return 1; }
    game_boot(game);
    gen_frame_cb = on_frame;
    gen_vblank_cb = on_vblank;
    cpu_fatal_hook = on_fatal;
    strike_ws_apply(game, ws_on);
    {
        char mmdir[600];
        snprintf(mmdir, sizeof mmdir, "%s\\minimap\\", outdir);
        strike_mm_init(game, mmdir);
    }
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    if (threads) {
        if (!snd_thread_start(-1)) { fprintf(stderr, "sound thread failed\n"); return 1; }
        if (!strike_render_start(0)) { fprintf(stderr, "render thread failed\n"); return 1; }
    } else strike_render_start(-1);
    strike_render_use_reference(ref_render);
    if (!ref_render && threads) {
        static const int hw[3] = { 1, 3, 5 };
        fprintf(stderr, "render bands: %d\n", strike_render_add_workers(hw, 3) + 1);
    }
    snprintf(path, sizeof path, "%s/render.log", outdir);
    rlog = fopen(path, "w");
    uint64_t render_ns = 0;
    while (g_frame < max_frames) {
        cpu_run_frame();
        snd_thread_flush();
        drain_audio();
        {   /* hash of every finished frame (compare --ref-render against the fast renderer) */
            const uint32_t *fb;
            int w, h, ext, pitch, scale;
            LARGE_INTEGER a, b;
            QueryPerformanceCounter(&a);
            if (strike_render_take(&fb, &w, &h, &ext, &pitch, &scale) && rlog) {
                uint32_t hsh = 2166136261u;
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++) hsh = (hsh ^ (fb[(size_t)y * pitch + x] & 0xFFFFFF)) * 16777619u;
                fprintf(rlog, "%llu %d %d %08X\n", (unsigned long long)g_frame, w, ext, hsh);
            }
            QueryPerformanceCounter(&b);
            render_ns += (uint64_t)((b.QuadPart - a.QuadPart) * 1000000000.0 / f.QuadPart);
        }
        if (mm_on) {
            LARGE_INTEGER a, b;
            QueryPerformanceCounter(&a);
            strike_mm_update(1, last_play);
            QueryPerformanceCounter(&b);
            double ms = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
            if (ms > 5) fprintf(stderr, "frame %llu: minimap terrain capture %.1f ms\n", (unsigned long long)g_frame, ms);
        }
        if (shot_every && g_frame % (uint64_t)shot_every == 0) save_shot(g_frame);
    }
    snd_thread_drain();
    drain_audio();
    QueryPerformanceCounter(&t1);
    if (rlog) fclose(rlog);
    fprintf(stderr, "render: %s, %u us per frame on the render thread\n", ref_render ? "reference" : "fast",
            (unsigned)(strike_render_take_us() / (g_frame ? g_frame : 1)));
    (void)render_ns;
    fclose(flog);
    wav_header(wav, wav_frames);
    fclose(wav);
    fprintf(stderr, "%s: %llu frames, %u audio frames, %s, %.1f ms\n", game->id, (unsigned long long)g_frame,
            wav_frames, threads ? "threaded" : "single thread", (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart);
    return 0;
}
