#include "draw.h"

#define CIRCLE_SEGS 48

static float s_cos[CIRCLE_SEGS + 1];
static float s_sin[CIRCLE_SEGS + 1];

void draw_init(void)
{
    for (int i = 0; i <= CIRCLE_SEGS; i++) {
        float a = (float)i / CIRCLE_SEGS * 2.0f * PI;
        s_cos[i] = cosf(a);
        s_sin[i] = sinf(a);
    }
}

static float s_grid = PIXEL_GRID, s_grid_y = PIXEL_GRID;

void draw_set_pixel_grid(float g)
{
    s_grid = s_grid_y = g;
}

void draw_set_pixel_grid_y(float g)
{
    s_grid_y = g;
}

float draw_pixel_grid(void)
{
    return s_grid;
}

float draw_pixel_grid_y(void)
{
    return s_grid_y;
}

static float stroke(float w, float grid)
{
    if (grid <= 0.0f) return w;
    float n = floorf(w * grid + 0.5f);
    return (n < 1.0f ? 1.0f : n) / grid;
}

float grid_w(float w)
{
    return stroke(w, s_grid);
}

float grid_h(float h)
{
    return stroke(h, s_grid_y);
}

/* A sixty-fourth of a pixel past the edge: rounding errors stay on that side
 * of it, so renderers that truncate positions (SDL's software one, the
 * PS2's gsKit to a sixteenth of a pixel) or round them to a sixteenth (the
 * PSP's) put the edge right on it. */
static float snap(float v, float grid)
{
    if (grid <= 0.0f) return v;
    return (floorf(v * grid + 0.5f) + 1.0f / 64.0f) / grid;
}

float grid_snap(float v)
{
    return snap(v, s_grid);
}

float grid_snap_y(float v)
{
    return snap(v, s_grid_y);
}

/* Pick a step through the circle table based on on-screen radius. */
static int seg_step(float r)
{
    if (r < 6.0f) return 6;  /* 8 segments */
    if (r < 14.0f) return 4; /* 12 */
    if (r < 40.0f) return 2; /* 24 */
    return 1;                /* 48 */
}

void draw_line2(float x0, float y0, float x1, float y1, float w, Color c0, Color c1)
{
    float dx = x1 - x0, dy = y1 - y0;
    /* on the grid of the way it is thick, mostly: down for a flat line */
    w = fabsf(dx) >= fabsf(dy) ? grid_h(w) : grid_w(w);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    Color c[4] = {c0, c1, c1, c0};
    gfx_quad(xy, c);
}

void draw_line(float x0, float y0, float x1, float y1, float w, Color c)
{
    draw_line2(x0, y0, x1, y1, w, c, c);
}

void draw_rect_outline(float x0, float y0, float x1, float y1, float w, Color c)
{
    float h = grid_h(w);
    w = grid_w(w);
    gfx_rect(x0, y0, x1, y0 + h, c);
    gfx_rect(x0, y1 - h, x1, y1, c);
    gfx_rect(x0, y0 + h, x0 + w, y1 - h, c);
    gfx_rect(x1 - w, y0 + h, x1, y1 - h, c);
}

void draw_circle_grad(float cx, float cy, float r, Color inner, Color outer)
{
    int st = seg_step(r);
    for (int i = 0; i < CIRCLE_SEGS; i += st) {
        int j = i + st;
        gfx_tri(cx, cy, inner,
                cx + s_cos[i] * r, cy + s_sin[i] * r, outer,
                cx + s_cos[j] * r, cy + s_sin[j] * r, outer);
    }
}

void draw_circle(float cx, float cy, float r, Color c)
{
    draw_circle_grad(cx, cy, r, c, c);
}

void draw_ellipse(float cx, float cy, float rx, float ry, Color c)
{
    int st = seg_step(maxf(rx, ry));
    for (int i = 0; i < CIRCLE_SEGS; i += st) {
        int j = i + st;
        gfx_tri(cx, cy, c,
                cx + s_cos[i] * rx, cy + s_sin[i] * ry, c,
                cx + s_cos[j] * rx, cy + s_sin[j] * ry, c);
    }
}

void draw_ring_grad(float cx, float cy, float r_in, float r_out, Color c_in, Color c_out)
{
    int st = seg_step(r_out);
    for (int i = 0; i < CIRCLE_SEGS; i += st) {
        int j = i + st;
        float xy[8] = {
            cx + s_cos[i] * r_in, cy + s_sin[i] * r_in,
            cx + s_cos[i] * r_out, cy + s_sin[i] * r_out,
            cx + s_cos[j] * r_out, cy + s_sin[j] * r_out,
            cx + s_cos[j] * r_in, cy + s_sin[j] * r_in,
        };
        Color c[4] = {c_in, c_out, c_out, c_in};
        gfx_quad(xy, c);
    }
}

void draw_ring(float cx, float cy, float r_in, float r_out, Color c)
{
    draw_ring_grad(cx, cy, r_in, r_out, c, c);
}

void draw_arc(float cx, float cy, float r_in, float r_out, float a0, float a1, Color c)
{
    int n = (int)(fabsf(a1 - a0) / (2.0f * PI) * CIRCLE_SEGS / seg_step(r_out)) + 1;
    float step = (a1 - a0) / n;
    for (int i = 0; i < n; i++) {
        float t0 = a0 + step * i, t1 = t0 + step;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        float xy[8] = {
            cx + c0 * r_in, cy + s0 * r_in, cx + c0 * r_out, cy + s0 * r_out,
            cx + c1 * r_out, cy + s1 * r_out, cx + c1 * r_in, cy + s1 * r_in,
        };
        Color cc[4] = {c, c, c, c};
        gfx_quad(xy, cc);
    }
}

void draw_ellipse_ring(float cx, float cy, float rx, float ry, float thick, Color c,
                       float a0, float a1)
{
    int n = (int)(fabsf(a1 - a0) / (2.0f * PI) * 24.0f) + 1;
    float step = (a1 - a0) / n;
    for (int i = 0; i < n; i++) {
        float t0 = a0 + step * i, t1 = t0 + step;
        float c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
        float xy[8] = {
            cx + c0 * (rx - thick), cy + s0 * (ry - thick),
            cx + c0 * rx, cy + s0 * ry,
            cx + c1 * rx, cy + s1 * ry,
            cx + c1 * (rx - thick), cy + s1 * (ry - thick),
        };
        Color cc[4] = {c, c, c, c};
        gfx_quad(xy, cc);
    }
}

void draw_glow(float cx, float cy, float r, Color c)
{
    gfx_blend(BLEND_ADD);
#ifdef GFX_GLOW
    if (!gfx_glow(cx, cy, r, c))
#endif
        draw_circle_grad(cx, cy, r, c, c & 0x00FFFFFFu);
    gfx_blend(BLEND_ALPHA);
}

void draw_rot_rect(float cx, float cy, float w, float h, float angle, Color c)
{
    float co = cosf(angle), si = sinf(angle);
    float hx = w * 0.5f, hy = h * 0.5f;
    float pts[8] = {-hx, -hy, hx, -hy, hx, hy, -hx, hy};
    for (int i = 0; i < 4; i++) {
        rot2(&pts[i * 2], &pts[i * 2 + 1], co, si);
        pts[i * 2] += cx;
        pts[i * 2 + 1] += cy;
    }
    Color cc[4] = {c, c, c, c};
    gfx_quad(pts, cc);
}

void draw_rot_rect_outline(float cx, float cy, float w, float h, float angle, float t, Color c)
{
    float co = cosf(angle), si = sinf(angle);
    float hx = w * 0.5f, hy = h * 0.5f;
    float pts[8] = {-hx, -hy, hx, -hy, hx, hy, -hx, hy};
    for (int i = 0; i < 4; i++) {
        rot2(&pts[i * 2], &pts[i * 2 + 1], co, si);
        pts[i * 2] += cx;
        pts[i * 2 + 1] += cy;
    }
    draw_poly_outline(pts, 4, t, c);
}

void draw_poly(const float *xy, int n, Color c)
{
    for (int i = 1; i + 1 < n; i++)
        gfx_tri(xy[0], xy[1], c, xy[i * 2], xy[i * 2 + 1], c, xy[i * 2 + 2], xy[i * 2 + 3], c);
}

/* Outline drawn inside-ish: each edge is a thick line, corners patched with squares. */
void draw_poly_outline(const float *xy, int n, float w, Color c)
{
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        draw_line(xy[i * 2], xy[i * 2 + 1], xy[j * 2], xy[j * 2 + 1], w, c);
    }
    for (int i = 0; i < n; i++)
        gfx_rect(xy[i * 2] - w * 0.5f, xy[i * 2 + 1] - w * 0.5f,
                 xy[i * 2] + w * 0.5f, xy[i * 2 + 1] + w * 0.5f, c);
}
