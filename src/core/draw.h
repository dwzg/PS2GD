/* Higher level shape helpers built on the gfx primitives. */
#ifndef PD_DRAW_H
#define PD_DRAW_H

#include "gfx.h"

void draw_init(void);

void draw_line(float x0, float y0, float x1, float y1, float w, Color c);
void draw_line2(float x0, float y0, float x1, float y1, float w, Color c0, Color c1);
void draw_rect_outline(float x0, float y0, float x1, float y1, float w, Color c);

void draw_circle(float cx, float cy, float r, Color c);
void draw_circle_grad(float cx, float cy, float r, Color inner, Color outer);
void draw_ellipse(float cx, float cy, float rx, float ry, Color c);
void draw_ring(float cx, float cy, float r_in, float r_out, Color c);
void draw_ring_grad(float cx, float cy, float r_in, float r_out, Color c_in, Color c_out);
void draw_ellipse_ring(float cx, float cy, float rx, float ry, float thick, Color c,
                       float a0, float a1);
void draw_arc(float cx, float cy, float r_in, float r_out, float a0, float a1, Color c);

/* Additive radial glow: color c at the center fading to nothing at r. */
void draw_glow(float cx, float cy, float r, Color c);

/* Rotated rectangle around its center. */
void draw_rot_rect(float cx, float cy, float w, float h, float angle, Color c);
void draw_rot_rect_outline(float cx, float cy, float w, float h, float angle, float t, Color c);

/* Filled convex polygon (fan from the first vertex). */
void draw_poly(const float *xy, int n, Color c);
void draw_poly_outline(const float *xy, int n, float w, Color c);

/* Transform helper: rotate (x,y) by angle around the origin. */
static inline void rot2(float *x, float *y, float c, float s)
{
    float nx = *x * c - *y * s;
    float ny = *x * s + *y * c;
    *x = nx;
    *y = ny;
}

#endif
