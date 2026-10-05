#include "icons.h"
#include "draw.h"
#include "level.h"
#include "theme.h"

/*
 * Local -> screen transform for icon parts (local units: 1 = one block).
 *
 * On a pixel grid (draw.h) an icon sits on a device pixel, and when it is
 * square to the screen (or within a few degrees of it, landing) its parts'
 * edges are rounded to whole pixels the same way on both sides of the
 * centre: the borders of nested squares come out even, instead of 1 pixel
 * on one side and 2 on the other.
 */
typedef struct {
    float cx, cy, s, co, si, fy;
    float grid, grid_y; /* > 0: round offsets to this many device pixels per virtual one */
} Xf;

static Xf xf_make(float cx, float cy, float s, float angle, int flip)
{
    Xf t = {cx, cy, s, cosf(angle), sinf(angle), flip ? -1.0f : 1.0f, 0.0f, 0.0f};
    float g = draw_pixel_grid();
    if (g > 0.0f) {
        t.cx = grid_snap(cx);
        t.cy = grid_snap_y(cy);
        float q = floorf(angle / (PI * 0.5f) + 0.5f); /* nearest quarter turn */
        if (fabsf(angle - q * (PI * 0.5f)) < 0.06f) {
            int k = ((int)q % 4 + 4) % 4;
            t.co = k == 0 ? 1.0f : (k == 2 ? -1.0f : 0.0f);
            t.si = k == 1 ? 1.0f : (k == 3 ? -1.0f : 0.0f);
            t.grid = g;
            t.grid_y = draw_pixel_grid_y();
        }
    }
    return t;
}

static void xf_pt(const Xf *t, float lx, float ly, float *ox, float *oy)
{
    ly *= t->fy;
    float dx = (lx * t->co - ly * t->si) * t->s, dy = (lx * t->si + ly * t->co) * t->s;
    if (t->grid > 0.0f) {
        dx = roundf(dx * t->grid) / t->grid;
        dy = roundf(dy * t->grid_y) / t->grid_y;
    }
    *ox = t->cx + dx;
    *oy = t->cy + dy;
}

static void lrect(const Xf *t, float x0, float y0, float x1, float y1, Color c)
{
    float p[8];
    xf_pt(t, x0, y0, &p[0], &p[1]);
    xf_pt(t, x1, y0, &p[2], &p[3]);
    xf_pt(t, x1, y1, &p[4], &p[5]);
    xf_pt(t, x0, y1, &p[6], &p[7]);
    Color cc[4] = {c, c, c, c};
    gfx_quad(p, cc);
}

static void lpoly(const Xf *t, const float *pts, int n, Color c)
{
    float p[32];
    for (int i = 0; i < n && i < 16; i++) xf_pt(t, pts[i * 2], pts[i * 2 + 1], &p[i * 2], &p[i * 2 + 1]);
    draw_poly(p, n, c);
}

static void ldiamond(const Xf *t, float r, Color c)
{
    float pts[8] = {0, -r, r, 0, 0, r, -r, 0};
    lpoly(t, pts, 4, c);
}

static void lellipse(const Xf *t, float ox, float oy, float rx, float ry, Color c, int half)
{
    const int n = 16;
    float cx, cy;
    xf_pt(t, ox, oy, &cx, &cy);
    int steps = half ? n / 2 : n;
    float a0 = half ? PI : 0.0f;
    for (int i = 0; i < steps; i++) {
        float t0 = a0 + (float)i / n * 2.0f * PI, t1 = a0 + (float)(i + 1) / n * 2.0f * PI;
        float x0, y0, x1, y1;
        xf_pt(t, ox + cosf(t0) * rx, oy + sinf(t0) * ry, &x0, &y0);
        xf_pt(t, ox + cosf(t1) * rx, oy + sinf(t1) * ry, &x1, &y1);
        gfx_tri(cx, cy, c, x0, y0, c, x1, y1, c);
    }
}

void icon_draw_cube(float cx, float cy, float size, float angle, int icon, Color c1, Color c2)
{
    Xf t = xf_make(cx, cy, size, angle, 0);
    const Color k = RGB(8, 8, 12);
    lrect(&t, -0.5f, -0.5f, 0.5f, 0.5f, k);
    lrect(&t, -0.43f, -0.43f, 0.43f, 0.43f, c1);
    /* subtle top highlight */
    lrect(&t, -0.43f, -0.43f, 0.43f, -0.33f, col_scale(c1, 1.25f));

    switch (icon % ICON_COUNT) {
    case 0: /* CORE */
        lrect(&t, -0.29f, -0.29f, 0.29f, 0.29f, k);
        lrect(&t, -0.23f, -0.23f, 0.23f, 0.23f, c2);
        lrect(&t, -0.11f, -0.11f, 0.11f, 0.11f, c1);
        break;
    case 1: /* VISOR */
        lrect(&t, -0.37f, -0.24f, 0.37f, 0.04f, k);
        lrect(&t, -0.31f, -0.18f, 0.31f, -0.02f, c2);
        lrect(&t, -0.22f, 0.16f, 0.22f, 0.27f, k);
        lrect(&t, -0.17f, 0.19f, 0.17f, 0.24f, c2);
        break;
    case 2: /* BUDDY */
        lrect(&t, -0.30f, -0.25f, -0.06f, -0.01f, k);
        lrect(&t, 0.06f, -0.25f, 0.30f, -0.01f, k);
        lrect(&t, -0.24f, -0.19f, -0.12f, -0.07f, c2);
        lrect(&t, 0.12f, -0.19f, 0.24f, -0.07f, c2);
        lrect(&t, -0.24f, 0.12f, 0.24f, 0.22f, k);
        lrect(&t, -0.18f, 0.14f, 0.18f, 0.20f, c2);
        break;
    case 3: { /* SPLIT */
        float a[6] = {0.43f, -0.43f, 0.43f, 0.43f, -0.43f, 0.43f};
        lpoly(&t, a, 3, c2);
        float b[8] = {0.43f, -0.43f, 0.43f, -0.33f, -0.33f, 0.43f, -0.43f, 0.43f};
        lpoly(&t, b, 4, k);
        lrect(&t, 0.12f, 0.12f, 0.28f, 0.28f, k);
        break;
    }
    case 4: /* TARGET */
        lrect(&t, -0.33f, -0.33f, 0.33f, 0.33f, k);
        lrect(&t, -0.27f, -0.27f, 0.27f, 0.27f, c2);
        lrect(&t, -0.17f, -0.17f, 0.17f, 0.17f, k);
        lrect(&t, -0.10f, -0.10f, 0.10f, 0.10f, c1);
        break;
    case 5: /* PLUS */
        lrect(&t, -0.35f, -0.12f, 0.35f, 0.12f, k);
        lrect(&t, -0.12f, -0.35f, 0.12f, 0.35f, k);
        lrect(&t, -0.29f, -0.06f, 0.29f, 0.06f, c2);
        lrect(&t, -0.06f, -0.29f, 0.06f, 0.29f, c2);
        break;
    case 6: /* STRIPE */
        for (int i = -1; i <= 1; i++) {
            float x = i * 0.25f;
            lrect(&t, x - 0.09f, -0.33f, x + 0.09f, 0.33f, k);
            lrect(&t, x - 0.04f, -0.28f, x + 0.04f, 0.28f, c2);
        }
        break;
    default: /* GEM */
        ldiamond(&t, 0.38f, k);
        ldiamond(&t, 0.31f, c2);
        ldiamond(&t, 0.13f, c1);
        break;
    }
}

void icon_draw_ship(float cx, float cy, float size, float angle, int flip, int icon, Color c1, Color c2)
{
    Xf t = xf_make(cx, cy, size, angle, flip);
    const Color k = RGB(8, 8, 12);
    /* mini cube rider first so the hull overlaps its lower half */
    {
        float rx, ry;
        xf_pt(&t, -0.08f, -0.3f, &rx, &ry);
        icon_draw_cube(rx, ry, size * 0.46f, angle, icon, c1, c2);
    }
    float fin[6] = {-0.50f, -0.05f, -0.78f, -0.42f, -0.30f, -0.05f};
    lpoly(&t, fin, 3, k);
    float fin2[6] = {-0.50f, -0.09f, -0.70f, -0.34f, -0.38f, -0.09f};
    lpoly(&t, fin2, 3, c2);
    float hull_o[12] = {-0.70f, 0.02f, -0.56f, -0.14f, 0.30f, -0.14f, 0.76f, 0.10f, 0.50f, 0.34f, -0.62f, 0.34f};
    lpoly(&t, hull_o, 6, k);
    float hull[12] = {-0.62f, 0.04f, -0.52f, -0.08f, 0.28f, -0.08f, 0.64f, 0.10f, 0.46f, 0.27f, -0.56f, 0.27f};
    lpoly(&t, hull, 6, c1);
    float stripe[8] = {-0.50f, 0.10f, 0.50f, 0.10f, 0.42f, 0.19f, -0.50f, 0.19f};
    lpoly(&t, stripe, 4, c2);
}

void icon_draw_ball(float cx, float cy, float size, float angle, int icon, Color c1, Color c2)
{
    const Color k = RGB(8, 8, 12);
    cx = grid_snap(cx); /* the circles below sit on a device pixel too */
    cy = grid_snap_y(cy);
    Xf t = xf_make(cx, cy, size, angle, 0);
    draw_circle(cx, cy, size * 0.49f, k);
    draw_circle(cx, cy, size * 0.42f, c1);
    draw_ring(cx, cy, size * 0.22f, size * 0.32f, k);
    draw_ring(cx, cy, size * 0.25f, size * 0.29f, c2);
    lrect(&t, -0.40f, -0.07f, 0.40f, 0.07f, k);
    lrect(&t, -0.07f, -0.40f, 0.07f, 0.40f, k);
    lrect(&t, -0.36f, -0.03f, 0.36f, 0.03f, c2);
    lrect(&t, -0.03f, -0.36f, 0.03f, 0.36f, c2);
    if (icon & 1) ldiamond(&t, 0.12f, c1);
    else draw_circle(cx, cy, size * 0.1f, c1);
}

void icon_draw_ufo(float cx, float cy, float size, float angle, int flip, int icon, Color c1, Color c2)
{
    Xf t = xf_make(cx, cy, size, angle, flip);
    const Color k = RGB(8, 8, 12);
    float rx, ry;
    xf_pt(&t, 0.0f, -0.16f, &rx, &ry);
    icon_draw_cube(rx, ry, size * 0.40f, angle, icon, c1, c2);
    /* glass dome */
    lellipse(&t, 0.0f, -0.02f, 0.34f, 0.40f, col_with_alpha(RGB(200, 240, 255), 0.35f), 1);
    lellipse(&t, 0.0f, 0.12f, 0.62f, 0.22f, k, 0);
    lellipse(&t, 0.0f, 0.12f, 0.55f, 0.16f, c1, 0);
    lrect(&t, -0.50f, 0.07f, 0.50f, 0.12f, col_scale(c1, 1.25f));
    lellipse(&t, 0.0f, 0.27f, 0.26f, 0.09f, k, 0);
    lellipse(&t, 0.0f, 0.27f, 0.20f, 0.06f, c2, 0);
}

void icon_draw_wave(float cx, float cy, float size, float angle, Color c1, Color c2)
{
    Xf t = xf_make(cx, cy, size, angle, 0);
    const Color k = RGB(8, 8, 12);
    float o[6] = {0.48f, 0.0f, -0.36f, -0.36f, -0.36f, 0.36f};
    lpoly(&t, o, 3, k);
    float a[6] = {0.36f, 0.0f, -0.29f, -0.27f, -0.29f, 0.27f};
    lpoly(&t, a, 3, c1);
    float b[6] = {0.12f, 0.0f, -0.20f, -0.13f, -0.20f, 0.13f};
    lpoly(&t, b, 3, c2);
}

void icon_draw_mode(int mode, float cx, float cy, float size, float angle, int flip, int icon,
                    Color c1, Color c2)
{
    switch (mode) {
    case MODE_SHIP: icon_draw_ship(cx, cy, size, angle, flip, icon, c1, c2); break;
    case MODE_BALL: icon_draw_ball(cx, cy, size, angle, icon, c1, c2); break;
    case MODE_UFO: icon_draw_ufo(cx, cy, size, angle, flip, icon, c1, c2); break;
    case MODE_WAVE: icon_draw_wave(cx, cy, size, angle, c1, c2); break;
    default: icon_draw_cube(cx, cy, size, angle, icon, c1, c2); break;
    }
}
