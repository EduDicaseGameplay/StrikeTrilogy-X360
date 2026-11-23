/* On-screen messages for the Strike Trilogy X360 port. See strike_osd.c. */
#ifndef STRIKE_OSD_H
#define STRIKE_OSD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void strike_osd_message(const char *text, int frames);           /* show a line for N frames */
void strike_osd_draw(uint32_t *fb, int pitch, int w, int h, int scale);   /* draw it over the frame */
void strike_osd_page(uint32_t *fb, int pitch, int w, int h, const char *const *lines, int n);
void strike_osd_text(uint32_t *fb, int pitch, int w, int h, int x, int y, const char *s, uint32_t color, int scale);
void strike_osd_box(uint32_t *fb, int pitch, int w, int h, int x, int y, int bw, int bh);
int  strike_osd_text_width(const char *s, int scale);
#ifdef __cplusplus
}
#endif
#endif
