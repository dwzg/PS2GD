#include "fx.h"
#include "draw.h"

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

void fx_draw(int space, float cam_x, float cam_y, float back)
{
    int blend = -1;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &s_p[i];
        if (!p->active || p->space != space) continue;
        float t = 1.0f - p->life / p->max_life;
        float size = lerpf(p->size, p->size_end, t);
        float px = p->x - p->vx * back, py = p->y - p->vy * back;
        float sx, sy;
        if (space == FX_WORLD) {
            sx = (px - cam_x) * BLOCK_PX;
            sy = SCREEN_H - (py - cam_y) * BLOCK_PX;
            size *= BLOCK_PX;
        } else {
            sx = px;
            sy = py;
        }
        if (sx < -60 || sx > SCREEN_W + 60 || sy < -60 || sy > SCREEN_H + 60) continue;
        if (blend != p->add) {
            blend = p->add;
            gfx_blend(blend ? BLEND_ADD : BLEND_ALPHA);
        }
        Color c = col_with_alpha(p->c, 1.0f - t * t);
        switch (p->shape) {
        case FX_CIRCLE: draw_circle(sx, sy, size * 0.5f, c); break;
        case FX_RING: {
            float th = maxf(2.0f, size * 0.06f * (1.0f - t) + 1.5f);
            draw_ring(sx, sy, maxf(0.0f, size - th), size, c);
            break;
        }
        default:
            if (p->rot != 0.0f || p->vrot != 0.0f) draw_rot_rect(sx, sy, size, size, p->rot, c);
            else gfx_rect(sx - size * 0.5f, sy - size * 0.5f, sx + size * 0.5f, sy + size * 0.5f, c);
            break;
        }
    }
    gfx_blend(BLEND_ALPHA);
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
