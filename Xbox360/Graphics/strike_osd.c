/* On-screen messages for the Strike Trilogy X360 port: a 5x7 font (drawn for this project)
 * written over the finished Genesis frame before it is presented, and full-screen text pages
 * (missing ROM, wrong ROM, fatal error). Uppercase only; other characters show as blanks. */
#include <string.h>
#include "strike_osd.h"

static const struct { char c; unsigned char row[7]; } glyphs[] = {
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
    {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}},
    {'J',{7,2,2,2,2,18,12}},      {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}}, {'N',{17,17,25,21,19,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}},   {'T',{31,4,4,4,4,4,4}},       {'U',{17,17,17,17,17,17,14}},
    {'V',{17,17,17,17,17,10,4}},  {'W',{17,17,17,21,21,21,10}}, {'X',{17,17,10,4,10,17,17}},
    {'Y',{17,17,10,4,4,4,4}},     {'Z',{31,1,2,4,8,16,31}},
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},      {'2',{14,17,1,2,4,8,31}},
    {'3',{31,2,4,2,1,17,14}},     {'4',{2,6,10,18,31,2,2}},     {'5',{31,16,30,1,1,17,14}},
    {'6',{6,8,16,30,17,17,14}},   {'7',{31,1,2,4,8,8,8}},       {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,2,12}},
    {':',{0,12,12,0,12,12,0}},    {'.',{0,0,0,0,0,12,12}},      {'-',{0,0,0,31,0,0,0}},
    {'/',{1,2,2,4,8,8,16}},       {'\\',{16,8,8,4,2,2,1}},      {'_',{0,0,0,0,0,0,31}},
    {'%',{24,25,2,4,8,19,3}},     {'(',{2,4,8,8,8,4,2}},        {')',{8,4,2,2,2,4,8}},
    {'+',{0,4,4,31,4,4,0}},       {'!',{4,4,4,4,4,0,4}},        {',',{0,0,0,0,12,4,8}},
    {'=',{0,0,31,0,31,0,0}},      {'>',{8,4,2,1,2,4,8}},        {'<',{2,4,8,16,8,4,2}},
};

static const unsigned char *glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (unsigned i = 0; i < sizeof glyphs / sizeof glyphs[0]; i++)
        if (glyphs[i].c == c) return glyphs[i].row;
    return 0;
}

int strike_osd_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

void strike_osd_text(uint32_t *fb, int pitch, int w, int h, int x, int y, const char *s, uint32_t color, int scale)
{
    for (; *s; s++, x += 6 * scale) {
        const unsigned char *g = glyph(*s);
        if (!g) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++) {
                if (!(g[r] & (16 >> c))) continue;
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++) {
                        int px = x + c * scale + dx, py = y + r * scale + dy;
                        if (px >= 0 && px < w && py >= 0 && py < h) fb[py * pitch + px] = color;
                    }
            }
    }
}

void strike_osd_box(uint32_t *fb, int pitch, int w, int h, int x, int y, int bw, int bh)
{
    for (int py = y; py < y + bh; py++)
        for (int px = x; px < x + bw; px++)
            if (px >= 0 && px < w && py >= 0 && py < h) {
                uint32_t v = fb[py * pitch + px];
                fb[py * pitch + px] = (v >> 2) & 0x3F3F3F;    /* darken to a quarter */
            }
}

/* ---- message line ---- */
static char msg[64];
static int msg_frames;

void strike_osd_message(const char *text, int frames)
{
    strncpy(msg, text, sizeof msg - 1);
    msg[sizeof msg - 1] = 0;
    msg_frames = frames;
}

void strike_osd_draw(uint32_t *fb, int pitch, int w, int h, int s)
{
    if (msg_frames <= 0) return;
    msg_frames--;
    if (s < 1) s = 1;
    int tw = strike_osd_text_width(msg, s);
    int x = (w - tw) / 2, y = h - 18 * s;
    strike_osd_box(fb, pitch, w, h, x - 4 * s, y - 3 * s, tw + 7 * s, 13 * s);
    strike_osd_text(fb, pitch, w, h, x, y, msg, 0xFFFFFF, s);
}

void strike_osd_page(uint32_t *fb, int pitch, int w, int h, const char *const *lines, int n)
{
    for (int y = 0; y < h; y++) memset(fb + y * pitch, 0, (size_t)w * 4);
    int y = (h - n * 12) / 2;
    for (int i = 0; i < n; i++, y += 12) {
        int tw = strike_osd_text_width(lines[i], 1);
        strike_osd_text(fb, pitch, w, h, (w - tw) / 2, y, lines[i], i == 0 ? 0xFFD040 : 0xE0E0E0, 1);
    }
}
