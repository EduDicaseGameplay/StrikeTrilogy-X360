/* Files on the console for the Strike Trilogy X360 port (Win32 file API of the XDK, no stdio). */
#include <xtl.h>
#include <string.h>
#include <stdlib.h>
#include "genesis.h"
#include "cpu.h"
#include "util/sha1.h"
#include "strike_storage.h"

long strike_file_read(const char *path, void *buf, long max)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD got = 0;
    if (f == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(f, buf, (DWORD)max, &got, NULL)) got = 0;
    CloseHandle(f);
    return (long)got;
}

int strike_file_write(const char *path, const void *buf, long n)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD put = 0;
    int ok;
    if (f == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(f, buf, (DWORD)n, &put, NULL) && put == (DWORD)n;
    FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}

void strike_dir_create(const char *path) { CreateDirectoryA(path, NULL); }

static void join(char *out, int n, const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la + lb + 1 > n) { out[0] = 0; return; }
    memcpy(out, a, (size_t)la);
    memcpy(out + la, b, (size_t)lb + 1);
}

int strike_rom_load(const char *name, char sha1_out[41], char *path_out, int path_n)
{
    static const char *const dirs[] = { "game:\\rom\\", "game:\\roms\\", "game:\\" };
    static uint8_t buf[0x400000];
    char path[260];
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        join(path, sizeof path, dirs[i], name);
        long n = strike_file_read(path, buf, sizeof buf);
        if (n <= 0) continue;
        if (path_out) { strncpy(path_out, path, (size_t)path_n - 1); path_out[path_n - 1] = 0; }
        sha1_hex(buf, (size_t)n, sha1_out);
        return gen_set_rom(buf, (uint32_t)n);
    }
    if (path_out) { join(path, sizeof path, dirs[0], name); strncpy(path_out, path, (size_t)path_n - 1); path_out[path_n - 1] = 0; }
    return 0;
}

/* ---- settings: game:\<id>.cfg, "key=value" lines, written at every change ---- */
static int parse_int(const char *s) { int v = 0, neg = 0; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

static void cfg_path(char *out, int n, const char *game_id)
{
    char name[64];
    join(name, sizeof name, game_id, ".cfg");
    join(out, n, "game:\\", name);
}

void strike_settings_load(const char *game_id, strike_settings *s)
{
    char path[260], text[1024];
    memset(s, 0, sizeof *s);
    s->sprite_limits = 1;
    s->widescreen = 2;
    s->minimap = 1;
    cfg_path(path, sizeof path, game_id);
    long n = strike_file_read(path, text, sizeof text - 1);
    if (n <= 0) return;
    text[n] = 0;
    for (char *line = text; line && *line; ) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            int v = parse_int(eq + 1);
            if (!strcmp(line, "speed_original")) s->speed_original = v != 0;
            else if (!strcmp(line, "filter_linear")) s->filter_linear = v != 0;
            else if (!strcmp(line, "aspect")) s->aspect = v >= 0 && v <= 2 ? v : 0;
            else if (!strcmp(line, "sprite_limits")) s->sprite_limits = v != 0;
            else if (!strcmp(line, "slot")) s->slot = v >= 0 && v <= 9 ? v : 0;
            else if (!strcmp(line, "widescreen")) s->widescreen = v >= 0 && v <= 2 ? v : 2;
            else if (!strcmp(line, "minimap")) s->minimap = v != 0;
        }
        line = next;
    }
}

static char *put_kv(char *p, const char *k, int v)
{
    size_t lk = strlen(k);
    memcpy(p, k, lk); p += lk;
    *p++ = '=';
    if (v < 0) { *p++ = '-'; v = -v; }
    char num[12]; int d = 0;
    do { num[d++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (d) *p++ = num[--d];
    *p++ = '\r'; *p++ = '\n';
    return p;
}

void strike_settings_save(const char *game_id, const strike_settings *s)
{
    char path[260], text[512], *p = text;
    cfg_path(path, sizeof path, game_id);
    p = put_kv(p, "speed_original", s->speed_original);
    p = put_kv(p, "filter_linear", s->filter_linear);
    p = put_kv(p, "aspect", s->aspect);
    p = put_kv(p, "sprite_limits", s->sprite_limits);
    p = put_kv(p, "slot", s->slot);
    p = put_kv(p, "widescreen", s->widescreen);
    p = put_kv(p, "minimap", s->minimap);
    strike_file_write(path, text, (long)(p - text));
}

/* ---- save states: game:\states\<id>_<slot>.sav = "STX3", size, CPU + hardware state ---- */
static void state_path(char *out, int n, const char *game_id, int slot)
{
    char name[64];
    char s[4] = { '_', (char)('0' + (slot % 10)), 0, 0 };
    join(name, sizeof name, game_id, s);
    join(out, n, "game:\\states\\", name);
    join(name, sizeof name, out, ".sav");
    strncpy(out, name, (size_t)n - 1);
    out[n - 1] = 0;
}

int strike_state_save(const char *game_id, int slot)
{
    char path[260];
    size_t n = cpu_state_size();
    uint8_t *b = (uint8_t *)malloc(n + 8);
    int ok;
    if (!b) return 0;
    memcpy(b, "STX3", 4);
    b[4] = (uint8_t)(n >> 24); b[5] = (uint8_t)(n >> 16); b[6] = (uint8_t)(n >> 8); b[7] = (uint8_t)n;
    cpu_state_save(b + 8);
    strike_dir_create("game:\\states");
    state_path(path, sizeof path, game_id, slot);
    ok = strike_file_write(path, b, (long)(n + 8));
    free(b);
    return ok;
}

int strike_state_load(const char *game_id, int slot)
{
    char path[260];
    size_t n = cpu_state_size();
    uint8_t *b = (uint8_t *)malloc(n + 8);
    int ok = 0;
    if (!b) return 0;
    state_path(path, sizeof path, game_id, slot);
    long got = strike_file_read(path, b, (long)(n + 8));
    if (got == (long)(n + 8) && !memcmp(b, "STX3", 4) &&
        (((size_t)b[4] << 24) | ((size_t)b[5] << 16) | ((size_t)b[6] << 8) | b[7]) == n)
        ok = cpu_state_load(b + 8);
    free(b);
    if (ok) gen_seek_inputs(g_frame);
    return ok;
}
