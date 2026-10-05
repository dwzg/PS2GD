#include "render.h"
#include "draw.h"
#include "icons.h"

#define B BLOCK_PX

/* ------------------------------------------------------------------ */
/* Background                                                          */
/* ------------------------------------------------------------------ */

static void bg_layer(const View *v, float parallax, float tile_w, float min_s, float max_s,
                     float alpha, uint32_t seed)
{
    const Color base = v->pal->accent;
    float px = v->cam_x * parallax;
    float py = v->cam_y * parallax * 0.6f;
    int first = (int)floorf(px / tile_w) - 1;
    int last = first + (int)(SCREEN_W / (tile_w * B)) + 3;
    for (int i = first; i <= last; i++) {
        uint32_t h = hash_u32((uint32_t)i * 2654435761u ^ seed);
        float size = min_s + (max_s - min_s) * hash_f01(h);
        float ox = hash_f01(h >> 3) * (tile_w - size * 0.5f);
        float oy = 1.5f + hash_f01(h >> 7) * 9.0f;
        float sx = (i * tile_w + ox - px) * B;
        float sy = SCREEN_H - (oy - py) * B;
        float s = size * B;
        float a = alpha * (0.6f + 0.4f * hash_f01(h >> 11));
        /* faint and thin-lined, so they never pass for blocks */
        gfx_rect(sx, sy - s, sx + s, sy, col_with_alpha(base, a * 0.55f));
        draw_rect_outline(sx, sy - s, sx + s, sy, 2.0f, col_with_alpha(base, a * (1.0f + v->pulse * 0.4f)));
        if (h & 1) {
            float inset = s * 0.25f;
            draw_rect_outline(sx + inset, sy - s + inset, sx + s - inset, sy - inset, 1.5f,
                              col_with_alpha(base, a * 0.8f));
        }
    }
}

void view_snap(View *v)
{
    /* view_sy(v, 0) = SCREEN_H + cam_y * B */
    v->cam_y = (grid_snap(SCREEN_H + v->cam_y * B) - SCREEN_H) / B;
}

void render_background(const View *v)
{
    gfx_rect_v(0, 0, SCREEN_W, SCREEN_H, v->pal->bg_top, v->pal->bg_bot);
    bg_layer(v, 0.12f, 9.0f, 3.0f, 6.0f, 0.07f, 0x51u);
    bg_layer(v, 0.30f, 6.0f, 1.5f, 3.0f, 0.09f, 0xA7u);
}

/* One horizontal band of "ground": surface at screen y gy, extending away
 * from the playfield (down when dir=+1, up when dir=-1). */
static void ground_band(const View *v, float gy, int dir, float alpha)
{
    const Palette *pal = v->pal;
    float edge = dir > 0 ? SCREEN_H : 0.0f;
    if ((dir > 0 && gy >= SCREEN_H) || (dir < 0 && gy <= 0)) return;
    Color g0 = col_with_alpha(pal->ground, alpha);
    Color g1 = col_with_alpha(col_scale(pal->ground, 0.55f), alpha);
    if (dir > 0) gfx_rect_v(0, gy, SCREEN_W, edge, g0, g1);
    else gfx_rect_v(0, edge, SCREEN_W, gy, g1, g0);

    /* tile separators every 4 blocks */
    float tile = 4.0f * B;
    float off = fmodf(v->cam_x * B, tile);
    Color sep = col_with_alpha(RGB(0, 0, 0), 0.22f * alpha);
    float sw = grid_w(3.0f);
    for (float x = -off; x < SCREEN_W; x += tile) {
        if (dir > 0) gfx_rect(x, gy, x + sw, edge, sep);
        else gfx_rect(x, edge, x + sw, gy, sep);
    }

    /* glowing surface line, fading out over the last `fade` pixels towards
     * each screen edge */
    const float fade = 170.0f;
    float lw = grid_h(3.0f);
    float y0 = dir > 0 ? gy : gy - lw;
    Color lc = col_with_alpha(pal->ground_line, alpha);
    Color lt = col_with_alpha(pal->ground_line, 0.0f);
    gfx_rect_h(0, y0, fade, y0 + lw, lt, lc);
    gfx_rect(fade, y0, SCREEN_W - fade, y0 + lw, lc);
    gfx_rect_h(SCREEN_W - fade, y0, SCREEN_W, y0 + lw, lc, lt);

    /* its glow over the playfield, gone 14 px from the line, fading out
     * towards the edges with it: towards an edge it gets fainter at every
     * height, so it thins out as well. The ends are drawn in slices, each
     * two triangles split the same way on every backend. */
    float glow = (0.25f + 0.35f * v->pulse) * alpha;
    gfx_blend(BLEND_ADD);
    Color gc = col_with_alpha(pal->ground_line, glow);
    Color gz = col_with_alpha(pal->ground_line, 0.0f);
    float ye = gy - dir * 14.0f; /* where the glow ends */
    if (dir > 0) gfx_rect_v(fade, ye, SCREEN_W - fade, gy, gz, gc);
    else gfx_rect_v(fade, gy, SCREEN_W - fade, ye, gc, gz);
    const int slices = 10;
    for (int side = 0; side < 2; side++) {
        float xo = side ? SCREEN_W : 0.0f, step = (side ? -fade : fade) / slices; /* from the outer edge in */
        for (int k = 0; k < slices; k++) {
            float xa = xo + step * k, xb = xa + step;
            Color ca = col_with_alpha(pal->ground_line, glow * k / slices);
            Color cb = col_with_alpha(pal->ground_line, glow * (k + 1) / slices);
            gfx_tri(xa, gy, ca, xb, gy, cb, xb, ye, gz);
            gfx_tri(xa, gy, ca, xb, ye, gz, xa, ye, gz);
        }
    }
    gfx_blend(BLEND_ALPHA);
}

void render_ground(const View *v, float corr_floor, float corr_ceil, float corr_alpha)
{
    ground_band(v, view_sy(v, 0.0f), 1, 1.0f);
    if (corr_alpha > 0.01f) {
        if (corr_floor > 0.01f) ground_band(v, view_sy(v, corr_floor), 1, corr_alpha);
        ground_band(v, view_sy(v, corr_ceil), -1, corr_alpha);
    }
}

/* ------------------------------------------------------------------ */
/* Objects                                                             */
/* ------------------------------------------------------------------ */

/* Soft glow around a spike in its outline colour (additive blending): it
 * lifts dark spikes off dark backgrounds, like the glow around blocks. */
static void spike_glow(float sx, float sy_base, float w, float h, int down, Color edge)
{
    float d = down ? 1.0f : -1.0f;
    float cx = sx + w * 0.5f, cy = sy_base + d * h * 0.33f; /* centroid */
    float g = 0.30f * w;                                     /* how far the glow reaches */
    float px[3] = {sx - g, sx + w + g, cx}, py[3] = {sy_base, sy_base, sy_base + d * (h + g)};
    Color c = col_with_alpha(edge, 0.30f), z = col_with_alpha(edge, 0.0f);
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        gfx_tri(cx, cy, c, px[i], py[i], z, px[j], py[j], z);
    }
}

/* Glow in the square beyond a block's corner (x, y), reaching gw out along
 * dx and dy (+-1): it fades out from the corner like the glow along the two
 * sides beside it (as strong as the weaker of the two fades), so the glow
 * goes round the corner. */
static void corner_glow(float x, float y, float dx, float dy, float gw, Color gc, Color gz)
{
    float ox = x + dx * gw, oy = y + dy * gw;
    gfx_tri(x, y, gc, ox, y, gz, ox, oy, gz);
    gfx_tri(x, y, gc, ox, oy, gz, x, oy, gz);
}

void render_spike(float sx, float sy_base, float w, float h, int down, Color fill, Color edge)
{
    float tipy = down ? sy_base + h : sy_base - h;
    /* dark body that lightens towards the tip, thick bright outline */
    Color top = col_lerp(col_scale(fill, 2.2f), edge, 0.16f);
    gfx_tri(sx, sy_base, fill, sx + w, sy_base, fill, sx + w * 0.5f, tipy, top);
    float lw = grid_w(3.0f), lh = grid_h(3.0f);
    draw_line(sx + 1, sy_base, sx + w * 0.5f, tipy, lw, edge);
    draw_line(sx + w - 1, sy_base, sx + w * 0.5f, tipy, lw, edge);
    gfx_rect(sx + 1, down ? sy_base : sy_base - lh, sx + w - 1, down ? sy_base + lh : sy_base, edge);
}

void render_saw(float cx, float cy, float r, float angle, Color fill, Color edge)
{
    const int teeth = 12;
    float pts[2 * 24 + 2];
    for (int i = 0; i <= teeth * 2; i++) {
        float a = angle + (float)i / (teeth * 2) * 2.0f * PI;
        float rr = (i & 1) ? r * 0.80f : r;
        pts[i * 2] = cx + cosf(a) * rr;
        pts[i * 2 + 1] = cy + sinf(a) * rr;
    }
    for (int i = 0; i < teeth * 2; i++)
        gfx_tri(cx, cy, col_scale(fill, 2.5f), pts[i * 2], pts[i * 2 + 1], fill, pts[i * 2 + 2],
                pts[i * 2 + 3], fill);
    for (int i = 0; i < teeth * 2; i++)
        draw_line(pts[i * 2], pts[i * 2 + 1], pts[i * 2 + 2], pts[i * 2 + 3], 2.0f, edge);
    draw_ring(cx, cy, r * 0.50f, r * 0.50f + 2.0f, edge);
    draw_circle(cx, cy, r * 0.18f, edge);
}

void render_orb(float cx, float cy, float r, Color c, float time, float pulse)
{
    float s = r * (1.0f + 0.12f * pulse);
    draw_glow(cx, cy, s * 2.6f, col_with_alpha(c, 0.45f));
    draw_circle(cx, cy, s * 1.32f, RGBA(0, 0, 0, 120));
    draw_ring(cx, cy, s * 1.02f, s * 1.30f, c);
    draw_circle_grad(cx, cy, s * 0.92f, col_lerp(c, COL_WHITE, 0.75f), c);
    /* rotating dashes */
    for (int i = 0; i < 4; i++) {
        float a = time * 2.0f + i * (PI * 0.5f);
        draw_arc(cx, cy, s * 1.55f, s * 1.70f, a, a + 0.75f, col_with_alpha(c, 0.8f));
    }
}

void render_portal(float cx, float cy, Color c, float time, int mode_icon, int grav_arrow)
{
    const float rx = 0.40f * B, ry = 1.45f * B;
    /* soft vertical glow */
    gfx_blend(BLEND_ADD);
    {
        const int n = 16;
        Color cc = col_with_alpha(c, 0.35f), cz = col_with_alpha(c, 0.0f);
        for (int i = 0; i < n; i++) {
            float a0 = (float)i / n * 2.0f * PI, a1 = (float)(i + 1) / n * 2.0f * PI;
            gfx_tri(cx, cy, cc, cx + cosf(a0) * rx * 2.4f, cy + sinf(a0) * ry * 1.25f, cz,
                    cx + cosf(a1) * rx * 2.4f, cy + sinf(a1) * ry * 1.25f, cz);
        }
    }
    gfx_blend(BLEND_ALPHA);
    draw_ellipse_ring(cx, cy, rx + 3, ry + 3, 10.0f, RGBA(0, 0, 0, 160), 0, 2.0f * PI);
    draw_ellipse_ring(cx, cy, rx, ry, 6.0f, c, 0, 2.0f * PI);
    draw_ellipse_ring(cx, cy, rx - 8, ry - 8, 2.0f, col_lerp(c, COL_WHITE, 0.6f), 0, 2.0f * PI);
    /* travelling highlight */
    float a = fmodf(time * 3.0f, 2.0f * PI);
    draw_ellipse_ring(cx, cy, rx, ry, 6.0f, col_lerp(c, COL_WHITE, 0.7f), a, a + 0.6f);

    if (mode_icon >= 0) {
        icon_draw_mode(mode_icon, cx, cy, B * 0.55f, 0.0f, 0, 0, col_with_alpha(c, 0.85f),
                       col_with_alpha(COL_WHITE, 0.85f));
    }
    if (grav_arrow) {
        float d = grav_arrow > 0 ? -1.0f : 1.0f; /* +1 = arrow up */
        float tip = cy + d * 0.45f * B, base = cy - d * 0.15f * B;
        gfx_tri(cx, tip, COL_WHITE, cx - 0.22f * B, base, COL_WHITE, cx + 0.22f * B, base, COL_WHITE);
        gfx_rect(cx - 0.07f * B, minf(base, cy - d * 0.45f * B), cx + 0.07f * B,
                 maxf(base, cy - d * 0.45f * B), COL_WHITE);
    }
}

static void render_speed_portal(float cx, float cy, int idx, float time)
{
    static const Color cols[4] = {RGB(255, 170, 40), RGB(60, 200, 255), RGB(80, 255, 120),
                                  RGB(255, 90, 220)};
    Color c = cols[idx & 3];
    int n = idx + 1;
    float h = 0.62f * B, w = 0.32f * B, gap = 0.26f * B;
    float x0 = cx - (n - 1) * gap * 0.5f - w * 0.5f;
    draw_glow(cx, cy, 1.4f * B, col_with_alpha(c, 0.35f + 0.1f * sinf(time * 6.0f)));
    for (int i = 0; i < n; i++) {
        float x = x0 + i * gap;
        draw_line(x - 2, cy - h - 2, x + w + 2, cy, 9.0f, RGBA(0, 0, 0, 170));
        draw_line(x - 2, cy + h + 2, x + w + 2, cy, 9.0f, RGBA(0, 0, 0, 170));
        draw_line(x, cy - h, x + w, cy, 6.0f, c);
        draw_line(x, cy + h, x + w, cy, 6.0f, c);
    }
}

static void render_pad(float sx, float sy_base, int ceiling, Color c, float pulse)
{
    float d = ceiling ? 1.0f : -1.0f; /* direction away from the surface on screen */
    float h = 0.20f * B;
    float xy[8] = {sx + 0.08f * B, sy_base, sx + 0.92f * B, sy_base,
                   sx + 0.78f * B, sy_base + d * h, sx + 0.22f * B, sy_base + d * h};
    Color cc[4] = {col_scale(c, 0.7f), col_scale(c, 0.7f), col_lerp(c, COL_WHITE, 0.4f),
                   col_lerp(c, COL_WHITE, 0.4f)};
    gfx_quad(xy, cc);
    gfx_blend(BLEND_ADD);
    float gh = (0.9f + 0.2f * pulse) * B;
    Color g0 = col_with_alpha(c, 0.45f), g1 = col_with_alpha(c, 0.0f);
    if (ceiling) gfx_rect_v(sx + 0.15f * B, sy_base + h, sx + 0.85f * B, sy_base + h + gh, g0, g1);
    else gfx_rect_v(sx + 0.15f * B, sy_base - h - gh, sx + 0.85f * B, sy_base - h, g1, g0);
    gfx_blend(BLEND_ALPHA);
}

void render_coin(float cx, float cy, float r, float spin, float alpha, int ghost)
{
    float sx = 0.15f + 0.85f * fabsf(cosf(spin));
    Color c = ghost ? col_with_alpha(RGB(230, 240, 255), 0.45f * alpha) : col_with_alpha(COL_COIN, alpha);
    if (!ghost) draw_glow(cx, cy, r * 2.4f, col_with_alpha(COL_COIN, 0.35f * alpha));
    draw_ellipse(cx, cy, r * sx + 2, r + 2, col_with_alpha(RGB(40, 20, 0), 0.8f * alpha));
    draw_ellipse(cx, cy, r * sx, r, c);
    draw_ellipse(cx, cy, r * sx * 0.68f, r * 0.68f, col_scale(c, 0.78f));
    if (sx > 0.4f) gfx_rect(cx - 2, cy - r * 0.35f, cx + 2, cy + r * 0.35f, col_scale(c, 1.2f));
}

void render_checkpoint(float cx, float cy)
{
    float d = 11.0f;
    float xy[8] = {cx, cy - d - 3, cx + d + 3, cy, cx, cy + d + 3, cx - d - 3, cy};
    draw_poly(xy, 4, RGB(0, 40, 10));
    float xy2[8] = {cx, cy - d, cx + d, cy, cx, cy + d, cx - d, cy};
    draw_poly(xy2, 4, RGB(90, 255, 120));
}

static Color orb_color(int t)
{
    switch (t) {
    case OBJ_ORB_PINK: case OBJ_PAD_PINK: return COL_ORB_PINK;
    case OBJ_ORB_BLUE: case OBJ_PAD_BLUE: return COL_ORB_BLUE;
    case OBJ_ORB_GREEN: return COL_ORB_GREEN;
    default: return COL_ORB_YELLOW;
    }
}

static int is_used(const Player *p, int id)
{
    return p && sim_used(p, id);
}

void render_level(const View *v, const Level *L, const Player *p, uint8_t saved_coins)
{
    const Palette *pal = v->pal;
    int c0 = clampi((int)floorf(v->cam_x) - 2, 0, L->width);
    int c1 = clampi((int)floorf(v->cam_x + SCREEN_W / B) + 2, 0, L->width);
    int r0 = clampi((int)floorf(v->cam_y) - 1, 0, L->height);
    int r1 = clampi((int)floorf(v->cam_y + SCREEN_H / B) + 1, 0, L->height);

    /* end gate glow */
    {
        float ex = view_sx(v, L->end_x);
        if (ex > -40 && ex < SCREEN_W + 120) {
            gfx_blend(BLEND_ADD);
            gfx_rect_h(ex - 3 * B, 0, ex, SCREEN_H, col_with_alpha(COL_WHITE, 0.0f),
                       col_with_alpha(COL_WHITE, 0.55f));
            gfx_rect_h(ex, 0, ex + 2 * B, SCREEN_H, col_with_alpha(COL_WHITE, 0.55f),
                       col_with_alpha(COL_WHITE, 0.0f));
            gfx_blend(BLEND_ALPHA);
            gfx_rect(ex - 2, 0, ex + 2, SCREEN_H, COL_WHITE);
        }
    }

    /* glow around blocks and hazards, behind everything else */
    const float gw = 8.0f;
    Color gc = col_with_alpha(pal->block_edge, 0.20f + 0.10f * v->pulse), gz = col_with_alpha(pal->block_edge, 0.0f);
    gfx_blend(BLEND_ADD);
    for (int cy = r0; cy < r1; cy++) {
        for (int cx = c0; cx < c1; cx++) {
            int t = L->grid[cy * L->width + cx];
            if (!t) continue;
            uint8_t e = L->edges[cy * L->width + cx];
            float x0 = view_sx(v, (float)cx), x1 = x0 + B;
            float y1 = view_sy(v, (float)cy), y0 = y1 - B;
            if (t == OBJ_SLAB_LO) y0 = y1 - B * 0.5f;
            else if (t == OBJ_SLAB_HI) y1 = y0 + B * 0.5f;
            if (e & EDGE_T) gfx_rect_v(x0, y0 - gw, x1, y0, gz, gc);
            if (e & EDGE_B) gfx_rect_v(x0, y1, x1, y1 + gw, gc, gz);
            if (e & EDGE_L) gfx_rect_h(x0 - gw, y0, x0, y1, gz, gc);
            if (e & EDGE_R) gfx_rect_h(x1, y0, x1 + gw, y1, gc, gz);
            if ((e & EDGE_T) && (e & EDGE_L)) corner_glow(x0, y0, -1, -1, gw, gc, gz);
            if ((e & EDGE_T) && (e & EDGE_R)) corner_glow(x1, y0, 1, -1, gw, gc, gz);
            if ((e & EDGE_B) && (e & EDGE_L)) corner_glow(x0, y1, -1, 1, gw, gc, gz);
            if ((e & EDGE_B) && (e & EDGE_R)) corner_glow(x1, y1, 1, 1, gw, gc, gz);
        }
    }
    {
        int g0 = clampi(c0 - 1, 0, L->width), g1 = clampi(c1 + 1, 0, L->width);
        for (int i = L->col_start[g0]; i < L->col_start[g1]; i++) {
            const LevelObj *o = &L->objs[i];
            float sx = view_sx(v, (float)o->cx), sb = view_sy(v, (float)o->cy);
            switch (o->type) {
            case OBJ_SPIKE_UP: spike_glow(sx, sb, B, B * 0.92f, 0, pal->block_edge); break;
            case OBJ_SPIKE_DOWN: spike_glow(sx, sb - B, B, B * 0.92f, 1, pal->block_edge); break;
            case OBJ_SPIKE_SM_UP: spike_glow(sx + B * 0.1f, sb, B * 0.8f, B * 0.42f, 0, pal->block_edge); break;
            case OBJ_SPIKE_SM_DOWN: spike_glow(sx + B * 0.1f, sb - B, B * 0.8f, B * 0.42f, 1, pal->block_edge); break;
            case OBJ_SAW_BIG: draw_circle_grad(sx + B * 0.5f, sb - B * 0.5f, B * 1.5f, gc, gz); break;
            case OBJ_SAW_SMALL: draw_circle_grad(sx + B * 0.5f, sb - B * 0.5f, B * 0.85f, gc, gz); break;
            default: break;
            }
        }
    }
    gfx_blend(BLEND_ALPHA);

    /* block fills */
    for (int cy = r0; cy < r1; cy++) {
        for (int cx = c0; cx < c1; cx++) {
            int t = L->grid[cy * L->width + cx];
            if (!t) continue;
            float x0 = view_sx(v, (float)cx), x1 = x0 + B;
            float y1 = view_sy(v, (float)cy), y0 = y1 - B;
            if (t == OBJ_SLAB_LO) y0 = y1 - B * 0.5f;
            else if (t == OBJ_SLAB_HI) y1 = y0 + B * 0.5f;
            Color f = pal->block_fill;
            gfx_rect_v(x0, y0, x1, y1, col_lerp(f, pal->block_edge, 0.10f), f);
            uint32_t h = hash_u32((uint32_t)(cx * 73856093) ^ (uint32_t)(cy * 19349663));
            if (t == OBJ_BLOCK && (h % 5) == 0) {
                float in = B * 0.24f;
                draw_rect_outline(x0 + in, y0 + in, x1 - in, y1 - in, 2.0f,
                                  col_with_alpha(pal->block_edge, 0.28f));
            }
        }
    }
    /* block edges (they light up on the beat) */
    const float ew = grid_w(3.0f), eh = grid_h(3.0f);
    Color ec = col_lerp(pal->block_edge, COL_WHITE, 0.45f * v->pulse);
    for (int cy = r0; cy < r1; cy++) {
        for (int cx = c0; cx < c1; cx++) {
            int t = L->grid[cy * L->width + cx];
            if (!t) continue;
            uint8_t e = L->edges[cy * L->width + cx];
            float x0 = view_sx(v, (float)cx), x1 = x0 + B;
            float y1 = view_sy(v, (float)cy), y0 = y1 - B;
            if (t == OBJ_SLAB_LO) y0 = y1 - B * 0.5f;
            else if (t == OBJ_SLAB_HI) y1 = y0 + B * 0.5f;
            if (e & EDGE_T) gfx_rect(x0, y0, x1, y0 + eh, ec);
            if (e & EDGE_B) gfx_rect(x0, y1 - eh, x1, y1, ec);
            if (e & EDGE_L) gfx_rect(x0, y0, x0 + ew, y1, ec);
            if (e & EDGE_R) gfx_rect(x1 - ew, y0, x1, y1, ec);
        }
    }

    /* objects */
    int oc0 = clampi(c0 - 1, 0, L->width), oc1 = clampi(c1 + 1, 0, L->width);
    const Color spike_fill = SPIKE_FILL;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = L->col_start[oc0]; i < L->col_start[oc1]; i++) {
            const LevelObj *o = &L->objs[i];
            if (o->cy < r0 - 2 || o->cy > r1 + 2) continue;
            float sx = view_sx(v, (float)o->cx), sb = view_sy(v, (float)o->cy);
            float mx = sx + B * 0.5f, my = sb - B * 0.5f;
            int t = o->type;
            /* pass 0: glowy stuff behind hazards (portals, pads); pass 1: the rest */
            int back = (t >= OBJ_PORTAL_CUBE && t <= OBJ_SPEED_3) || (t >= OBJ_PAD_YELLOW && t <= OBJ_PAD_BLUE);
            if ((pass == 0) != back) continue;
            switch (t) {
            case OBJ_SPIKE_UP: render_spike(sx, sb, B, B * 0.92f, 0, spike_fill, ec); break;
            case OBJ_SPIKE_DOWN: render_spike(sx, sb - B, B, B * 0.92f, 1, spike_fill, ec); break;
            case OBJ_SPIKE_SM_UP: render_spike(sx + B * 0.1f, sb, B * 0.8f, B * 0.42f, 0, spike_fill, ec); break;
            case OBJ_SPIKE_SM_DOWN: render_spike(sx + B * 0.1f, sb - B, B * 0.8f, B * 0.42f, 1, spike_fill, ec); break;
            case OBJ_SAW_BIG: render_saw(mx, my, B * 0.98f, v->time * 5.0f, spike_fill, ec); break;
            case OBJ_SAW_SMALL: render_saw(mx, my, B * 0.5f, -v->time * 7.0f, spike_fill, ec); break;
            case OBJ_ORB_YELLOW: case OBJ_ORB_PINK: case OBJ_ORB_BLUE: case OBJ_ORB_GREEN: {
                float a = is_used(p, o->id) ? 0.45f : 1.0f;
                render_orb(mx, my, B * 0.30f, col_with_alpha(orb_color(t), a), v->time, v->pulse);
                break;
            }
            case OBJ_PAD_YELLOW: case OBJ_PAD_PINK: case OBJ_PAD_BLUE:
                if (o->flags & OF_CEILING) render_pad(sx, sb - B, 1, orb_color(t), v->pulse);
                else render_pad(sx, sb, 0, orb_color(t), v->pulse);
                break;
            case OBJ_PORTAL_CUBE: render_portal(mx, my, COL_PORTAL_CUBE, v->time, MODE_CUBE, 0); break;
            case OBJ_PORTAL_SHIP: render_portal(mx, my, COL_PORTAL_SHIP, v->time, MODE_SHIP, 0); break;
            case OBJ_PORTAL_BALL: render_portal(mx, my, COL_PORTAL_BALL, v->time, MODE_BALL, 0); break;
            case OBJ_PORTAL_UFO: render_portal(mx, my, COL_PORTAL_UFO, v->time, MODE_UFO, 0); break;
            case OBJ_PORTAL_WAVE: render_portal(mx, my, COL_PORTAL_WAVE, v->time, MODE_WAVE, 0); break;
            case OBJ_PORTAL_GRAV_FLIP: render_portal(mx, my, COL_PORTAL_FLIP, v->time, -1, 1); break;
            case OBJ_PORTAL_GRAV_NORMAL: render_portal(mx, my, COL_PORTAL_NORMAL, v->time, -1, -1); break;
            case OBJ_SPEED_0: case OBJ_SPEED_1: case OBJ_SPEED_2: case OBJ_SPEED_3:
                render_speed_portal(mx, my, t - OBJ_SPEED_0, v->time);
                break;
            case OBJ_COIN: {
                if (is_used(p, o->id)) break;
                int idx = (o->flags >> 4) & 3;
                int ghost = (saved_coins >> idx) & 1;
                render_coin(mx, my, B * 0.42f, v->time * 2.5f + o->cx, 1.0f, ghost);
                break;
            }
            default: break;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* UI helpers                                                          */
/* ------------------------------------------------------------------ */

void render_panel(float x0, float y0, float x1, float y1, Color fill, Color edge)
{
    const float r = 10.0f, bw = grid_w(3.0f), bh = grid_h(3.0f);
    /* on whole pixels, so the borders come out even */
    if (draw_pixel_grid() > 0.0f) {
        x0 = grid_snap(x0);
        y0 = grid_snap_y(y0);
        x1 = grid_snap(x1);
        y1 = grid_snap_y(y1);
    } else {
        x0 = floorf(x0 + 0.5f);
        y0 = floorf(y0 + 0.5f);
        x1 = floorf(x1 + 0.5f);
        y1 = floorf(y1 + 0.5f);
    }
    gfx_rect(x0 + r, y0, x1 - r, y1, fill);
    gfx_rect(x0, y0 + r, x0 + r, y1 - r, fill);
    gfx_rect(x1 - r, y0 + r, x1, y1 - r, fill);
    /* rounded corners */
    float cxs[4] = {x0 + r, x1 - r, x1 - r, x0 + r}, cys[4] = {y0 + r, y0 + r, y1 - r, y1 - r};
    for (int k = 0; k < 4; k++) {
        float a0 = PI + k * PI * 0.5f;
        for (int i = 0; i < 4; i++) {
            float t0 = a0 + i * (PI / 8), t1 = t0 + PI / 8;
            gfx_tri(cxs[k], cys[k], fill, cxs[k] + cosf(t0) * r, cys[k] + sinf(t0) * r, fill,
                    cxs[k] + cosf(t1) * r, cys[k] + sinf(t1) * r, fill);
        }
        draw_arc(cxs[k], cys[k], r - bw, r, a0, a0 + PI * 0.5f, edge);
    }
    gfx_rect(x0 + r, y0, x1 - r, y0 + bh, edge);
    gfx_rect(x0 + r, y1 - bh, x1 - r, y1, edge);
    gfx_rect(x0, y0 + r, x0 + bw, y1 - r, edge);
    gfx_rect(x1 - bw, y0 + r, x1, y1 - r, edge);
}

void render_progress_bar(float x0, float y0, float x1, float y1, float frac, Color fill_a, Color fill_b)
{
    frac = clampf(frac, 0.0f, 1.0f);
    x0 = grid_snap(x0); /* on whole pixels, so the frame comes out even */
    y0 = grid_snap_y(y0);
    x1 = grid_snap(x1);
    y1 = grid_snap_y(y1);
    float b = grid_w(3.0f), bh = grid_h(3.0f);
    gfx_rect(x0 - b, y0 - bh, x1 + b, y1 + bh, RGBA(0, 0, 0, 200));
    gfx_rect(x0, y0, x1, y1, RGBA(255, 255, 255, 40));
    if (frac > 0.0f) {
        float xm = x0 + (x1 - x0) * frac;
        gfx_rect_h(x0, y0, xm, y1, fill_a, col_lerp(fill_a, fill_b, frac));
        gfx_rect(x0, y0, xm, y0 + (y1 - y0) * 0.35f, RGBA(255, 255, 255, 70));
    }
}
