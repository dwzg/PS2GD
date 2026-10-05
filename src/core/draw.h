/* Higher level shape helpers built on the gfx primitives. */
#ifndef PD_DRAW_H
#define PD_DRAW_H

#include "gfx.h"

void draw_init(void);

/*
 * Pixel grid: device pixels per virtual pixel of the screen the game is
 * shown on (0: none, draw anywhere). Builds start with their console's
 * (PIXEL_GRID in common.h: 1 on the PS2, 272/448 on the PSP); the PC build
 * sets its window's scale and pd_tool others for the pictures it draws.
 * Text (font.c), icons (icons.c) and strokes (grid_w, grid_h) are drawn on
 * whole device pixels: a border 2.4 pixels wide would otherwise come out 2
 * or 3 pixels wide depending on where it lands.
 *
 * Down the screen the grid can differ from the one across it, where the
 * virtual screen is stretched to the screen's height (the PS2's PAL
 * picture shows the 448 virtual lines on 512): draw_set_pixel_grid sets
 * both, draw_set_pixel_grid_y then the one down the screen alone.
 */
void draw_set_pixel_grid(float g);
void draw_set_pixel_grid_y(float g);
float draw_pixel_grid(void);   /* across */
float draw_pixel_grid_y(void); /* down */
/* A stroke width as a whole number of device pixels (at least one) on the
 * grid: it covers that many pixels wherever it lands. Unchanged without.
 * grid_w for a width across the screen (a vertical line), grid_h for a
 * height down it (a horizontal one). */
float grid_w(float w);
float grid_h(float h);
/* v moved onto the nearest device pixel edge on the grid; unchanged without.
 * grid_snap for an x, grid_snap_y for a y. */
float grid_snap(float v);
float grid_snap_y(float v);

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
