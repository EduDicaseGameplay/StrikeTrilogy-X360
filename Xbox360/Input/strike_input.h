/* Xbox 360 controller for the Strike Trilogy X360 port. See strike_input.c. */
#ifndef STRIKE_INPUT_H
#define STRIKE_INPUT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {                                  /* host commands, from Back + a button (edge triggered) */
    STRIKE_CMD_NONE = 0,
    STRIKE_CMD_SAVE_STATE,              /* Back + LB */
    STRIKE_CMD_LOAD_STATE,              /* Back + RB */
    STRIKE_CMD_NEXT_SLOT,               /* Back + D-pad right */
    STRIKE_CMD_PREV_SLOT,               /* Back + D-pad left */
    STRIKE_CMD_SPEED,                   /* Back + Y: game speed Original / Smooth */
    STRIKE_CMD_FILTER,                  /* Back + X: sharp / smooth picture */
    STRIKE_CMD_ASPECT,                  /* Back + A: 4:3 / pixel aspect / stretch */
    STRIKE_CMD_FLICKER,                 /* Back + B: sprite limit (console flicker) on / off */
    STRIKE_CMD_WIDESCREEN,              /* Back + D-pad up */
    STRIKE_CMD_MINIMAP,                 /* Back + D-pad down */
    STRIKE_CMD_RESET,                   /* Back + LT + RT held 2 s: console reset */
    STRIKE_CMD_QUIT                     /* Back + Start held 3 s: back to the launcher */
};

void     strike_input_poll(uint64_t frame);  /* once per frame, before the game runs */
uint16_t strike_input_genesis(void);    /* pad 1 as Genesis GB_* bits (0 while Back is held) */
uint16_t strike_input_genesis2(void);   /* pad 2, plain mapping */
int      strike_input_command(void);    /* STRIKE_CMD_* of this frame */
int      strike_input_connected(void);
int      strike_input_mm_step(void);    /* LB -1 / RB +1 this frame, else 0 */

#ifdef __cplusplus
}
#endif
#endif
