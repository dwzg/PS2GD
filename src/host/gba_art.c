/*
 * The Game Boy Advance ROM's graphics (gba_tool export): the level's cells,
 * the ground, the squares behind, the font, and the sprites, drawn for the
 * GBA's 12 pixels a block. The shapes follow the core's renderer
 * (src/core/render.c, icons.c, font.c); the sprites with fixed colours are
 * drawn by it, at the GBA's size, and turned into 16-colour tiles.
 *
 * The level's cells are drawn in the world palette's colour slots
 * (src/gba/video.h WC_*), which the ROM fills from the level's palette as
 * it changes; see src/gba/art.h for the ids and layouts.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_gen.h"
#include "../core/font.h"
#include "../core/level.h"
#include "png_write.h"
#include "../gba/art.h"

/* ------------------------------------------------------------------ */
/* Tiles                                                               */
/* ------------------------------------------------------------------ */

/* An 8x8 4-bit tile from an index picture (stride in pixels). */
static void pack_tile(uint32_t out[8], const uint8_t *px, int stride)
{
    for (int y = 0; y < 8; y++) {
        uint32_t w = 0;
        for (int x = 0; x < 8; x++) w |= (uint32_t)(px[y * stride + x] & 15) << (x * 4);
        out[y] = w;
    }
}

static void flip_tile(uint32_t out[8], const uint32_t in[8], int h, int v)
{
    for (int y = 0; y < 8; y++) {
        uint32_t w = in[v ? 7 - y : y];
        if (h) {
            uint32_t r = 0;
            for (int x = 0; x < 8; x++) r |= ((w >> (x * 4)) & 15) << ((7 - x) * 4);
            w = r;
        }
        out[y] = w;
    }
}

/* A set of distinct tiles; tile_add returns a screen entry (index and flips). */
typedef struct {
    uint32_t (*t)[8];
    int n, cap;
} TileSet;

static uint16_t tile_add(TileSet *s, const uint32_t t[8], int first)
{
    for (int i = 0; i < s->n; i++) {
        for (int f = 0; f < 4; f++) {
            uint32_t ft[8];
            flip_tile(ft, s->t[i], f & 1, f >> 1);
            if (!memcmp(ft, t, 32))
                return (uint16_t)((first + i) | (f & 1 ? ART_HFLIP : 0) | (f & 2 ? ART_VFLIP : 0));
        }
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->t = realloc(s->t, (size_t)s->cap * 32);
    }
    memcpy(s->t[s->n], t, 32);
    return (uint16_t)(first + s->n++);
}

/* ------------------------------------------------------------------ */
/* Sine table                                                          */
/* ------------------------------------------------------------------ */

static void emit_sin(GbaGen *g)
{
    int16_t t[1024];
    for (int i = 0; i < 1024; i++) t[i] = (int16_t)lround(sin(i * 2.0 * M_PI / 1024.0) * 16384.0);
    gba_gen_blob(g, "g_sin_tab", "int16_t", t, sizeof(t));
}

/* ------------------------------------------------------------------ */
/* The level's cells                                                   */
/* ------------------------------------------------------------------ */

#define B BLOCK_PIX

static uint8_t s_cells[CELL_COUNT][B][B];

static void draw_block(uint8_t c[B][B], int edges, int decor)
{
    /* the fill, a little lighter towards the top (render.c: lerp to the
     * edge colour by 0.10 at the top) */
    for (int y = 0; y < B; y++)
        for (int x = 0; x < B; x++) c[y][x] = y < B / 2 ? WC_FILL_TOP : WC_FILL;
    if (decor) {
        /* the inner square, 0.24 of a block in */
        for (int i = 3; i <= 8; i++) {
            c[3][i] = c[8][i] = WC_DECOR;
            c[i][3] = c[i][8] = WC_DECOR;
        }
    }
    for (int i = 0; i < B; i++) {
        if (edges & EDGE_T) c[0][i] = WC_EDGE;
        if (edges & EDGE_B) c[B - 1][i] = WC_EDGE;
        if (edges & EDGE_L) c[i][0] = WC_EDGE;
        if (edges & EDGE_R) c[i][B - 1] = WC_EDGE;
    }
}

static void draw_slab(uint8_t c[B][B], int hi, int ends)
{
    int y0 = hi ? 0 : B / 2, y1 = y0 + B / 2;
    for (int y = y0; y < y1; y++)
        for (int x = 0; x < B; x++) {
            int edge = y == y0 || y == y1 - 1 || (x == 0 && (ends & 1)) || (x == B - 1 && (ends & 2));
            c[y][x] = edge ? WC_EDGE : (y < y0 + 3 ? WC_FILL_TOP : WC_FILL);
        }
}

/* Distance from (px, py) to the segment a-b. */
static double seg_dist(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    double t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double ex = ax + dx * t - px, ey = ay + dy * t - py;
    return sqrt(ex * ex + ey * ey);
}

/* render_spike: base from x0 to x1 on the cell's bottom edge (top if
 * down), tip h above it; a dark body lightening towards the tip, with a
 * bright outline. Sampled 4x4 per pixel: a pixel is the spike's if half
 * of it is inside. */
static void draw_spike(uint8_t c[B][B], double x0, double x1, double h, int down)
{
    const double lw = 1.0; /* outline width, 3 of 34 pixels a block on the PC */
    double tipx = (x0 + x1) * 0.5;
    for (int y = 0; y < B; y++)
        for (int x = 0; x < B; x++) {
            int in = 0, edge = 0;
            double hsum = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4;
                    double up = B - py; /* height above the base */
                    if (up < 0 || up > h) continue;
                    double half = (x1 - x0) * 0.5 * (1.0 - up / h);
                    if (px < tipx - half || px > tipx + half) continue;
                    in++;
                    hsum += up / h;
                    if (up < lw || seg_dist(px, up, x0, 0, tipx, h) < lw || seg_dist(px, up, x1, 0, tipx, h) < lw)
                        edge++;
                }
            if (in < 8) continue;
            double t = hsum / in;
            int col = t < 0.38 ? WC_SPIKE : (t < 0.7 ? WC_SPIKE_MID : WC_SPIKE_TIP);
            if (edge * 2 >= in) col = WC_EDGE;
            c[down ? B - 1 - y : y][x] = (uint8_t)col;
        }
}

/* The glow (render_level's, spike_glow's): how strong it is at a pixel, as
 * a share of the blocks' glow at its strongest (0.30: the PC's 0.20 +
 * 0.10 on the beat), to WC_HALO, WC_HALO2 or nothing. The blocks' glow
 * fades out over 8 of the PC's 34 pixels, 2.8 here: 0.82 of it in the
 * pixel next to an edge, 0.47 in the one after, 0.11 in the third (left
 * out); the colours are those two. */
static int halo_of(double a)
{
    return a >= 0.64 ? WC_HALO : (a >= 0.25 ? WC_HALO2 : 0);
}

/* where the glow of the blocks beside a cell falls in it (art.h G_*): the
 * two pixels along a side, and round a corner as strong as the weaker of
 * the two sides' (corner_glow) */
static int glow_at(int mask, int x, int y)
{
    int d = 2; /* pixels from the nearest edge glowing into it (2: none) */
#define NEAR(bit, dist) do { if ((mask & (bit)) && (dist) < d) d = (dist); } while (0)
    NEAR(G_TOP, y);
    NEAR(G_BOT, B - 1 - y);
    NEAR(G_LEFT, x);
    NEAR(G_RIGHT, B - 1 - x);
    NEAR(G_TL, x > y ? x : y);
    NEAR(G_TR, B - 1 - x > y ? B - 1 - x : y);
    NEAR(G_BL, x > B - 1 - y ? x : B - 1 - y);
    NEAR(G_BR, B - 1 - x > B - 1 - y ? B - 1 - x : B - 1 - y);
#undef NEAR
    return d == 0 ? WC_HALO : (d == 1 ? WC_HALO2 : 0);
}

/* the glow under a slab's edge inside its own cell (its top's, or its
 * bottom's for one at the top: both are always exposed) */
static void glow_slab(uint8_t c[B][B], int hi)
{
    int edge = hi ? B / 2 - 1 : B / 2; /* the row of its edge facing the cell's empty half */
    for (int x = 0; x < B; x++)
        for (int k = 1; k <= 2; k++) {
            int y = hi ? edge + k : edge - k;
            if (!c[y][x]) c[y][x] = (uint8_t)(k == 1 ? WC_HALO : WC_HALO2);
        }
}

/* spike_glow: a fan from the spike's centroid, 0.30 strong there, out to
 * its base's ends pushed out by 0.3 of its width and its tip pushed up by
 * as much, nothing there; where the cell has nothing else (sampled 4x4 a
 * pixel; what falls in the cells beside is left out) */
static double tri_weight(double px, double py, const double t[6])
{
    /* the barycentric weight of t's first corner at p, or -1 outside */
    double x0 = t[0], y0 = t[1], x1 = t[2], y1 = t[3], x2 = t[4], y2 = t[5];
    double den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    double l0 = ((y1 - y2) * (px - x2) + (x2 - x1) * (py - y2)) / den;
    double l1 = ((y2 - y0) * (px - x2) + (x0 - x2) * (py - y2)) / den;
    double l2 = 1.0 - l0 - l1;
    return l0 >= 0 && l1 >= 0 && l2 >= 0 ? l0 : -1.0;
}

static void glow_spike(uint8_t c[B][B], double x0, double x1, double h, int down)
{
    double w = x1 - x0, g = 0.3 * w, cx = (x0 + x1) * 0.5, cy = B - h * 0.33;
    double p[3][2] = {{x0 - g, B}, {x1 + g, B}, {cx, B - (h + g)}};
    for (int y = 0; y < B; y++)
        for (int x = 0; x < B; x++) {
            double sum = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4;
                    for (int i = 0; i < 3; i++) {
                        int j = (i + 1) % 3;
                        double t[6] = {cx, cy, p[i][0], p[i][1], p[j][0], p[j][1]};
                        double l = tri_weight(px, py, t);
                        if (l >= 0) {
                            sum += l;
                            break;
                        }
                    }
                }
            /* 0.30 of the edge's colour at the centroid: as strong as the
             * blocks' at its strongest */
            int col = halo_of(sum / 16.0);
            int yy = down ? B - 1 - y : y;
            if (col && !c[yy][x]) c[yy][x] = (uint8_t)col;
        }
}

static void make_cells(void)
{
    memset(s_cells, 0, sizeof(s_cells));
    for (int e = 0; e < 16; e++) {
        draw_block(s_cells[CELL_BLOCK + e], e, 0);
        draw_block(s_cells[CELL_BLOCK_DECOR + e], e, 1);
    }
    for (int k = 0; k < 4; k++) {
        draw_slab(s_cells[CELL_SLAB_LO + k], 0, k);
        draw_slab(s_cells[CELL_SLAB_HI + k], 1, k);
    }
    /* sizes as render_level draws them: full spikes a block wide and 0.92
     * high, small ones 0.8 wide (0.1 in) and 0.42 high */
    draw_spike(s_cells[CELL_SPIKE_UP], 0.0, B, 0.92 * B, 0);
    draw_spike(s_cells[CELL_SPIKE_DOWN], 0.0, B, 0.92 * B, 1);
    draw_spike(s_cells[CELL_SPIKE_SM_UP], 0.1 * B, 0.9 * B, 0.42 * B, 0);
    draw_spike(s_cells[CELL_SPIKE_SM_DOWN], 0.1 * B, 0.9 * B, 0.42 * B, 1);
    /* the glow behind them, where they leave the cell empty */
    for (int k = 0; k < 4; k++) {
        glow_slab(s_cells[CELL_SLAB_LO + k], 0);
        glow_slab(s_cells[CELL_SLAB_HI + k], 1);
    }
    glow_spike(s_cells[CELL_SPIKE_UP], 0.0, B, 0.92 * B, 0);
    glow_spike(s_cells[CELL_SPIKE_DOWN], 0.0, B, 0.92 * B, 1);
    glow_spike(s_cells[CELL_SPIKE_SM_UP], 0.1 * B, 0.9 * B, 0.42 * B, 0);
    glow_spike(s_cells[CELL_SPIKE_SM_DOWN], 0.1 * B, 0.9 * B, 0.42 * B, 1);
}

/* a 12x12 picture as a cell's 3x3 quarters of 4 rows (art.h) */
static void quarters(uint16_t q[36], const uint8_t c[B][B])
{
    for (int qy = 0; qy < 3; qy++)
        for (int qx = 0; qx < 3; qx++)
            for (int r = 0; r < 4; r++) {
                uint16_t w = 0;
                for (int x = 0; x < 4; x++) w |= (uint16_t)((c[qy * 4 + r][qx * 4 + x] & 15) << (x * 4));
                q[(qy * 3 + qx) * 4 + r] = w;
            }
}

static uint16_t s_q[CELL_COUNT][36], s_gq[256][36];

/* the cells' and the glows' quarters, as g_cells and g_glow (for
 * gba_levels.c, which puts the levels' tiles together) */
const uint16_t *gba_art_cells(void)
{
    return &s_q[0][0];
}
const uint16_t *gba_art_glow(void)
{
    return &s_gq[0][0];
}

static void emit_cells(GbaGen *g)
{
    uint16_t (*q)[36] = s_q, (*gq)[36] = s_gq;
    static uint8_t c[B][B];
    make_cells();
    /* (for gba_levels.c, which makes the levels' tiles from them: the ROM
     * has those) */
    (void)g;
    for (int i = 0; i < CELL_COUNT; i++) quarters(q[i], s_cells[i]);
    for (int m = 0; m < 256; m++) {
        for (int y = 0; y < B; y++)
            for (int x = 0; x < B; x++) c[y][x] = (uint8_t)glow_at(m, x, y);
        quarters(gq[m], c);
    }
}

/* ------------------------------------------------------------------ */
/* Ground and bands                                                    */
/* ------------------------------------------------------------------ */

static uint8_t s_ground[GT_COUNT][8][8];

static void make_ground(void)
{
    memset(s_ground, 0, sizeof(s_ground));
    for (int d = 0; d < GROUND_DEPTHS; d++)
        for (int sep = 0; sep < 2; sep++) {
            uint8_t (*t)[8] = s_ground[GT_GROUND + d * 2 + sep];
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) t[y][x] = (uint8_t)(sep && x == 0 ? GC_SEP + d : GC_SHADE + d);
            if (d == 0)
                for (int x = 0; x < 8; x++) t[0][x] = GC_LINE;
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            s_ground[GT_GLOW][y][x] = (uint8_t)(y >= 6 ? WC_GLOW : (y >= 3 ? WC_GLOW2 : 0));
            s_ground[GT_BAND][y][x] = WC_BAND;
            s_ground[GT_CEIL_7][y][x] = (uint8_t)(y == 7 ? WC_BAND_LINE : WC_BAND);
            s_ground[GT_CEIL_3][y][x] = (uint8_t)(y == 3 ? WC_BAND_LINE : (y < 3 ? WC_BAND : 0));
            s_ground[GT_FLOOR_0][y][x] = (uint8_t)(y == 0 ? WC_BAND_LINE : WC_BAND);
            s_ground[GT_FLOOR_4][y][x] = (uint8_t)(y == 4 ? WC_BAND_LINE : (y > 4 ? WC_BAND : 0));
        }
}

static void emit_ground(GbaGen *g)
{
    static uint32_t t[GT_COUNT][8];
    make_ground();
    for (int i = 0; i < GT_COUNT; i++) pack_tile(t[i], &s_ground[i][0][0], 8);
    gba_gen_blob(g, "g_ground_tiles", "uint32_t", t, sizeof(t));
}

/* ------------------------------------------------------------------ */
/* The squares behind                                                  */
/* ------------------------------------------------------------------ */

#define SQ_W (SQ_MAP_W * 8)
#define SQ_H (SQ_MAP_H * 8)
static uint8_t s_sq[SQ_H][SQ_W];

static void sq_box(int x0, int y0, int size, int inner)
{
    for (int y = y0; y < y0 + size; y++)
        for (int x = x0; x < x0 + size; x++) {
            int xx = x & (SQ_W - 1);
            if (y < 0 || y >= SQ_H) continue;
            int edge = x == x0 || x == x0 + size - 1 || y == y0 || y == y0 + size - 1;
            int in = size / 4;
            if (inner && !edge && x >= x0 + in && x <= x0 + size - 1 - in && y >= y0 + in && y <= y0 + size - 1 - in &&
                (x == x0 + in || x == x0 + size - 1 - in || y == y0 + in || y == y0 + size - 1 - in))
                edge = 1;
            s_sq[y][xx] = (uint8_t)(edge ? WC_SQ_EDGE : WC_SQ_FILL);
        }
}

/* render_background's two layers of squares (big far ones, smaller near
 * ones), in one picture that repeats every 512 pixels; positions and sizes
 * on a 4-pixel grid so that its tiles repeat */
static void make_squares(void)
{
    memset(s_sq, 0, sizeof(s_sq));
    for (int pass = 0; pass < 2; pass++) {
        int n = pass ? 8 : 4, slot = SQ_W / n;
        double min_s = pass ? 1.5 : 3.0, max_s = pass ? 3.0 : 5.0;
        uint32_t seed = pass ? 0xA7u : 0x51u;
        for (int i = 0; i < n; i++) {
            uint32_t h = hash_u32((uint32_t)i * 2654435761u ^ seed);
            double size = min_s + (max_s - min_s) * hash_f01(h);
            int s = ((int)(size * B) + 2) / 4 * 4;
            int ox = (int)(hash_f01(h >> 3) * (slot - s * 0.5)) / 4 * 4;
            int oy = (int)((1.5 + hash_f01(h >> 7) * 8.0) * B) / 4 * 4;
            sq_box(i * slot + ox, SQ_H - oy - s, s, (int)(h & 1));
        }
    }
}

static void emit_squares(GbaGen *g)
{
    static uint16_t map[SQ_MAP_H][SQ_MAP_W];
    TileSet ts = {0};
    uint32_t t[8];
    make_squares();
    /* tile 0 is the empty one */
    memset(t, 0, sizeof(t));
    tile_add(&ts, t, 0);
    for (int ty = 0; ty < SQ_MAP_H; ty++)
        for (int tx = 0; tx < SQ_MAP_W; tx++) {
            pack_tile(t, &s_sq[ty * 8][tx * 8], SQ_W);
            map[ty][tx] = tile_add(&ts, t, 0);
        }
    gba_gen_blob(g, "g_sq_tiles", "uint32_t", ts.t, (size_t)ts.n * 32);
    gba_gen_blob(g, "g_sq_map", "uint16_t", map, sizeof(map));
    fprintf(g->hdr, "#define SQ_TILE_COUNT %d\n", ts.n);
    free(ts.t);
}

/* ------------------------------------------------------------------ */
/* Font                                                                */
/* ------------------------------------------------------------------ */

static void emit_font(GbaGen *g)
{
    static uint8_t f[128][8];
    memset(f, 0, sizeof(f));
    for (int c = 0; c < 128; c++) font_glyph((char)c, f[c]);
    gba_gen_blob(g, "g_font", "uint8_t", f, sizeof(f));
}

/* ------------------------------------------------------------------ */

int gba_art_obj_export(GbaGen *g);

int gba_art_export(GbaGen *g)
{
    if (gba_art_obj_export(g)) return 1;
    emit_sin(g);
    emit_cells(g);
    emit_ground(g);
    emit_squares(g);
    emit_font(g);
    return 0;
}

/* gba_tool artsheet <out.png>: every cell, ground tile and the squares in a
 * sample palette, magnified, to look at. */
int gba_art_sheet(const char *out)
{
    /* (in the order of art.h's WC_*) */
    static const uint8_t pal[16][3] = {
        {40, 90, 220},  {60, 110, 230}, {120, 170, 255}, {10, 20, 60}, {20, 34, 80},   {80, 100, 140},
        {30, 70, 190},  {200, 220, 255}, {110, 160, 250}, {80, 128, 238}, {225, 238, 255}, {8, 8, 12},
        {30, 30, 44},   {70, 70, 96},   {120, 160, 255}, {80, 120, 240}};
    const int Z = 4, W = 16 * (B + 2) * Z;
    int rows = (CELL_COUNT + 15) / 16;
    int H = (rows * (B + 2) + 12 + SQ_H / 2) * Z;
    uint8_t *img = calloc((size_t)W * H * 3, 1);
    make_cells();
    make_ground();
    make_squares();
#define PUT(x, y, c) do { for (int zy = 0; zy < Z; zy++) for (int zx = 0; zx < Z; zx++) { \
        uint8_t *p = img + (((size_t)(y) * Z + zy) * W + (size_t)(x) * Z + zx) * 3; \
        memcpy(p, pal[(c) & 15], 3); } } while (0)
    for (int i = 0; i < CELL_COUNT; i++)
        for (int y = 0; y < B; y++)
            for (int x = 0; x < B; x++) PUT((i % 16) * (B + 2) + x, (i / 16) * (B + 2) + y, s_cells[i][y][x]);
    int y0 = rows * (B + 2) + 2;
    for (int i = 0; i < GT_COUNT; i++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) PUT(i * 10 + x, y0 + y, s_ground[i][y][x]);
    for (int y = 0; y < SQ_H / 2; y++)
        for (int x = 0; x < W / Z && x < SQ_W; x++) PUT(x, y0 + 10 + y, s_sq[SQ_H / 2 + y][x]);
#undef PUT
    int r = png_write_rgb(out, img, W, H);
    free(img);
    return r;
}
