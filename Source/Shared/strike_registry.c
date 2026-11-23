/* Single-game registry for the Strike Trilogy X360 port: replaces the reference's
 * games/registry.c, which links all three games into one program. Each XEX links only its own
 * game module and recompiled code (game_config.h of Source/<Game>), so the three executables are
 * independent. Same functions and behaviour as the reference. */
#include <string.h>
#include "game.h"
#include "util/sha1.h"
#include "game_config.h"

extern const game_module STRIKE_GAME_SYM;

const game_module *const g_games[] = { &STRIKE_GAME_SYM };
const int g_game_count = 1;

const game_module *game_find_sha1(const char *sha1)
{
    return strcmp(STRIKE_GAME_SYM.rom_sha1, sha1) ? 0 : &STRIKE_GAME_SYM;
}

const game_module *game_find_id(const char *id)
{
    return strcmp(STRIKE_GAME_SYM.id, id) ? 0 : &STRIKE_GAME_SYM;
}

const game_module *game_identify_rom(void)
{
    char h[41];
    sha1_hex(g_rom, g_rom_size, h);
    return game_find_sha1(h);
}

void game_boot(const game_module *g)
{
    cpu_lookup = g->lookup;
    gen_reset();
    g_ws_flight_frame = 0;
    cpu_reset();
}

const game_module *strike_this_game(void) { return &STRIKE_GAME_SYM; }
