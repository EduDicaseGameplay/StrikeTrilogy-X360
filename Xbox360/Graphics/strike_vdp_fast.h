/* Fast VDP renderer (same pixels as the reference vdp_render 1x path). See strike_vdp_fast.c. */
#ifndef STRIKE_VDP_FAST_H
#define STRIKE_VDP_FAST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t pal[64], pal_s[64], pal_h[64];
    int h40, W, ext, n, limits, wide;
} strike_vdp_frame;
/* Once per frame, from the snap_* copy. */
void strike_vdp_fast_prepare(strike_vdp_frame *f, int ext, int limits, int wide);
/* Lines [l0, l1): independent, may run on several threads at once. */
void strike_vdp_fast_lines(const strike_vdp_frame *f, uint32_t *fb, int pitch, int l0, int l1);
#ifdef __cplusplus
}
#endif
#endif
