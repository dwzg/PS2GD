/* gfx.h backend for the PC build: batches triangles into SDL_RenderGeometry. */
#include <SDL.h>
#include <string.h>

#include "gfx_sdl.h"
#include "../core/gfx.h"

#define MAX_VERTS 98304

static SDL_Renderer *s_r;
static float s_sx = 1.0f, s_sy = 1.0f;
static float s_ox, s_oy;
static SDL_Vertex s_v[MAX_VERTS];
static int s_n;
static int s_blend = BLEND_ALPHA;
static GfxStats s_stats;

void gfx_sdl_begin(SDL_Renderer *r, float scale_x, float scale_y)
{
    s_r = r;
    s_sx = scale_x;
    s_sy = scale_y;
    s_ox = s_oy = 0.0f;
    s_n = 0;
    s_blend = BLEND_ALPHA;
    memset(&s_stats, 0, sizeof(s_stats));
    if (r) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void gfx_sdl_offset(float dx, float dy)
{
    s_ox = dx;
    s_oy = dy;
}

const GfxStats *gfx_sdl_stats(void)
{
    return &s_stats;
}

void gfx_sdl_flush(void)
{
    if (s_n > 0 && s_r) {
        SDL_SetRenderDrawBlendMode(s_r, s_blend == BLEND_ADD ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        SDL_RenderGeometry(s_r, NULL, s_v, s_n, NULL, 0);
    }
    s_n = 0;
}

static inline void put(float x, float y, Color c)
{
    SDL_Vertex *v = &s_v[s_n++];
    v->position.x = (x + s_ox) * s_sx;
    v->position.y = (y + s_oy) * s_sy;
    v->color.r = (Uint8)COL_R(c);
    v->color.g = (Uint8)COL_G(c);
    v->color.b = (Uint8)COL_B(c);
    v->color.a = (Uint8)COL_A(c);
    v->tex_coord.x = 0.0f;
    v->tex_coord.y = 0.0f;
}

static inline void reserve(int n)
{
    if (s_n + n > MAX_VERTS) gfx_sdl_flush();
}

void gfx_blend(int mode)
{
    if (mode == s_blend) return;
    gfx_sdl_flush();
    s_blend = mode;
    s_stats.blend_switches++;
}

void gfx_tri(float x0, float y0, Color c0, float x1, float y1, Color c1, float x2, float y2, Color c2)
{
    s_stats.tris++;
    reserve(3);
    put(x0, y0, c0);
    put(x1, y1, c1);
    put(x2, y2, c2);
}

static void quad_impl(const float *xy, const Color *c)
{
    reserve(6);
    put(xy[0], xy[1], c[0]);
    put(xy[2], xy[3], c[1]);
    put(xy[4], xy[5], c[2]);
    put(xy[0], xy[1], c[0]);
    put(xy[4], xy[5], c[2]);
    put(xy[6], xy[7], c[3]);
}

void gfx_quad(const float *xy, const Color *c)
{
    s_stats.quads++;
    quad_impl(xy, c);
}

static void quad4(float x0, float y0, float x1, float y1, Color tl, Color tr, Color br, Color bl)
{
    float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    Color c[4] = {tl, tr, br, bl};
    quad_impl(xy, c);
}

void gfx_rect(float x0, float y0, float x1, float y1, Color c)
{
    s_stats.rects++;
    quad4(x0, y0, x1, y1, c, c, c, c);
}

void gfx_rect_v(float x0, float y0, float x1, float y1, Color top, Color bottom)
{
    s_stats.quads++;
    quad4(x0, y0, x1, y1, top, top, bottom, bottom);
}

void gfx_rect_h(float x0, float y0, float x1, float y1, Color left, Color right)
{
    s_stats.quads++;
    quad4(x0, y0, x1, y1, left, right, right, left);
}
