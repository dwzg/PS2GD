/*
 * The particles (fx.h) on the GBA, in fixed point (fx_gba.c): in pixels as
 * the GBA draws them, for sprites.c.
 */
#ifndef PD_GBA_FX_GBA_H
#define PD_GBA_FX_GBA_H

#include <stdint.h>
#include "fx.h"

typedef struct {
    int32_t x, y;   /* pixels (16.16): in the world, y down (world.h), or on the GBA's screen */
    int32_t vx, vy; /* pixels a tick */
    int32_t gy;     /* added to vy every tick (the gravity) */
    int32_t k;      /* the drag: v times k / 65536 every tick */
    int32_t life;   /* ticks left (8.8) */
    int32_t inv;    /* 65536 * 256 / its life at the start (8.8), for how far through it is */
    int32_t s0, s1; /* its size at the start and at the end, pixels (8.8) */
} FxFix;

/* the particles' motion, as fx_particles' slots (those active 2; one the
 * core spawned in floats is active 1 until fx_fix or fx_update takes it
 * over) */
extern FxFix g_fx[];
/* Particle i of fx_particles in fixed point (taken over now if it was
 * spawned since the latest fx_update); NULL if it is not active. */
const FxFix *fx_fix(int i);
/* How far through its life it is, 0..256. */
static inline int fx_t8(const FxFix *f)
{
    int t = 256 - (f->life * f->inv >> 16);
    return t < 0 ? 0 : (t > 256 ? 256 : t);
}

#endif
