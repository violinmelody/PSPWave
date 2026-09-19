#ifndef PSPWAVE_RENDER_H
#define PSPWAVE_RENDER_H

#include <stdint.h>

void render_init(void);
void render_begin(uint32_t accent);
void render_end(void);
void render_rect(int x, int y, int w, int h, uint32_t c);
void render_text(int x, int y, int scale, uint32_t c, const char *s);
uint32_t render_rgb(int r, int g, int b);
uint32_t render_hsv(float h, float s, float v);

#endif
