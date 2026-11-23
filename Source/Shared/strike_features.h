/* Widescreen playfield test and minimap for the Strike Trilogy X360 port (portable C over Win32
 * files: compiled in the XEX and in Tools/pc_check). Ported from the reference host
 * (src/host/main.c: render_frame's playfield logic, mm_rect, mm_capture, mm_update, draw_minimap). */
#ifndef STRIKE_FEATURES_H
#define STRIKE_FEATURES_H
#include <stdint.h>
#include <setjmp.h>
#include "game.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- hidden frames (the host's frame and vblank callbacks must check these) ---- */
extern int strike_sandbox;              /* 1 while the minimap runs hidden frames */
extern unsigned strike_sandbox_pad;     /* Genesis buttons held during them */
extern jmp_buf strike_sandbox_jmp;      /* the fatal hook longjmps here while strike_sandbox */

/* ---- widescreen ---- */
#define STRIKE_WS_EXT 56                /* 16:9: 56 px per side, inside the game's own 512 px window */
void strike_ws_apply(const game_module *g, int on);   /* sets g_ws_ext / g_ws_win (game patches) */
int  strike_ws_ext(void);
/* At vblank (emulation thread): is this frame the playfield? Same test, map check and settle
 * delays as the reference. Also tells whether sprite X is read as 16-bit. */
int  strike_ws_playfield(const game_module *g, int *wide_sprites);

/* ---- minimap ---- */
/* dir: folder for the terrain cache, ending in a backslash ("game:\\minimap\\"). */
void strike_mm_init(const game_module *g, const char *dir);
/* Once per frame boundary, after the frame ran; playfield = the last strike_ws_playfield result.
 * May run hidden frames (once per level, then cached). */
void strike_mm_update(int enabled, int playfield);
void strike_mm_step(int dir);           /* LB / RB: the pause map's selection */
/* Draws the minimap panel in screen pixels into ovl (A8R8G8B8, pitch in pixels).
 * Returns 0 when there is nothing to show; else *w, *h of the panel. */
int  strike_mm_compose(uint32_t *ovl, int pitch, int max_w, int max_h, int screen_h, int *w, int *h, uint32_t ms_now);

#ifdef __cplusplus
}
#endif
#endif
