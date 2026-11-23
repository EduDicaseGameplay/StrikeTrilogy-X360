/* The reference's VDP renderer (Reference/StrikeHDCollection/src/hw/vdp_render.c, MIT), compiled
 * unchanged but reading a copy of the VDP memories taken at the start of vblank, so it can run on
 * the render thread while the 68000 goes on with the next frame (strike_render.c).
 * The renderer only reads g_vram, g_cram, g_vsram and g_vreg (checked with grep on the reference). */
#include "genesis.h"

extern uint8_t  snap_vram[0x10000];
extern uint16_t snap_cram[64];
extern uint16_t snap_vsram[40];
extern uint8_t  snap_vreg[32];

#define g_vram  snap_vram
#define g_cram  snap_cram
#define g_vsram snap_vsram
#define g_vreg  snap_vreg
#include "hw/vdp_render.c"
