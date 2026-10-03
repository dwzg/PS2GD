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

#endif
