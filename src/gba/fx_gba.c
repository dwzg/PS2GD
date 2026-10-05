/*
 * The particles (fx.h) on the GBA, in fixed point (fx_gba.h): fx.c's are
 * floats, and a float operation is a call of 60 to 100 cycles here, so
 * this file is fx.c with integers (target.h's FX_TARGET leaves fx.c out).
 * Positions are pixels (16.16) as the GBA draws them, speeds pixels a
 * tick, lives ticks; bursts and rings are made in fixed point from the
 * start. The particles the core spawns itself (fx_spawn, its fields set
 * as floats: play.c's dust) are taken over at the next look, their slot
 * marked active 2. The same particles as fx.c's, but for the random
 * draws (an integer generator, the directions from the sine table).
 */
#include <string.h>

#include "fx_gba.h"
#include "gba.h"
#include "video.h"
#include "art.h"
#include "gen.h"
#include "common.h"

#define MAX FX_MAX_PARTICLES

static Particle s_p[MAX];   /* the slots: shape, colour, space, active (and the floats of one just spawned) */
FxFix g_fx[MAX];            /* each active one's motion */
static int s_next, s_live;  /* the next slot round, and how many are active */
static uint32_t s_seed = 12345;

/* blocks (or the 640x448 layout's pixels) to the GBA's pixels, for a space */
static float kx_of(int space)
{
    return space == FX_WORLD ? (float)BLOCK_PIX : SCR_W / (float)SCREEN_W;
}
static float ky_of(int space)
{
    return space == FX_WORLD ? -(float)BLOCK_PIX : SCR_H / (float)SCREEN_H;
}

/* a float as a 16.16 number, times k */
#define FIX(v, k) ((int32_t)((v) * ((k) * 65536.0f)))

/* 16 random bits */
static uint32_t rnd16(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return s_seed >> 16;
}

/* v times (a + b * r), r random in [0, 1): a and b in 1/65536ths (v under
 * 2^19, a + b under 2^17: in 32 bits, no 64-bit multiplying in Thumb) */
static int32_t vary(int32_t v, int32_t a, int32_t b)
{
    int32_t f = a + (int32_t)((b * (int32_t)rnd16()) >> 16);
    return (v * (f >> 4)) >> 12;
}

void fx_clear(void)
{
    memset(s_p, 0, sizeof(s_p));
    s_live = 0;
}

void fx_clear_space(int space)
{
    int i;
    for (i = 0; i < MAX; i++)
        if (s_p[i].space == space && s_p[i].active) {
            s_p[i].active = 0;
            s_live--;
        }
}

void fx_shift(int space, float dx)
{
    int32_t d = FIX(dx, kx_of(space));
    int i;
    for (i = 0; i < MAX; i++) {
        if (s_p[i].space != space) continue;
        if (s_p[i].active == 2) g_fx[i].x += d;
        else s_p[i].x += dx;
    }
}

/* a slot: round from the last, the first free one, or when all are in use
 * the next one round (the oldest) */
static int slot(int space)
{
    int i = s_next, k;
    if (s_live < MAX) {
        for (k = 0; k < MAX && s_p[i].active; k++) i = i + 1 < MAX ? i + 1 : 0;
        s_live++;
    }
    s_next = i + 1 < MAX ? i + 1 : 0;
    memset(&s_p[i], 0, sizeof(Particle));
    s_p[i].active = 1;
    s_p[i].space = (uint8_t)space;
    return i;
}

Particle *fx_spawn(int space)
{
    return &s_p[slot(space)];
}

/* a particle spawned by the core in floats, in fixed point from now on */
static void take_over(Particle *p, FxFix *f)
{
    float kx = kx_of(p->space), ky = ky_of(p->space);
    float k = 1.0f - p->drag * TICK_DT, life = p->max_life * (TICK_HZ * 256.0f);
    f->x = FIX(p->x, kx);
    f->y = FIX(p->y, ky);
    f->vx = FIX(p->vx, kx * TICK_DT);
    f->vy = FIX(p->vy, ky * TICK_DT);
    f->gy = FIX(-p->grav, ky * TICK_DT * TICK_DT);
    f->k = p->drag > 0.0f ? FIX(k < 0.0f ? 0.0f : k, 1.0f) : 65536;
    f->life = (int32_t)(p->life * (TICK_HZ * 256.0f));
    f->inv = life > 0.0f ? (int32_t)((65536.0f * 256.0f) / life) : 0;
    f->s0 = (int32_t)(p->size * kx * 256.0f);
    f->s1 = (int32_t)(p->size_end * kx * 256.0f);
    p->active = 2;
}

const FxFix *fx_fix(int i)
{
    if (!s_p[i].active) return 0;
    if (s_p[i].active == 1) take_over(&s_p[i], &g_fx[i]);
    return &g_fx[i];
}

const Particle *fx_particles(int *count)
{
    *count = MAX;
    return s_p;
}

static inline int32_t mul16(int32_t v, int32_t k)
{
    return (int32_t)(((int64_t)v * k) >> 16);
}

/* fx.c's fx_update, a tick (dt is always TICK_DT) */
IWRAM_CODE void fx_update(float dt)
{
    int i;
    (void)dt;
    for (i = 0; i < MAX; i++) {
        Particle *p = &s_p[i];
        FxFix *f = &g_fx[i];
        if (!p->active) continue;
        if (p->active == 1) take_over(p, f);
        f->life -= 256;
        if (f->life <= 0) {
            p->active = 0;
            s_live--;
            continue;
        }
        f->vy += f->gy;
        if (f->k != 65536) {
            f->vx = mul16(f->vx, f->k);
            f->vy = mul16(f->vy, f->k);
        }
        f->x += f->vx;
        f->y += f->vy;
    }
}

/* fx.c's: n squares (or added light) flying out from (x, y) in all
 * directions at 0.35 to 1 of the speed, slowed by a drag of 1.5, living
 * 0.6 to 1 of life, 0.6 to 1.2 of size and shrinking to a fifth */
void fx_burst(int space, float x, float y, int n, float speed, float size, float life, Color c, int add)
{
    float kx = kx_of(space), ky = ky_of(space);
    int32_t x0 = FIX(x, kx), y0 = FIX(y, ky);
    int32_t sx = FIX(speed, kx * TICK_DT), sy = FIX(speed, ky * TICK_DT);
    int32_t life8 = (int32_t)(life * (TICK_HZ * 256.0f)), size8 = (int32_t)(size * kx * 256.0f);
    int32_t drag = FIX(1.0f - 1.5f * TICK_DT, 1.0f);
    int i;
    for (i = 0; i < n; i++) {
        int s = slot(space), a = (int)(rnd16() & 1023);
        Particle *p = &s_p[s];
        FxFix *f = &g_fx[s];
        int32_t k = 22938 + (int32_t)((42598 * rnd16()) >> 16); /* 0.35 + 0.65 r */
        int32_t sa = g_sin_tab[a], ca = g_sin_tab[(a + 256) & 1023];
        /* the speed times k (both 16.16), then times the sine (16384 = 1):
         * in 32 bits, for speeds under 16 pixels a tick */
        int32_t mx = ((sx >> 4) * (k >> 4)) >> 8, my = ((sy >> 4) * (k >> 4)) >> 8;
        f->x = x0;
        f->y = y0;
        f->vx = ((mx >> 3) * ca) >> 11;
        f->vy = ((my >> 3) * sa) >> 11;
        f->gy = 0;
        f->k = drag;
        f->life = vary(life8, 39322, 26214);   /* 0.6 + 0.4 r */
        f->inv = (65536 * 256) / (f->life > 0 ? f->life : 1);
        f->s0 = vary(size8, 39322, 39322);     /* 0.6 + 0.6 r */
        f->s1 = f->s0 / 5;
        p->c = c;
        p->add = (uint8_t)add;
        p->active = 2;
    }
}

/* fx.c's: a ring growing from r0 to r1 over its life, added light */
void fx_ring(int space, float x, float y, float r0, float r1, float life, Color c)
{
    float kx = kx_of(space), ky = ky_of(space);
    int s = slot(space);
    Particle *p = &s_p[s];
    FxFix *f = &g_fx[s];
    f->x = FIX(x, kx);
    f->y = FIX(y, ky);
    f->vx = f->vy = f->gy = 0;
    f->k = 65536;
    f->life = (int32_t)(life * (TICK_HZ * 256.0f));
    f->inv = (65536 * 256) / (f->life > 0 ? f->life : 1);
    f->s0 = (int32_t)(r0 * kx * 256.0f);
    f->s1 = (int32_t)(r1 * kx * 256.0f);
    p->shape = FX_RING;
    p->c = c;
    p->add = 1;
    p->active = 2;
}
