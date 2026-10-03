/* World rendering: background, ground, level objects. */
#ifndef PD_RENDER_H
#define PD_RENDER_H

#include "level.h"
#include "sim.h"
#include "theme.h"

typedef struct {
    float cam_x, cam_y;
    float time;  /* seconds, drives animation */
    float pulse; /* 0..1 beat pulse */
    const Palette *pal;
} View;

static inline float view_sx(const View *v, float wx) { return (wx - v->cam_x) * BLOCK_PX; }
static inline float view_sy(const View *v, float wy) { return SCREEN_H - (wy - v->cam_y) * BLOCK_PX; }

void render_background(const View *v);
/* Ground band at y=0 plus optional corridor floor/ceiling bands. */
void render_ground(const View *v, float corr_floor, float corr_ceil, float corr_alpha);
/* Level geometry and objects. p may be NULL (no per-attempt state). */
void render_level(const View *v, const Level *L, const Player *p, uint8_t saved_coins);

/* Individual object painters (also used by menus). */
void render_spike(float sx, float sy_base, float w, float h, int down, Color fill, Color edge);
void render_orb(float cx, float cy, float r, Color c, float time, float pulse);
void render_portal(float cx, float cy, Color c, float time, int mode_icon, int grav_arrow);
void render_saw(float cx, float cy, float r, float angle, Color fill, Color edge);
void render_coin(float cx, float cy, float r, float spin, float alpha, int ghost);

/* Small helpers for UI */
void render_panel(float x0, float y0, float x1, float y1, Color fill, Color edge);
void render_progress_bar(float x0, float y0, float x1, float y1, float frac, Color fill_a, Color fill_b);

#endif
