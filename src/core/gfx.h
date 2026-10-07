/*
 * Rendering backend interface. Everything is drawn from flat/gouraud shaded
 * triangles and rectangles in virtual 640x448 screen space; the PS2 backend
 * maps these onto gsKit primitives and the PC backend onto SDL geometry.
 */
#ifndef PD_GFX_H
#define PD_GFX_H

#include "common.h"

enum { BLEND_ALPHA = 0, BLEND_ADD = 1 };

void gfx_blend(int mode);

void gfx_tri(float x0, float y0, Color c0,
             float x1, float y1, Color c1,
             float x2, float y2, Color c2);

/* Convex quad given as 4 points in perimeter order. */
void gfx_quad(const float *xy, const Color *c);

void gfx_rect(float x0, float y0, float x1, float y1, Color c);

/* Vertical gradient: top color at y0, bottom color at y1. */
void gfx_rect_v(float x0, float y0, float x1, float y1, Color top, Color bottom);

/* Horizontal gradient: left color at x0, right color at x1. */
void gfx_rect_h(float x0, float y0, float x1, float y1, Color left, Color right);

/* Optional, for backends built with GFX_GLOW (the PSP's): a round glow,
 * color c at the center fading out to nothing at r, drawn in one piece
 * (from a texture) with the current blend mode. Returns 0 when it did not
 * draw it; draw_glow() then draws it as a fan of triangles, as on the other
 * backends. */
#ifdef GFX_GLOW
int gfx_glow(float cx, float cy, float r, Color c);
#endif

/* Optional, for backends built with GFX_DEVICE_RECTS (the DS's): a
 * rectangle in whole device pixels of the pixel grid (draw.h: x0 is the
 * device pixel at grid_snap(x0 / grid)), its colour going from top at y0
 * to bottom at y1. Text on the grid (font.c) is drawn with it, worked out
 * in integers, where floating point is done in software. */
#ifdef GFX_DEVICE_RECTS
void gfx_rect_dev(int x0, int y0, int x1, int y1, Color top, Color bottom);
/* And a glyph of the font (font.h: ch as font_glyph takes it), its top
 * left corner at device pixel (x, y), each of its 5x7 pixels px by py
 * device pixels, coloured from top to bottom down it: the backend draws
 * it whole (the DS: a square of a texture of the font), where a glyph
 * would otherwise be a rectangle for each run of its pixels. */
void gfx_glyph_dev(int x, int y, int px, int py, unsigned char ch, Color top, Color bottom);
/* And the outline around such a glyph, ox device pixels across and oy
 * down, in colour c: the pixels within that of the glyph's but not its
 * own, once each where the outlines of glyphs side by side meet, so that
 * a see-through outline comes out even. Returns 0 when it did not draw it
 * (font.c then draws the outline as it does elsewhere). */
int gfx_glyph_outline_dev(int x, int y, int px, int py, int ox, int oy, unsigned char ch, Color c);
#endif

#endif
