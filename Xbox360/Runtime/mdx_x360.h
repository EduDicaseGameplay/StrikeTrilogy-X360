/* MDX360-Runtime, Xbox 360 platform layer (XDK through the OXDK clang Xenon toolchain).
 * Plain C interface: video (D3D9 on Xenos), vertical-blank pacing, audio (XAudio2 stream with a
 * ring and dynamic rate), pads (XInput mapped to Mega Drive buttons), log. Methods follow the
 * console-tested SoR2-X360 platform code (see ORIGEM.txt). */
#ifndef MDX_X360_H
#define MDX_X360_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- log: game:\<name>_log.txt (mdx_log_set_path before the first line), flushed per line ---- */
void mdx_log_set_path(const char *path);
void mdx_log(const char *text);
void mdx_log_int(const char *label, int v);     /* "label v" */
void mdx_install_crash_logger(void);

/* ---- video ---- */
enum { MDX_ASPECT_4_3 = 0, MDX_ASPECT_PIXEL = 1, MDX_ASPECT_STRETCH = 2 };
int  mdx_video_init(void);                 /* 0 on failure; 1280x720 or 640x480 by the console's mode */
int  mdx_video_width(void);
int  mdx_video_height(void);
void mdx_video_set_aspect(int mode);       /* MDX_ASPECT_*; 4:3 by default, never distorts unless STRETCH */
void mdx_video_set_filter(int linear);     /* 0 nearest (default), 1 linear */
/* Uploads the frame (0x00RRGGBB words, pitch in pixels), draws it and queues the flip. */
void mdx_video_present(const uint32_t *fb, int w, int h, int pitch, int extra_side);
void mdx_wait_vblank(void);
/* Strike: overlay in screen pixels (A8R8G8B8, up to 512x512) drawn over the next presents; px NULL = none.
 * The pixels are read at present time. */
void mdx_video_set_overlay(const uint32_t *px, int w, int h, int pitch, int x, int y);
int  mdx_video_is_widescreen(void);        /* the console's video mode is 16:9 */
void mdx_video_picture_rect(int r[4]);     /* x, y, w, h of the last picture on screen */                /* returns at the start of the next vertical blank */

/* ---- audio ---- */
int      mdx_audio_init(int rate);         /* 48000 stereo int16 */
void     mdx_audio_queue(const int16_t *frames, int count);
int      mdx_audio_queued(void);           /* stereo frames waiting */
/* Feeds one emulated frame of md_sound_out and returns the md_sound_rate_adjust that holds the
 * queue at a 50 ms cushion (±0.5 %), as the SoR2-X360 loop does. */
double   mdx_audio_feed_frame(const int16_t *frames, int count, int rate);
/* Strike: queue a frame at the source rate and steer XAudio2's frequency ratio instead. */
float    mdx_audio_feed_ratio(const int16_t *frames, int count, int rate);

/* ---- threads ---- */
typedef int (*mdx_thread_fn)(void *arg);
int  mdx_thread_start(mdx_thread_fn fn, void *arg, int hw_thread, int stack_kb);  /* 0 on failure */
void *mdx_event_create(void);              /* auto-reset event */
void mdx_event_set(void *e);
void mdx_event_wait(void *e);

/* ---- time ----
 * 32-bit: the low word of the performance counter (wraps after over a minute; use differences).
 * Converting 64-bit integers to double made clang Xenon pass them as two 32-bit stores and one
 * 64-bit load, which Xenia's JIT rejects ("Invalid handling of constant" and a breakpoint). */
uint32_t mdx_ticks(void);
uint32_t mdx_ticks_per_second(void);

#ifdef __cplusplus
}
#endif
#endif
