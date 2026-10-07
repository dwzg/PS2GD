#include <stdlib.h>

#include "font.h"
#include "draw.h"

/*
 * Each glyph: its character followed by 7 rows of 5 pixels ('#' = set).
 * Lowercase letters are rendered with the uppercase shapes.
 */
static const char *const FONT_SRC[] = {
    "A", ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#",
    "B", "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####.",
    "C", ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###.",
    "D", "####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####.",
    "E", "#####", "#....", "#....", "####.", "#....", "#....", "#####",
    "F", "#####", "#....", "#....", "####.", "#....", "#....", "#....",
    "G", ".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####",
    "H", "#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#",
    "I", ".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###.",
    "J", "..###", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##..",
    "K", "#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#",
    "L", "#....", "#....", "#....", "#....", "#....", "#....", "#####",
    "M", "#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#",
    "N", "#...#", "#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#",
    "O", ".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.",
    "P", "####.", "#...#", "#...#", "####.", "#....", "#....", "#....",
    "Q", ".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#",
    "R", "####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#",
    "S", ".####", "#....", "#....", ".###.", "....#", "....#", "####.",
    "T", "#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#..",
    "U", "#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###.",
    "V", "#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#..",
    "W", "#...#", "#...#", "#...#", "#.#.#", "#.#.#", "#.#.#", ".#.#.",
    "X", "#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#",
    "Y", "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#..",
    "Z", "#####", "....#", "...#.", "..#..", ".#...", "#....", "#####",
    "0", ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###.",
    "1", "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###.",
    "2", ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####",
    "3", "#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###.",
    "4", "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#.",
    "5", "#####", "#....", "####.", "....#", "....#", "#...#", ".###.",
    "6", "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###.",
    "7", "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#...",
    "8", ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###.",
    "9", ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##..",
    ".", ".....", ".....", ".....", ".....", ".....", ".##..", ".##..",
    ",", ".....", ".....", ".....", ".....", ".##..", "..#..", ".#...",
    "!", "..#..", "..#..", "..#..", "..#..", "..#..", ".....", "..#..",
    "?", ".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#..",
    ":", ".....", ".##..", ".##..", ".....", ".##..", ".##..", ".....",
    ";", ".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#...",
    "-", ".....", ".....", ".....", "#####", ".....", ".....", ".....",
    "+", ".....", "..#..", "..#..", "#####", "..#..", "..#..", ".....",
    "/", "....#", "....#", "...#.", "..#..", ".#...", "#....", "#....",
    "%", "##..#", "##..#", "...#.", "..#..", ".#...", "#..##", "#..##",
    "(", "...#.", "..#..", ".#...", ".#...", ".#...", "..#..", "...#.",
    ")", ".#...", "..#..", "...#.", "...#.", "...#.", "..#..", ".#...",
    "'", "..#..", "..#..", ".#...", ".....", ".....", ".....", ".....",
    "\"", ".#.#.", ".#.#.", ".....", ".....", ".....", ".....", ".....",
    "=", ".....", ".....", "#####", ".....", "#####", ".....", ".....",
    "<", "...#.", "..#..", ".#...", "#....", ".#...", "..#..", "...#.",
    ">", ".#...", "..#..", "...#.", "....#", "...#.", "..#..", ".#...",
    "_", ".....", ".....", ".....", ".....", ".....", ".....", "#####",
    "#", ".#.#.", ".#.#.", "#####", ".#.#.", "#####", ".#.#.", ".#.#.",
    "*", ".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", ".....",
    "&", ".##..", "#..#.", "#.#..", ".#...", "#.#.#", "#..#.", ".##.#",
    "\x01", ".....", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", ".....",
    "\x02", ".....", ".###.", "#...#", "#...#", "#...#", ".###.", ".....",
    "\x03", ".....", "#####", "#...#", "#...#", "#...#", "#####", ".....",
    "\x04", ".....", "..#..", ".#.#.", ".#.#.", "#...#", "#####", ".....",
    "\x05", "..#..", "..#..", "#####", ".###.", ".###.", ".#.#.", "#...#",
    "\x06", "...#.", "..##.", ".###.", "####.", ".###.", "..##.", "...#.",
    "\x07", ".#...", ".##..", ".###.", ".####", ".###.", ".##..", ".#...",
    NULL,
};

static uint8_t s_glyph[128][7];
static uint8_t s_has[128];
#ifdef GFX_DEVICE_RECTS
/* each glyph's runs (font.c's for_each_run_dev): row << 4 | first column,
 * then the column after the last; at most 3 runs a row */
static uint8_t s_runs[128][7 * 3][2];
static uint8_t s_nruns[128];
#endif

void font_init(void)
{
    memset(s_glyph, 0, sizeof(s_glyph));
    memset(s_has, 0, sizeof(s_has));
    for (int i = 0; FONT_SRC[i]; i += 8) {
        unsigned char ch = (unsigned char)FONT_SRC[i][0];
        if (ch >= 128) continue;
        for (int r = 0; r < 7; r++) {
            uint8_t bits = 0;
            for (int c = 0; c < 5; c++)
                if (FONT_SRC[i + 1 + r][c] == '#') bits |= (uint8_t)(1u << (4 - c));
            s_glyph[ch][r] = bits;
        }
        s_has[ch] = 1;
#ifdef GFX_DEVICE_RECTS
        s_nruns[ch] = 0;
        for (int r = 0; r < 7; r++) {
            int c = 0;
            while (c < 5) {
                if (!(s_glyph[ch][r] & (1u << (4 - c)))) { c++; continue; }
                int start = c;
                while (c < 5 && (s_glyph[ch][r] & (1u << (4 - c)))) c++;
                s_runs[ch][s_nruns[ch]][0] = (uint8_t)(r << 4 | start);
                s_runs[ch][s_nruns[ch]][1] = (uint8_t)c;
                s_nruns[ch]++;
            }
        }
#endif
    }
}

static unsigned char norm_char(char c)
{
    unsigned char ch = (unsigned char)c;
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
#if FACE_BUTTON_LETTERS
    /* the console's face buttons are letters (the DS's A, B, Y, X: target.h) */
    if (ch >= 1 && ch <= 4) ch = (unsigned char)"ABYX"[ch - 1];
#endif
    return ch;
}

int font_glyph(char c, uint8_t rows[7])
{
    unsigned char ch = norm_char(c);
    if (ch >= 128 || !s_has[ch]) return 0;
    memcpy(rows, s_glyph[ch], 7);
    return 1;
}

/* On a pixel grid (draw.h) font pixels are whole device pixels, the nearest
 * whole number to their size (across and down, where the grid differs), and
 * text starts on a device pixel; otherwise small text would come out with
 * strokes of uneven width. */
float font_pixel(float scale)
{
    return grid_w(scale);
}

float font_pixel_y(float scale)
{
    return grid_h(scale);
}

float font_width(const char *s, float scale)
{
    int n = (int)strlen(s);
    if (n == 0) return 0.0f;
    return (n * 6 - 1) * font_pixel(scale);
}

float font_height(float scale)
{
    return 7 * font_pixel_y(scale);
}

float font_center_y(float y0, float y1, float scale)
{
    return (y0 + y1) * 0.5f - font_height(scale) * 0.5f;
}

typedef void (*RunFn)(float x0, float y0, float x1, float y1, int row, void *ctx);

/* Walk every horizontal run of set pixels in the string, font pixels px
 * wide and py tall; on the pixel grid (snap) or off it. */
static void for_each_run(float x, float y, float px, float py, int snap, int align, const char *s, RunFn fn,
                         void *ctx)
{
    int n = (int)strlen(s);
    float w = n > 0 ? (n * 6 - 1) * px : 0.0f;
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    if (snap) {
        x = grid_snap(x);
        y = grid_snap_y(y);
    }
    for (; *s; s++, x += 6 * px) {
        unsigned char ch = norm_char(*s);
        if (ch >= 128 || !s_has[ch]) continue;
        for (int r = 0; r < 7; r++) {
            uint8_t bits = s_glyph[ch][r];
            int c = 0;
            while (c < 5) {
                if (!(bits & (1u << (4 - c)))) { c++; continue; }
                int start = c;
                while (c < 5 && (bits & (1u << (4 - c)))) c++;
                fn(x + start * px, y + r * py, x + c * px, y + (r + 1) * py, r, ctx);
            }
        }
    }
}

typedef struct {
    Color top, bottom, outline;
    float ox, oy; /* outline width across and down */
    Color row[8]; /* the fill's colour at the top of each row, and at the bottom */
    int dox, doy; /* the outline's width in device pixels (for_each_run_dev) */
} FancyCtx;

static void run_plain(float x0, float y0, float x1, float y1, int row, void *ctx)
{
    (void)row;
    gfx_rect(x0, y0, x1, y1, *(Color *)ctx);
}

static void run_outline(float x0, float y0, float x1, float y1, int row, void *ctx)
{
    FancyCtx *f = (FancyCtx *)ctx;
    (void)row;
    gfx_rect(x0 - f->ox, y0 - f->oy, x1 + f->ox, y1 + f->oy, f->outline);
}

static void run_fill(float x0, float y0, float x1, float y1, int row, void *ctx)
{
    FancyCtx *f = (FancyCtx *)ctx;
    gfx_rect_v(x0, y0, x1, y1, f->row[row], f->row[row + 1]);
}

#ifdef GFX_DEVICE_RECTS
/*
 * Text on the pixel grid, for a backend that draws in device pixels
 * (gfx_rect_dev, gfx_glyph_dev, gfx.h; the DS's): the glyphs whole, and
 * their outlines as the runs of for_each_run grown, worked out in whole
 * device pixels, in integers. Where floating point is done in software,
 * text took half a menu's frame otherwise, and a rectangle a run took
 * more polygons than the DS draws on the options screen. x and y are
 * already on the grid; px and py whole device pixels (font_pixel).
 */
typedef void (*DevRunFn)(int x0, int y0, int x1, int y1, int row, const FancyCtx *f);

static void for_each_run_dev(float x, float y, float px, float py, const char *s, DevRunFn fn, const FancyCtx *f)
{
    float gx = draw_pixel_grid(), gy = draw_pixel_grid_y();
    int X = (int)floorf(x * gx), Y = (int)floorf(y * gy);
    int PX = (int)(px * gx + 0.5f), PY = (int)(py * gy + 0.5f);
    for (; *s; s++, X += 6 * PX) {
        unsigned char ch = norm_char(*s);
        if (ch >= 128) continue;
        for (int k = 0; k < s_nruns[ch]; k++) {
            int r = s_runs[ch][k][0] >> 4, start = s_runs[ch][k][0] & 15, end = s_runs[ch][k][1];
            fn(X + start * PX, Y + r * PY, X + end * PX, Y + (r + 1) * PY, r, f);
        }
    }
}

/* the glyphs' outlines whole (gfx_glyph_outline_dev); 0 if the backend
 * didn't take them */
static int outlines_dev(float x, float y, float px, float py, const char *s, const FancyCtx *f)
{
    float gx = draw_pixel_grid(), gy = draw_pixel_grid_y();
    int X = (int)floorf(x * gx), Y = (int)floorf(y * gy);
    int PX = (int)(px * gx + 0.5f), PY = (int)(py * gy + 0.5f);
    for (const char *c = s; *c; c++, X += 6 * PX) {
        unsigned char ch = norm_char(*c);
        if (ch < 128 && s_nruns[ch] && !gfx_glyph_outline_dev(X, Y, PX, PY, f->dox, f->doy, ch, f->outline)) return 0;
    }
    return 1;
}

/* the glyphs whole, coloured from top to bottom (gfx_glyph_dev) */
static void glyphs_dev(float x, float y, float px, float py, const char *s, Color top, Color bottom)
{
    float gx = draw_pixel_grid(), gy = draw_pixel_grid_y();
    int X = (int)floorf(x * gx), Y = (int)floorf(y * gy);
    int PX = (int)(px * gx + 0.5f), PY = (int)(py * gy + 0.5f);
    for (; *s; s++, X += 6 * PX) {
        unsigned char ch = norm_char(*s);
        if (ch < 128 && s_nruns[ch]) gfx_glyph_dev(X, Y, PX, PY, ch, top, bottom);
    }
}

static void dev_outline(int x0, int y0, int x1, int y1, int row, const FancyCtx *f)
{
    (void)row;
    gfx_rect_dev(x0 - f->dox, y0 - f->doy, x1 + f->dox, y1 + f->doy, f->outline, f->outline);
}


/* Where the text starts (aligned, on the grid), as for_each_run has it;
 * returns 0 when there is no grid to draw on. */
static int dev_origin(float *x, float *y, float px, int align, const char *s)
{
    if (draw_pixel_grid() <= 0.0f || draw_pixel_grid_y() <= 0.0f) return 0;
    int n = (int)strlen(s);
    float w = n > 0 ? (n * 6 - 1) * px : 0.0f;
    if (align == ALIGN_CENTER) *x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) *x -= w;
    *x = grid_snap(*x);
    *y = grid_snap_y(*y);
    return 1;
}
#endif

/*
 * A translucent outline (text fading out) can't be drawn as one rectangle
 * around each run: it would show darker where those overlap and under the
 * translucent fill. It is cut instead, band by band, into rectangles that
 * cover the outline once and leave the fill out.
 */
#define MAX_RUNS 512 /* plenty for a line of text */

typedef struct {
    float x0, y0, x1, y1;
} Box;

typedef struct {
    Box b[MAX_RUNS];
    int n;
} RunList;

static void run_collect(float x0, float y0, float x1, float y1, int row, void *ctx)
{
    RunList *l = (RunList *)ctx;
    (void)row;
    if (l->n < MAX_RUNS) l->b[l->n++] = (Box){x0, y0, x1, y1};
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* The runs (grown by ox across, oy down) that span the band [ya, yb), as
 * sorted x-intervals merged where they overlap or touch. Returns how many. */
static int band_spans(const RunList *l, float ox, float oy, float ya, float yb, float *iv)
{
    int k = 0;
    for (int i = 0; i < l->n; i++) {
        const Box *b = &l->b[i];
        if (b->y0 - oy > ya || b->y1 + oy < yb) continue;
        float x0 = b->x0 - ox, x1 = b->x1 + ox;
        int j = k++; /* insertion sort by start */
        while (j > 0 && iv[(j - 1) * 2] > x0) {
            iv[j * 2] = iv[(j - 1) * 2];
            iv[j * 2 + 1] = iv[(j - 1) * 2 + 1];
            j--;
        }
        iv[j * 2] = x0;
        iv[j * 2 + 1] = x1;
    }
    int m = 0;
    for (int i = 0; i < k; i++) {
        if (m > 0 && iv[i * 2] <= iv[(m - 1) * 2 + 1]) {
            iv[(m - 1) * 2 + 1] = maxf(iv[(m - 1) * 2 + 1], iv[i * 2 + 1]);
        } else {
            iv[m * 2] = iv[i * 2];
            iv[m * 2 + 1] = iv[i * 2 + 1];
            m++;
        }
    }
    return m;
}

static void outline_once(const RunList *l, float ox, float oy, Color c)
{
    static float ys[MAX_RUNS * 4], outer[MAX_RUNS * 2], inner[MAX_RUNS * 2];
    int ny = 0;
    for (int i = 0; i < l->n; i++) {
        ys[ny++] = l->b[i].y0 - oy;
        ys[ny++] = l->b[i].y0;
        ys[ny++] = l->b[i].y1;
        ys[ny++] = l->b[i].y1 + oy;
    }
    qsort(ys, (size_t)ny, sizeof(float), cmp_float);
    for (int j = 0; j + 1 < ny; j++) {
        float ya = ys[j], yb = ys[j + 1];
        if (yb <= ya) continue;
        int no = band_spans(l, ox, oy, ya, yb, outer), ni = band_spans(l, 0.0f, 0.0f, ya, yb, inner);
        for (int a = 0, i = 0; a < no; a++) {
            float x = outer[a * 2], end = outer[a * 2 + 1];
            for (; i < ni && inner[i * 2] < end; i++) {
                if (inner[i * 2] > x) gfx_rect(x, ya, inner[i * 2], yb, c);
                x = maxf(x, inner[i * 2 + 1]);
            }
            if (x < end) gfx_rect(x, ya, end, yb, c);
        }
    }
}

void font_draw(float x, float y, float scale, Color c, int align, const char *s)
{
    float px = font_pixel(scale), py = font_pixel_y(scale);
#ifdef GFX_DEVICE_RECTS
    if (dev_origin(&x, &y, px, align, s)) {
        glyphs_dev(x, y, px, py, s, c, c);
        return;
    }
#endif
    for_each_run(x, y, px, py, 1, align, s, run_plain, &c);
}

static void fancy(float x, float y, float px, float py, int snap, Color top, Color bottom, Color outline,
                  float outline_px, float ox, float oy, int align, const char *s)
{
    FancyCtx f = {top, bottom, outline, ox, oy, {0}, 0, 0};
    for (int r = 0; r <= 7; r++) f.row[r] = col_lerp(top, bottom, r / 7.0f);
#ifdef GFX_DEVICE_RECTS
    {
        float dx = x, dy = y;
        int see_through = outline_px > 0.0f && COL_A(outline) < 255;
        if (snap && dev_origin(&dx, &dy, px, align, s)) {
            f.dox = (int)floorf(ox * draw_pixel_grid() + 0.5f);
            f.doy = (int)floorf(oy * draw_pixel_grid_y() + 0.5f);
            /* the outlines whole where the backend takes them; else as
             * rectangles, or, see-through, as below */
            int done = outline_px <= 0.0f || outlines_dev(dx, dy, px, py, s, &f);
            if (!done && !see_through) {
                for_each_run_dev(dx, dy, px, py, s, dev_outline, &f);
                done = 1;
            }
            if (done) {
                glyphs_dev(dx, dy, px, py, s, top, bottom);
                return;
            }
        }
    }
#endif
    if (outline_px > 0.0f && COL_A(outline) < 255) {
        static RunList runs;
        runs.n = 0;
        for_each_run(x, y, px, py, snap, align, s, run_collect, &runs);
        outline_once(&runs, f.ox, f.oy, outline);
    } else if (outline_px > 0.0f) {
        for_each_run(x, y, px, py, snap, align, s, run_outline, &f);
    }
    for_each_run(x, y, px, py, snap, align, s, run_fill, &f);
}

void font_draw_fancy(float x, float y, float scale, Color top, Color bottom, Color outline,
                     float outline_px, int align, const char *s)
{
    fancy(x, y, font_pixel(scale), font_pixel_y(scale), 1, top, bottom, outline, outline_px, grid_w(outline_px),
          grid_h(outline_px), align, s);
}

void font_draw_fancy_grown(float x, float y, float scale, float k, Color top, Color bottom, Color outline,
                           float outline_px, int align, const char *s)
{
    /* its pixels and outline as font_draw_fancy draws them, times k */
    float fx = font_pixel(scale), fy = font_pixel_y(scale), px = fx * k, py = fy * k;
    fancy(x, y, px, py, 0, top, bottom, outline, outline_px * k, grid_w(outline_px) * px / fx,
          grid_h(outline_px) * py / fy, align, s);
}
