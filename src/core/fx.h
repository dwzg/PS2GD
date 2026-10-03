/* Particle effects (death bursts, trails, portal sparkles, menu confetti). */
#ifndef PD_FX_H
#define PD_FX_H

#include "common.h"

enum { FX_SQUARE = 0, FX_CIRCLE, FX_RING };
enum { FX_WORLD = 0, FX_SCREEN = 1 };

typedef struct {
    float x, y, vx, vy;
    float life, max_life;
    float size, size_end;
    float rot, vrot;
    float grav, drag;
    Color c;
    uint8_t shape, add, space, active;
} Particle;

void fx_clear(void);
void fx_clear_space(int space);
/* Move every particle of a space sideways (the title demo's loop jumps back). */
void fx_shift(int space, float dx);
Particle *fx_spawn(int space);
void fx_update(float dt);
/*
 * World particles are drawn with the camera transform (blocks -> pixels).
 * `back` rewinds each particle along its velocity by that many seconds
 * (render interpolation between ticks).
 */
void fx_draw(int space, float cam_x, float cam_y, float back);

/* Common bursts */
void fx_burst(int space, float x, float y, int n, float speed, float size, float life, Color c, int add);
void fx_ring(int space, float x, float y, float r0, float r1, float life, Color c);

#endif
