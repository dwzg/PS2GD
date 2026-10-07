#include "fx.h"

/* A platform's target.h may define FX_TARGET and implement fx.h itself
 * (the GBA, src/gba/fx_gba.c: in fixed point, as floats are calls of 60
 * to 100 cycles there); this is the one the others use. */
#ifndef FX_TARGET

#define MAX_PARTICLES 700

static Particle s_p[MAX_PARTICLES];
static int s_next;
static uint32_t s_seed = 12345;

static float frand(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (s_seed >> 8) / 16777216.0f;
}

void fx_clear(void)
{
    memset(s_p, 0, sizeof(s_p));
}

void fx_clear_space(int space)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (s_p[i].space == space) s_p[i].active = 0;
}

void fx_shift(int space, float dx)
{
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (s_p[i].space == space) s_p[i].x += dx;
}

Particle *fx_spawn(int space)
{
    /* Round-robin: when full, the oldest slot gets reused. */
    for (int k = 0; k < MAX_PARTICLES; k++) {
        int i = (s_next + k) % MAX_PARTICLES;
        if (!s_p[i].active) {
            s_next = (i + 1) % MAX_PARTICLES;
            memset(&s_p[i], 0, sizeof(Particle));
            s_p[i].active = 1;
            s_p[i].space = (uint8_t)space;
            return &s_p[i];
        }
    }
    Particle *p = &s_p[s_next];
    s_next = (s_next + 1) % MAX_PARTICLES;
    memset(p, 0, sizeof(Particle));
    p->active = 1;
    p->space = (uint8_t)space;
    return p;
}

void fx_update(float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_p[i];
        if (!p->active) continue;
        p->life -= dt;
        if (p->life <= 0.0f) {
            p->active = 0;
            continue;
        }
        p->vy -= p->grav * dt;
        if (p->drag > 0.0f) {
            float k = 1.0f - p->drag * dt;
            if (k < 0.0f) k = 0.0f;
            p->vx *= k;
            p->vy *= k;
        }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->rot += p->vrot * dt;
    }
}

const Particle *fx_particles(int *count)
{
    *count = MAX_PARTICLES;
    return s_p;
}

void fx_burst(int space, float x, float y, int n, float speed, float size, float life, Color c, int add)
{
    for (int i = 0; i < n; i++) {
        Particle *p = fx_spawn(space);
        float a = frand() * 2.0f * PI;
        float s = speed * (0.35f + 0.65f * frand());
        p->x = x;
        p->y = y;
        p->vx = cosf(a) * s;
        p->vy = sinf(a) * s;
        p->life = p->max_life = life * (0.6f + 0.4f * frand());
        p->size = size * (0.6f + 0.6f * frand());
        p->size_end = p->size * 0.2f;
        p->c = c;
        p->add = (uint8_t)add;
        p->drag = 1.5f;
        p->rot = frand() * PI;
        p->vrot = (frand() - 0.5f) * 10.0f;
    }
}

void fx_ring(int space, float x, float y, float r0, float r1, float life, Color c)
{
    Particle *p = fx_spawn(space);
    p->x = x;
    p->y = y;
    p->shape = FX_RING;
    p->size = r0;
    p->size_end = r1;
    p->life = p->max_life = life;
    p->c = c;
    p->add = 1;
}

#endif /* FX_TARGET */
