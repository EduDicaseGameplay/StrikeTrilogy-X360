/* Files on the console for the Strike Trilogy X360 port: ROM, save states, settings.
 * Every path uses backslashes and the game:\ device (the XDK accepts nothing else,
 * PORTING_PLAYBOOK 18). See strike_storage.c. */
#ifndef STRIKE_STORAGE_H
#define STRIKE_STORAGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int speed_original;      /* 0 Smooth (20000 instructions per frame, the reference default), 1 Original */
    int filter_linear;       /* 0 sharp pixels, 1 bilinear */
    int aspect;              /* MDX_ASPECT_* */
    int sprite_limits;       /* 1 console sprite limits (flicker), 0 unlimited */
    int slot;                /* save-state slot 0-9 */
    int widescreen;          /* 2 auto (on when the console is set to 16:9), 1 on, 0 off */
    int minimap;             /* 1 on (default), 0 off */
} strike_settings;

long strike_file_read(const char *path, void *buf, long max);       /* bytes read, -1 if missing */
int  strike_file_write(const char *path, const void *buf, long n);  /* 1 on success */
void strike_dir_create(const char *path);

/* Finds the ROM (game:\rom\<name>, game:\roms\<name>, game:\<name>) into g_rom.
 * Returns 1 and fills sha1_out (41 chars) when found; path_out gets the tried/used path. */
int  strike_rom_load(const char *name, char sha1_out[41], char *path_out, int path_n);

void strike_settings_load(const char *game_id, strike_settings *s);
void strike_settings_save(const char *game_id, const strike_settings *s);

int  strike_state_save(const char *game_id, int slot);   /* between frames only */
int  strike_state_load(const char *game_id, int slot);

#ifdef __cplusplus
}
#endif
#endif
