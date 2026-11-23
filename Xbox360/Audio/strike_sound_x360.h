/* Threaded FM generation for the Strike Trilogy X360 port (see strike_sound_x360.c). */
#ifndef STRIKE_SOUND_X360_H
#define STRIKE_SOUND_X360_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int      snd_thread_start(int hw_thread);   /* after the first gen_reset(); 0 on failure */
void     snd_thread_flush(void);            /* once per emulated frame: hand the batch over */
void     snd_thread_drain(void);            /* wait until every batch is mixed */
uint32_t snd_thread_take_us(void);          /* sound thread busy microseconds since the last call */
int      snd_is_threaded(void);
void     snd_set_discard(int on);           /* hidden frames: their sound is not played */
#ifdef __cplusplus
}
#endif
#endif
