/* The vector family's drawing of the particles (fx.c). */
#include "fx.h"
#include "draw.h"

void fx_draw(int space, float cam_x, float cam_y, float back)
{
    int count;
    const Particle *all = fx_particles(&count);
    int blend = -1;
    for (int i = 0; i < count; i++) {
        const Particle *p = &all[i];
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
