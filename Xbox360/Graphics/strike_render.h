/* Render thread for the Strike Trilogy X360 port. See strike_render.c. */
#ifndef STRIKE_RENDER_H
#define STRIKE_RENDER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define STRIKE_FB_PITCH 1024            /* pixels per framebuffer row (VDP_MAX_W) */

/* hw_thread < 0: render synchronously on the caller (PC check, fallback). */
int  strike_render_start(int hw_thread);
/* From the vblank callback (emulation thread): copy the VDP memories and start rendering them. */
/* ext: extra pixels per side (widescreen, 0 = 4:3); active: this frame is the playfield (else the
 * sides are black); wide_sprites: sprite X read as signed 16-bit (reference, issue #11). */
void strike_render_vblank(int ext, int active, int wide_sprites);
/* The last finished frame: waits for the render in flight. Returns 0 if none yet.
 * The buffer stays valid until the next strike_render_vblank() after the following one. */
/* w, h, ext and pitch in output pixels; scale is always 1. */
int  strike_render_take(const uint32_t **fb, int *w, int *h, int *ext, int *pitch, int *scale);
/* Synchronous plain render of the live state (minimap capture); returns the display width. */
int  strike_render_capture(uint32_t *out);
/* Render thread busy microseconds since the last call. */
uint32_t strike_render_take_us(void);
/* Extra threads that draw bands of lines with the fast renderer (up to 3). Returns how many run. */
int  strike_render_add_workers(const int *hw_threads, int count);
/* 1: draw with the reference vdp_render instead of the fast renderer (PC comparison). */
void strike_render_use_reference(int on);
/* Sprite flicker (hardware per-line sprite limits): 1 as the console (default), 0 unlimited. */
void strike_render_set_sprite_limits(int on);

#ifdef __cplusplus
}
#endif
#endif
