/*
 * The bottom screen's drawing: gfx.h's primitives drawn by the CPU into a
 * 16-bit bitmap the sub engine shows (VRAM C), for the core's own text,
 * panels and icons there (bottom_nds.c). gfx_nds.c sends its primitives here between
 * gfx_nds_bottom_begin and _end. The bottom screen changes only now and
 * then (a screen, an attempt), so what is drawn there is drawn once, not
 * every frame.
 *
 * Coordinates come in the vertices' units of gfx_nds.c (1/16 of a pixel):
 * the bottom screen is laid out as the top one, the virtual screen scaled
 * by the pixel grid. Triangles cover the pixels whose middles are inside
 * them (an edge's pixels go to the triangle on its right or below, so
 * the triangles of a fan don't blend twice there), their colours and
 * alpha blended across them; triangles reaching more than 128 pixels off
 * the screen are left out (nothing on the bottom screen does).
 */
#include <nds.h>
#include <string.h>

#include "nds_platform.h"
#include <stdint.h>
#include "../core/common.h"

#define W 256
#define H 192

/* drawn into a copy in main RAM, then copied to the screen's bitmap at the
 * vertical blank by DMA (soft_present): the CPU's writes and blends are
 * faster there (the data cache), and the screen never shows a picture
 * half drawn */
static uint16_t s_back[W * H] __attribute__((aligned(32)));
static uint16_t *FB = s_back;
uint16_t *soft_screen(void) { return s_back; }
static int s_present;

void soft_init(void)
{
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    for (int i = 0; i < W * H; i++) ((uint16_t *)BG_GFX_SUB)[i] = 0x8000;
}

void soft_target(uint16_t *fb)
{
    FB = fb ? fb : s_back;
}

void soft_vblank(void)
{
    if (!s_present) return;
    DC_FlushRange(s_back, sizeof(s_back));
    dmaCopyWords(3, s_back, BG_GFX_SUB, sizeof(s_back));
    s_present = 0;
}

/* c over the pixel at p, alpha a (0..256) */
static inline void blend(uint16_t *p, unsigned r, unsigned g, unsigned b, unsigned a)
{
    if (a >= 256) {
        *p = (uint16_t)(0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
        return;
    }
    unsigned d = *p, dr = (d & 31) << 3, dg = ((d >> 5) & 31) << 3, db = ((d >> 10) & 31) << 3;
    dr += ((r - dr) * a) >> 8;
    dg += ((g - dg) * a) >> 8;
    db += ((b - db) * a) >> 8;
    *p = (uint16_t)(0x8000 | (dr >> 3) | ((dg >> 3) << 5) | ((db >> 3) << 10));
}

static inline unsigned alpha256(Color c)
{
    unsigned a = COL_A(c);
    return a + (a >> 7);
}

/* the rectangle of pixels [x0, x1) x [y0, y1), from top's colour down to
 * bottom's */
static void do_rect(int x0, int y0, int x1, int y1, Color top, Color bottom)
{
    int h = y1 - y0;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    if (x0 >= x1 || h <= 0) return;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < H; y++) {
        Color c = top == bottom ? top : col_lerp(top, bottom, ((float)(y - y0) + 0.5f) / (float)h);
        unsigned r = COL_R(c), g = COL_G(c), b = COL_B(c), a = alpha256(c);
        if (!a) continue;
        uint16_t *p = FB + y * W;
        if (a >= 256) {
            /* two pixels a word */
            uint16_t v = (uint16_t)(0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
            int x = x0;
            if (x & 1) p[x++] = v;
            uint32_t *w = (uint32_t *)(p + x), vv = v | (uint32_t)v << 16;
            for (; x + 1 < x1; x += 2) *w++ = vv;
            if (x < x1) p[x] = v;
            continue;
        }
        for (int x = x0; x < x1; x++) blend(p + x, r, g, b, a);
    }
}

/* edge function of (x, y) against a -> b, in 1/16 pixel units */
static inline int64_t edge(int ax, int ay, int bx, int by, int x, int y)
{
    return (int64_t)(x - ax) * (by - ay) - (int64_t)(y - ay) * (bx - ax);
}

/* a pixel on an edge goes to the triangle it is a top or a left edge of */
static inline int top_left(int ax, int ay, int bx, int by)
{
    return (ay == by && bx < ax) || by < ay;
}

static void do_tri(const int16_t *xy, const Color *c)
{
    int x0 = xy[0], y0 = xy[1], x1 = xy[2], y1 = xy[3], x2 = xy[4], y2 = xy[5];
    int64_t area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    if (area < 0) {
        /* the other way round */
        int tx = x1, ty = y1;
        x1 = x2;
        y1 = y2;
        x2 = tx;
        y2 = ty;
        Color cc[3] = {c[0], c[2], c[1]};
        int16_t v[6] = {(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, (int16_t)x2, (int16_t)y2};
        do_tri(v, cc);
        return;
    }
    int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
    int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int px0 = minx >> 4, px1 = (maxx + 15) >> 4, py0 = miny >> 4, py1 = (maxy + 15) >> 4;
    if (px0 < 0) px0 = 0;
    if (py0 < 0) py0 = 0;
    if (px1 > W) px1 = W;
    if (py1 > H) py1 = H;
    int flat = c[0] == c[1] && c[1] == c[2];
    int b0 = top_left(x1, y1, x2, y2) ? 0 : -1, b1 = top_left(x2, y2, x0, y0) ? 0 : -1,
        b2 = top_left(x0, y0, x1, y1) ? 0 : -1;
    /* the edge functions at the first pixel's middle, and their steps a
     * pixel across (16 units) and down */
    if (px0 >= px1 || py0 >= py1) return;
    /* (in 32 bits: the corners are within 128 pixels of the screen, so
     * these stay under 2^31) */
    if (minx < -2048 || miny < -2048 || maxx > (W + 128) * 16 || maxy > (H + 128) * 16) return;
    int32_t r0 = (int32_t)edge(x1, y1, x2, y2, px0 * 16 + 8, py0 * 16 + 8) + b0,
            r1 = (int32_t)edge(x2, y2, x0, y0, px0 * 16 + 8, py0 * 16 + 8) + b1,
            r2 = (int32_t)edge(x0, y0, x1, y1, px0 * 16 + 8, py0 * 16 + 8) + b2;
    int32_t dx0 = (y2 - y1) * 16, dx1 = (y0 - y2) * 16, dx2 = (y1 - y0) * 16;
    int32_t dy0 = -(x2 - x1) * 16, dy1 = -(x0 - x2) * 16, dy2 = -(x1 - x0) * 16;
    unsigned fr = COL_R(c[0]), fg = COL_G(c[0]), fb = COL_B(c[0]), fa = alpha256(c[0]);
    /* the colours across it (not flat): each channel at the first pixel
     * and its steps across and down, in 16.16 */
    int32_t cs[4] = {0, 0, 0, 0}, cdx[4] = {0, 0, 0, 0}, cdy[4] = {0, 0, 0, 0};
    if (!flat)
        for (int k = 0; k < 4; k++) {
            int sh = 24 - 8 * k;
            int64_t v0 = (c[0] >> sh) & 255, v1 = (c[1] >> sh) & 255, v2 = (c[2] >> sh) & 255;
            cs[k] = (int32_t)(((((int64_t)r0 - b0) * v0 + ((int64_t)r1 - b1) * v1 + ((int64_t)r2 - b2) * v2) << 16) / area);
            cdx[k] = (int32_t)((((int64_t)dx0 * v0 + (int64_t)dx1 * v1 + (int64_t)dx2 * v2) << 16) / area);
            cdy[k] = (int32_t)((((int64_t)dy0 * v0 + (int64_t)dy1 * v1 + (int64_t)dy2 * v2) << 16) / area);
        }
    for (int py = py0; py < py1; py++, r0 += dy0, r1 += dy1, r2 += dy2) {
        uint16_t *row = FB + py * W;
        int32_t w0 = r0, w1 = r1, w2 = r2;
        int32_t ch[4];
        for (int k = 0; k < 4; k++) ch[k] = cs[k] + cdy[k] * (py - py0);
        for (int px = px0; px < px1; px++, w0 += dx0, w1 += dx1, w2 += dx2) {
            if ((w0 | w1 | w2) >= 0) {
                if (flat) {
                    blend(row + px, fr, fg, fb, fa);
                } else {
                    unsigned a = (unsigned)(ch[0] < 0 ? 0 : ch[0] >> 16);
                    blend(row + px, (unsigned)(ch[1] >> 16) & 255, (unsigned)(ch[2] >> 16) & 255,
                          (unsigned)(ch[3] >> 16) & 255, a > 255 ? 256 : a + (a >> 7));
                }
            }
            for (int k = 0; k < 4; k++) ch[k] += cdx[k];
        }
    }
}

/* a disc, its middle and radius in pixels (16.16): the pixels whose
 * middles are inside it */
static void do_disc(int32_t cx, int32_t cy, int32_t r, Color c)
{
    int y0 = (cy - r) >> 16, y1 = (cy + r) >> 16;
    unsigned cr = COL_R(c), cg = COL_G(c), cb = COL_B(c), a = alpha256(c);
    if (r <= 0 || !a) return;
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < H; y++) {
        int32_t dy = ((y << 16) + 0x8000 - cy) >> 4; /* 12.12 */
        int32_t rr = r >> 4;
        int64_t h2 = (int64_t)rr * rr - (int64_t)dy * dy;
        if (h2 <= 0) continue;
        /* the half width: a square root in integers */
        uint32_t lo = 0, hi = (uint32_t)rr + 1;
        while (lo + 1 < hi) {
            uint32_t m = (lo + hi) / 2;
            if ((int64_t)m * m <= h2) lo = m;
            else hi = m;
        }
        int32_t half = (int32_t)lo << 4; /* 16.16 */
        int x0 = (cx - half + 0x7FFF) >> 16, x1 = (cx + half - 0x8000) >> 16;
        if (x0 < 0) x0 = 0;
        if (x1 >= W) x1 = W - 1;
        uint16_t *row = FB + y * W;
        for (int x = x0; x <= x1; x++) blend(row + x, cr, cg, cb, a);
    }
}

/* glyph ch of the font, 5x7 bits (font_glyph), each pixel px by py, the
 * colour from top to bottom down it */
static void do_glyph(int x, int y, int px, int py, const uint8_t rows[7], Color top, Color bottom)
{
    for (int r = 0; r < 7; r++) {
        if (!rows[r]) continue;
        /* the row's colour, at its middle (in integers: no soft float) */
        Color c = top;
        if (top != bottom) {
            unsigned t = (unsigned)(r * 2 + 1) * 256 / 14, ch[4];
            for (int k = 0; k < 4; k++) {
                int a = (int)((top >> (8 * k)) & 255), b = (int)((bottom >> (8 * k)) & 255);
                ch[k] = (unsigned)(a + ((b - a) * (int)t >> 8));
            }
            c = ch[0] | ch[1] << 8 | ch[2] << 16 | ch[3] << 24;
        }
        for (int col = 0; col < 5; col++)
            if (rows[r] & (1u << (4 - col))) {
                int c0 = col;
                while (col + 1 < 5 && (rows[r] & (1u << (3 - col)))) col++;
                do_rect(x + c0 * px, y + r * py, x + (col + 1) * px, y + (r + 1) * py, c, c);
            }
    }
}

/* ------------------------------------------------------------------ */
/* The queue                                                           */
/* ------------------------------------------------------------------ */

/*
 * What is drawn is queued, and drawn while the frames have time for it
 * (soft_run): a screen's worth takes a frame or two of the CPU, more than
 * any frame of the game leaves over. The picture goes to the screen when
 * the queue has drawn it all (soft_present, queued last).
 */
enum { OP_RECT, OP_TRI, OP_GLYPH, OP_DISC, OP_COPY, OP_PRESENT };
typedef struct {
    uint8_t op;
    uint8_t rows[7];
    uint16_t *fb;
    int32_t a[6];
    Color c[3];
} Cmd;

#define QUEUE 2048
static Cmd s_q[QUEUE];
static int s_head, s_tail;

static Cmd *push(int op)
{
    if (s_head - s_tail == QUEUE) soft_run(0xFFFFFFFFu); /* (full: drawn now) */
    Cmd *c = &s_q[s_head++ % QUEUE];
    c->op = (uint8_t)op;
    c->fb = FB;
    return c;
}

void soft_rect(int x0, int y0, int x1, int y1, Color top, Color bottom)
{
    Cmd *c = push(OP_RECT);
    c->a[0] = x0;
    c->a[1] = y0;
    c->a[2] = x1;
    c->a[3] = y1;
    c->c[0] = top;
    c->c[1] = bottom;
}

void soft_tri(const int16_t *xy, const Color *col)
{
    Cmd *c = push(OP_TRI);
    for (int i = 0; i < 6; i++) c->a[i] = xy[i];
    for (int i = 0; i < 3; i++) c->c[i] = col[i];
}

void soft_disc(int32_t cx, int32_t cy, int32_t r, Color col)
{
    Cmd *c = push(OP_DISC);
    c->a[0] = cx;
    c->a[1] = cy;
    c->a[2] = r;
    c->c[0] = col;
}

void soft_glyph(int x, int y, int px, int py, const uint8_t rows[7], Color top, Color bottom)
{
    Cmd *c = push(OP_GLYPH);
    c->a[0] = x;
    c->a[1] = y;
    c->a[2] = px;
    c->a[3] = py;
    memcpy(c->rows, rows, 7);
    c->c[0] = top;
    c->c[1] = bottom;
}

void soft_copy(uint16_t *dst, const uint16_t *src)
{
    Cmd *c = push(OP_COPY);
    c->fb = dst;
    c->a[0] = (int32_t)(intptr_t)src;
}

void soft_present(void)
{
    Cmd *c = push(OP_PRESENT);
    c->fb = s_back;
}

int soft_busy(void)
{
    return s_head != s_tail;
}

void soft_run(uint32_t until)
{
    while (s_tail != s_head && (until == 0xFFFFFFFFu || (int32_t)(until - nds_clock()) > 0)) {
        Cmd *c = &s_q[s_tail++ % QUEUE];
        uint16_t *keep = FB;
        FB = c->fb;
        switch (c->op) {
        case OP_RECT: do_rect(c->a[0], c->a[1], c->a[2], c->a[3], c->c[0], c->c[1]); break;
        case OP_TRI: {
            int16_t v[6];
            for (int i = 0; i < 6; i++) v[i] = (int16_t)c->a[i];
            do_tri(v, c->c);
            break;
        }
        case OP_GLYPH: do_glyph(c->a[0], c->a[1], c->a[2], c->a[3], c->rows, c->c[0], c->c[1]); break;
        case OP_DISC: do_disc(c->a[0], c->a[1], c->a[2], c->c[0]); break;
        case OP_COPY: memcpy(c->fb, (const void *)(intptr_t)c->a[0], sizeof(s_back)); break;
        case OP_PRESENT: s_present = 1; break;
        }
        FB = keep;
    }
}
