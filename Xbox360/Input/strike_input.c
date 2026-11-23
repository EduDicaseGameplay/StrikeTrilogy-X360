/*
 * Xbox 360 controller for the Strike Trilogy X360 port (XInput).
 *
 * Mapping (the reference's Classic and Modern layouts at once, src/host/input.c):
 *   D-pad or left stick      Genesis D-pad
 *   X  or LT                 Genesis A   (Hellfire; held = strafe, as on the Genesis)
 *   A                        Genesis B   (Hydras)
 *   LB / RB                  previous / next objective on the minimap (the pause map's selection)
 *   B  or RT                 Genesis C   (chain gun)
 *   Start                    Genesis Start (pause / status map)
 *   Y                        strafe: holds Genesis A for at least 16 frames, so it never fires;
 *                            A (Genesis A) pressed while strafing fires a Hellfire and the
 *                            strafe goes on (reference input_pad_state, issue #9)
 *   right stick left/right   strafe in that direction (reference Right Stick Strafe)
 * Back + button: host commands (strike_input.h); the game sees no buttons while Back is held.
 * An empty port is asked again only every 2 s: XInputGetState on it is slow (KI-X360).
 */
#include <xtl.h>
#include <xinput2.h>
#include <string.h>
#include "genesis.h"
#include "strike_input.h"

#define DEADZONE  12000
#define TRIGGER   64

typedef struct { int connected; DWORD last_try; XINPUT_GAMEPAD g; } Pad;
static Pad pads[2];
static uint16_t out1, out2;
static int cmd, mm_step;
static WORD prev_buttons;
static DWORD quit_since, reset_since;

/* strafe state (reference input_pad_state) */
static uint64_t strafe_from, tap_at;
static int strafing, tapping, a_was;

static uint16_t plain_bits(const XINPUT_GAMEPAD *g)
{
    uint16_t s = 0;
    WORD b = g->wButtons;
    if ((b & XINPUT_GAMEPAD_DPAD_UP) || g->sThumbLY > DEADZONE) s |= GB_UP;
    if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || g->sThumbLY < -DEADZONE) s |= GB_DOWN;
    if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || g->sThumbLX < -DEADZONE) s |= GB_LEFT;
    if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || g->sThumbLX > DEADZONE) s |= GB_RIGHT;
    if ((b & XINPUT_GAMEPAD_X) || g->bLeftTrigger > TRIGGER) s |= GB_A;
    if (b & XINPUT_GAMEPAD_A) s |= GB_B;
    if ((b & XINPUT_GAMEPAD_B) || g->bRightTrigger > TRIGGER) s |= GB_C;
    if (b & XINPUT_GAMEPAD_START) s |= GB_START;
    return s;
}

static uint16_t fix_opposites(uint16_t s)
{
    if ((s & (GB_UP | GB_DOWN)) == (GB_UP | GB_DOWN)) s &= (uint16_t)~(GB_UP | GB_DOWN);
    if ((s & (GB_LEFT | GB_RIGHT)) == (GB_LEFT | GB_RIGHT)) s &= (uint16_t)~(GB_LEFT | GB_RIGHT);
    return s;
}

static int pressed(WORD now, WORD mask) { return (now & mask) && !(prev_buttons & mask); }

void strike_input_poll(uint64_t frame)
{
    DWORD now = GetTickCount();
    for (int p = 0; p < 2; p++) {
        Pad *pad = &pads[p];
        XINPUT_STATE st;
        if (!pad->connected && pad->last_try && now - pad->last_try < 2000) continue;
        pad->last_try = now;
        if (XInputGetState((DWORD)p, &st) != ERROR_SUCCESS) { pad->connected = 0; memset(&pad->g, 0, sizeof pad->g); continue; }
        pad->connected = 1;
        pad->g = st.Gamepad;
    }
    const XINPUT_GAMEPAD *g = &pads[0].g;
    WORD b = g->wButtons;
    cmd = STRIKE_CMD_NONE;
    mm_step = 0;
    out2 = pads[1].connected ? fix_opposites(plain_bits(&pads[1].g)) : 0;

    if (b & XINPUT_GAMEPAD_BACK) {
        /* host commands; the game sees nothing while Back is held */
        if (pressed(b, XINPUT_GAMEPAD_LEFT_SHOULDER)) cmd = STRIKE_CMD_SAVE_STATE;
        else if (pressed(b, XINPUT_GAMEPAD_RIGHT_SHOULDER)) cmd = STRIKE_CMD_LOAD_STATE;
        else if (pressed(b, XINPUT_GAMEPAD_DPAD_RIGHT)) cmd = STRIKE_CMD_NEXT_SLOT;
        else if (pressed(b, XINPUT_GAMEPAD_DPAD_LEFT)) cmd = STRIKE_CMD_PREV_SLOT;
        else if (pressed(b, XINPUT_GAMEPAD_Y)) cmd = STRIKE_CMD_SPEED;
        else if (pressed(b, XINPUT_GAMEPAD_X)) cmd = STRIKE_CMD_FILTER;
        else if (pressed(b, XINPUT_GAMEPAD_A)) cmd = STRIKE_CMD_ASPECT;
        else if (pressed(b, XINPUT_GAMEPAD_B)) cmd = STRIKE_CMD_FLICKER;
        else if (pressed(b, XINPUT_GAMEPAD_DPAD_UP)) cmd = STRIKE_CMD_WIDESCREEN;
        else if (pressed(b, XINPUT_GAMEPAD_DPAD_DOWN)) cmd = STRIKE_CMD_MINIMAP;
        if (b & XINPUT_GAMEPAD_START) {
            if (!quit_since) quit_since = now ? now : 1;
            else if (now - quit_since >= 3000) cmd = STRIKE_CMD_QUIT;
        } else quit_since = 0;
        if (g->bLeftTrigger > TRIGGER && g->bRightTrigger > TRIGGER) {
            if (!reset_since) reset_since = now ? now : 1;
            else if (now - reset_since >= 2000) { cmd = STRIKE_CMD_RESET; reset_since = 0xFFFFFFFFu; }
        } else reset_since = 0;
        prev_buttons = b;
        out1 = 0;
        strafing = tapping = 0;
        a_was = 0;
        return;
    }
    quit_since = 0;
    reset_since = 0;

    if (pressed(b, XINPUT_GAMEPAD_LEFT_SHOULDER)) mm_step = -1;
    else if (pressed(b, XINPUT_GAMEPAD_RIGHT_SHOULDER)) mm_step = 1;
    uint16_t s = plain_bits(g);
    int a_now = (s & GB_A) != 0;
    /* Right stick strafing: left/right holds A with that direction. */
    int rdir = 0;
    if (g->sThumbRX > DEADZONE + 4000) rdir = 1;
    else if (g->sThumbRX < -(DEADZONE + 4000)) rdir = -1;
    if (rdir) { s &= (uint16_t)~(GB_LEFT | GB_RIGHT); s |= rdir > 0 ? GB_RIGHT : GB_LEFT; }
    if ((b & XINPUT_GAMEPAD_Y) || rdir) {
        if (!strafing) { strafing = 1; strafe_from = frame; tapping = 0; }
        if (a_now && !a_was && !tapping) { tapping = 1; tap_at = frame; }
        if (tapping) {
            uint64_t t = frame - tap_at;
            if (t == 0 || t == 4) s &= (uint16_t)~GB_A;           /* let go: no fire after a long hold */
            else if (t < 4) s |= GB_A;                            /* the tap */
            else { tapping = 0; strafe_from = frame; s |= GB_A; } /* hold again */
        } else s |= GB_A;
    } else if (strafing) {
        if (frame - strafe_from < 16) s |= GB_A;
        else strafing = 0;
        tapping = 0;
    }
    a_was = a_now;
    out1 = fix_opposites(s);
    prev_buttons = b;
}

uint16_t strike_input_genesis(void) { return out1; }
uint16_t strike_input_genesis2(void) { return out2; }
int strike_input_command(void) { return cmd; }
int strike_input_mm_step(void) { return mm_step; }
int strike_input_connected(void) { return pads[0].connected; }
