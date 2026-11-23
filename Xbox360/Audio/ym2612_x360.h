/* YM2612 (ymfm) for the Strike Trilogy X360 port: two chips.
 * The game chip lives on the emulation thread: it takes every register write and runs the two
 * timers per sample, so the status the Z80 reads is exactly the reference's (ymfm's busy flag is
 * always clear, and the status only depends on writes and timers). It never generates audio.
 * The audio chip lives on the sound thread: it replays the same writes at the same sample index
 * and generates the output. Method of SoR2-X360 port/runtime/src/sound.cpp (SOUND_THREAD). */
#ifndef YM2612_X360_H
#define YM2612_X360_H
#include <stddef.h>
#include <stdint.h>
#include "ym2612.h"          /* reference API: ym_init, ym_reset, ym_write, ym_status, ym_sample, state */
#ifdef __cplusplus
extern "C" {
#endif
void ym_tick(void);                          /* game chip: advance the timers by one sample (144 clocks) */
void ym_audio_reset(void);
void ym_audio_write(int port, uint8_t v);
void ym_audio_sample(int32_t out[2]);        /* generate one sample on the audio chip */
int  ym_audio_state_load(const uint8_t *in, size_t n);   /* audio chip from a game-chip state */
#ifdef __cplusplus
}
#endif
#endif
