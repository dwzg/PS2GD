/* 5x7 pixel font rendered with rectangles (no textures needed). */
#ifndef PD_FONT_H
#define PD_FONT_H

#include "common.h"

/* Special glyphs usable inside strings. */
#define GLYPH_CROSS "\x01"
#define GLYPH_CIRCLE "\x02"
#define GLYPH_SQUARE "\x03"
#define GLYPH_TRIANGLE "\x04"
#define GLYPH_STAR "\x05"
#define GLYPH_LEFT "\x06"
#define GLYPH_RIGHT "\x07"

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

void font_init(void);

/* Width in pixels of a string at the given pixel scale. */
float font_width(const char *s, float scale);
/* Height of the glyphs at the given pixel scale. */
float font_height(float scale);

/* Plain single-color text. y is the top of the glyphs. */
void font_draw(float x, float y, float scale, Color c, int align, const char *s);

/* Title-style text: vertical gradient fill with a dark outline. */
void font_draw_fancy(float x, float y, float scale, Color top, Color bottom, Color outline,
                     float outline_px, int align, const char *s);

#endif
