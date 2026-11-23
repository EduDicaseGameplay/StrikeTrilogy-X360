/*
 * Strike Trilogy X360: entry point of DesertStrike.xex, JungleStrike.xex and UrbanStrike.xex.
 * The game is chosen at build time (Source/<Game>/game_config.h).
 *
 * The game code is the reference's static recompilation (68000 -> C, lockstep-exact against
 * Musashi), driven frame by frame as the reference host does: cpu_run_frame() runs one emulated
 * frame (20000 instructions by default, vblank at 85 %) and returns at the next frame boundary.
 *
 * Threads (Xenon hardware threads):
 *   0  this one: input, the 68000 (recompiled), the Z80 and PSG, YM2612 writes and timers
 *   2  sound: the audio YM2612 generating the FM and the mix (strike_sound_x360.c)
 *   3  feeder of the XAudio2 voice (mdx_x360_audio.c); XAudio2 itself runs on 5
 *   4  render: the reference VDP renderer on a copy taken at vblank (strike_render.c)
 * Pacing: one emulated frame per TV refresh, by the raster position (SoR2-X360, KNOWN_ISSUES
 * SOR-3); the sound follows by steering XAudio2's frequency ratio by at most 0.5 %.
 * Every 5 s the log gets frames and time per part (PORTING_PLAYBOOK 18: measure first).
 */
#include <xtl.h>
#include <string.h>
#include "genesis.h"
#include "sound.h"
#include "cpu.h"
#include "game.h"
#include "mdx_x360.h"
#include "strike_sound_x360.h"
#include "strike_render.h"
#include "strike_input.h"
#include "strike_osd.h"
#include "strike_storage.h"
#include "strike_features.h"
#include "game_config.h"

#define STRIKE_VERSION "Strike Trilogy X360 build 7 (2026-11-23): fast renderer on 4 threads, widescreen 16:9, minimap"
#define AUDIO_RATE     53267            /* the YM2612's own rate (master clock / 1008) */

const game_module *strike_this_game(void);

/* x360_format.cpp (compat) prints through this name. */
void x360_log_write(const char *text, unsigned length)
{
    char line[512];
    if (length >= sizeof line) length = sizeof line - 1;
    memcpy(line, text, length);
    while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) length--;
    line[length] = 0;
    if (length) mdx_log(line);
}

static uint32_t page[VDP_H * STRIKE_FB_PITCH];
static strike_settings cfg;
static const game_module *game;
static int last_play;                  /* the last vblank was the playfield */
static uint32_t ovl[512 * 512];        /* minimap panel, A8R8G8B8 screen pixels */

static void show_page_forever(const char *const *lines, int n)
{
    for (int i = 0; i < n; i++) mdx_log(lines[i]);
    strike_osd_page(page, STRIKE_FB_PITCH, 320, VDP_H, lines, n);
    for (;;) {
        mdx_wait_vblank();
        mdx_video_present(page, 320, VDP_H, STRIKE_FB_PITCH, 0);
        strike_input_poll(0);
        if (strike_input_command() == STRIKE_CMD_QUIT) {
            XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
            return;
        }
    }
}

static void on_fatal(void)
{
    if (strike_sandbox) longjmp(strike_sandbox_jmp, 1);   /* minimap hidden frames: abandon, restore */
    static char where[64];
    const char *hex = "0123456789ABCDEF";
    memcpy(where, "AT 68000 ADDRESS $", 18);
    for (int i = 0; i < 6; i++) where[18 + i] = hex[(cpu_cur_pc >> (20 - 4 * i)) & 15];
    where[24] = 0;
    const char *lines[] = { "THE GAME STOPPED (RECOMPILED CODE ERROR)", where,
                            "SEE THE LOG FILE NEXT TO THE XEX", "HOLD BACK + START TO LEAVE" };
    show_page_forever(lines, 4);
}

static void on_frame(void) { cpu_frame_yield(); }
static void on_vblank(void)
{
    int wide;
    if (strike_sandbox) return;            /* hidden frames draw nothing */
    last_play = strike_ws_playfield(game, &wide);
    strike_render_vblank(strike_ws_ext(), last_play, wide);
}

static void apply_settings(void)
{
    g_frame_insns = cfg.speed_original ? GEN_FRAME_INSNS_AUTHENTIC : GEN_FRAME_INSNS;
    mdx_video_set_filter(cfg.filter_linear);
    mdx_video_set_aspect(cfg.aspect);
    strike_render_set_sprite_limits(cfg.sprite_limits);
    {
        int ws = cfg.widescreen == 2 ? mdx_video_is_widescreen() : cfg.widescreen;
        strike_ws_apply(game, ws && cfg.aspect != MDX_ASPECT_STRETCH);
    }
}

static void slot_message(const char *what)
{
    char m[48];
    size_t n = strlen(what);
    memcpy(m, what, n);
    memcpy(m + n, " SLOT ", 6);
    m[n + 6] = (char)('0' + cfg.slot);
    m[n + 7] = 0;
    strike_osd_message(m, 120);
}

static void handle_command(int cmd)
{
    switch (cmd) {
    case STRIKE_CMD_SAVE_STATE:
        snd_thread_drain();
        if (strike_state_save(STRIKE_GAME_ID, cfg.slot)) slot_message("SAVED");
        else strike_osd_message("SAVE FAILED", 120);
        mdx_log_int("[state] save slot", cfg.slot);
        break;
    case STRIKE_CMD_LOAD_STATE:
        if (strike_state_load(STRIKE_GAME_ID, cfg.slot)) slot_message("LOADED");
        else slot_message("NOTHING IN");
        mdx_log_int("[state] load slot", cfg.slot);
        break;
    case STRIKE_CMD_NEXT_SLOT: case STRIKE_CMD_PREV_SLOT:
        cfg.slot = (cfg.slot + (cmd == STRIKE_CMD_NEXT_SLOT ? 1 : 9)) % 10;
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        slot_message("STATE");
        break;
    case STRIKE_CMD_SPEED:
        cfg.speed_original = !cfg.speed_original;
        apply_settings();
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(cfg.speed_original ? "GAME SPEED: ORIGINAL (CONSOLE SLOWDOWN)" : "GAME SPEED: SMOOTH (NO SLOWDOWN)", 150);
        break;
    case STRIKE_CMD_FILTER:
        cfg.filter_linear = !cfg.filter_linear;
        apply_settings();
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(cfg.filter_linear ? "PICTURE: SMOOTH" : "PICTURE: SHARP PIXELS", 120);
        break;
    case STRIKE_CMD_ASPECT:
        cfg.aspect = (cfg.aspect + 1) % 3;
        apply_settings();
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(cfg.aspect == MDX_ASPECT_4_3 ? "ASPECT: 4:3" : cfg.aspect == MDX_ASPECT_PIXEL ? "ASPECT: SQUARE PIXELS" : "ASPECT: STRETCH", 120);
        break;
    case STRIKE_CMD_FLICKER:
        cfg.sprite_limits = !cfg.sprite_limits;
        apply_settings();
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(cfg.sprite_limits ? "SPRITE LIMIT: CONSOLE (FLICKER)" : "SPRITE LIMIT: OFF (NO FLICKER)", 120);
        break;
    case STRIKE_CMD_WIDESCREEN:
        cfg.widescreen = strike_ws_ext() ? 0 : 1;
        apply_settings();
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(strike_ws_ext() ? "WIDESCREEN 16:9: ON" : "WIDESCREEN: OFF (4:3)", 120);
        break;
    case STRIKE_CMD_MINIMAP:
        cfg.minimap = !cfg.minimap;
        strike_settings_save(STRIKE_GAME_ID, &cfg);
        strike_osd_message(cfg.minimap ? "MINIMAP: ON (LB/RB: OBJECTIVES)" : "MINIMAP: OFF", 120);
        break;
    case STRIKE_CMD_RESET:
        game_boot(game);
        strike_osd_message("RESET", 90);
        mdx_log("[main] reset");
        break;
    default: break;
    }
}

void __cdecl main(void)
{
    mdx_log_set_path(STRIKE_LOG_PATH);
    mdx_install_crash_logger();
    mdx_log(STRIKE_VERSION);
    mdx_log("[main] " STRIKE_TITLE);
    if (!mdx_video_init()) { mdx_log("[main] no video"); for (;;) Sleep(1000); }

    game = strike_this_game();
    {
        char sha1[41], path[260];
        if (!strike_rom_load(STRIKE_ROM_NAME, sha1, path, sizeof path)) {
            const char *lines[] = { STRIKE_SHORT ": ROM NOT FOUND", "COPY YOUR OWN GENESIS ROM TO", path,
                                    "(USA/EUROPE VERSION, SEE README)", "HOLD BACK + START TO LEAVE" };
            show_page_forever(lines, 5);
            return;
        }
        mdx_log(path);
        mdx_log(sha1);
        if (strcmp(sha1, game->rom_sha1)) {
            const char *lines[] = { STRIKE_SHORT ": WRONG ROM VERSION", path, "SHA-1 MUST BE", game->rom_sha1,
                                    "THE CODE IS RECOMPILED FOR THAT EXACT DUMP", "HOLD BACK + START TO LEAVE" };
            show_page_forever(lines, 6);
            return;
        }
    }

    strike_settings_load(STRIKE_GAME_ID, &cfg);
    int audio = mdx_audio_init(AUDIO_RATE);
    mdx_log_int("[main] audio", audio);
    game_boot(game);
    cpu_fatal_hook = on_fatal;
    gen_frame_cb = on_frame;
    gen_vblank_cb = on_vblank;
    if (!snd_thread_start(2)) mdx_log("[main] sound thread failed: single-thread sound");
    if (!strike_render_start(4)) { mdx_log("[main] render thread failed: rendering inline"); strike_render_start(-1); }
    {   /* build 2 measured 19 ms of rendering per frame: 4 bands of lines on hardware threads 4, 1, 3, 5
         * (the game uses 0, the FM 2; XAudio2 works on 5 in short bursts) */
        static const int hw[3] = { 1, 3, 5 };
        mdx_log_int("[main] render bands", strike_render_add_workers(hw, 3) + 1);
    }
    apply_settings();
    strike_mm_init(game, "game:\\minimap\\");
    strike_osd_message(STRIKE_SHORT " X360 - BACK+START (3 S) TO LEAVE", 240);
    mdx_log("[main] running");

    static int16_t abuf[8192 * 2];
    uint32_t freq = mdx_ticks_per_second(), t0 = mdx_ticks();
    uint32_t t_wait = 0, t_present = 0, t_emu = 0, t_audio = 0;
    int frames = 0, steps = 0;
    float ratio = 1.0f;
    for (;;) {
        uint32_t a = mdx_ticks();
        mdx_wait_vblank();
        uint32_t b = mdx_ticks();
        {
            const uint32_t *fbc;
            int w, h, ext, pitch, scale;
            if (strike_render_take(&fbc, &w, &h, &ext, &pitch, &scale)) {
                uint32_t *fb = (uint32_t *)fbc;          /* our buffer until the next vblank render */
                strike_osd_draw(fb, pitch, w, h, scale);
                mdx_video_present(fb, w, h, pitch, ext);
            }
        }
        uint32_t c = mdx_ticks();
        strike_input_poll(g_frame);
        int cmd = strike_input_command();
        if (cmd == STRIKE_CMD_QUIT) break;
        if (cmd) handle_command(cmd);
        g_pad_buttons[0] = strike_input_genesis();
        g_pad_buttons[1] = strike_input_genesis2();
        if (!steps) mdx_log("[main] first frame");
        cpu_run_frame();
        snd_thread_flush();
        if (!steps) mdx_log("[main] first frame done");
        {   /* minimap: data, terrain (hidden frames once per level), panel for the next presents */
            int step = strike_input_mm_step(), pw, ph, pic[4];
            uint32_t tm = mdx_ticks();
            strike_mm_update(cfg.minimap, last_play);
            if (step) strike_mm_step(step);
            uint32_t tm2 = mdx_ticks();
            if (tm2 - tm > freq / 20) mdx_log_int("[minimap] terrain capture ms", (int)((tm2 - tm) / (freq / 1000)));
            mdx_video_picture_rect(pic);
            if (cfg.minimap && last_play && strike_mm_compose(ovl, 512, 512, 512, mdx_video_height(), &pw, &ph, GetTickCount())) {
                int m = mdx_video_height() >= 700 ? 20 : 10;
                int x = pic[0] + pic[2] - pw - m, y = pic[1] + pic[3] - ph - m;
                if (x + pw > mdx_video_width() - m) x = mdx_video_width() - m - pw;
                mdx_video_set_overlay(ovl, pw, ph, 512, x < 0 ? 0 : x, y < 0 ? 0 : y);
            } else mdx_video_set_overlay(NULL, 0, 0, 0, 0, 0);
        }
        uint32_t d = mdx_ticks();
        int n = snd_read(abuf, 8192);
        if (audio && n > 0) ratio = mdx_audio_feed_ratio(abuf, n, AUDIO_RATE);
        uint32_t e = mdx_ticks();
        steps++;
        frames++;
        t_wait += b - a; t_present += c - b; t_emu += d - c; t_audio += e - d;
        if (e - t0 >= freq * 5) {
            double us = 1000000.0 / (double)freq / frames;
            mdx_log_int("[perf] frames in 5 s", frames);
            mdx_log_int("[perf] emulation us/frame (68000+Z80+PSG+YM timers)", (int)(t_emu * us));
            {
                int rus = (int)(strike_render_take_us() / (unsigned)frames);
                mdx_log_int("[perf] render thread us/frame", rus);
            }
            mdx_log_int("[perf] sound thread us/frame", (int)(snd_thread_take_us() / (unsigned)frames));
            mdx_log_int("[perf] present us/frame", (int)(t_present * us));
            mdx_log_int("[perf] audio feed us/frame", (int)(t_audio * us));
            mdx_log_int("[perf] vblank wait us/frame", (int)(t_wait * us));
            mdx_log_int("[perf] audio queued frames", mdx_audio_queued());
            mdx_log_int("[perf] audio ratio ppm", (int)((ratio - 1.0f) * 1000000.0f));
            mdx_log_int("[perf] game frame", (int)g_frame);
            t0 = e; t_wait = t_present = t_emu = t_audio = 0; frames = 0;
        }
    }
    mdx_log("[main] quit by Back+Start");
    XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
}
