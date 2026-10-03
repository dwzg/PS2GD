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
    }
}

static unsigned char norm_char(char c)
{
    unsigned char ch = (unsigned char)c;
    if (ch >= 'a' && ch <= 'z') ch = (unsigned char)(ch - 'a' + 'A');
    return ch;
}

/* On a pixel grid (draw.h) font pixels are whole device pixels, the nearest
 * whole number to their size, and text starts on a device pixel; otherwise
 * small text would come out with strokes of uneven width. */
static float grid_size(float v, float grid)
{
    float n = floorf(v * grid + 0.5f);
    return (n < 1.0f ? 1.0f : n) / grid;
}

/* Size of one font pixel at a scale. */
static float pixel_size(float scale)
{
    float g = draw_pixel_grid();
    return g > 0.0f ? grid_size(scale, g) : scale;
}

float font_width(const char *s, float scale)
{
    int n = (int)strlen(s);
    if (n == 0) return 0.0f;
    return (n * 6 - 1) * pixel_size(scale);
}

float font_height(float scale)
{
    return 7 * pixel_size(scale);
}

typedef void (*RunFn)(float x0, float y0, float x1, float y1, int row, void *ctx);

/* Walk every horizontal run of set pixels in the string. */
static void for_each_run(float x, float y, float scale, int align, const char *s, RunFn fn, void *ctx)
{
    float px = pixel_size(scale);
    float w = font_width(s, scale);
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    x = grid_snap(x);
    y = grid_snap(y);
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
                fn(x + start * px, y + r * px, x + c * px, y + (r + 1) * px, r, ctx);
            }
        }
    }
}

typedef struct {
    Color top, bottom, outline;
    float o; /* outline width */
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
    gfx_rect(x0 - f->o, y0 - f->o, x1 + f->o, y1 + f->o, f->outline);
}

static void run_fill(float x0, float y0, float x1, float y1, int row, void *ctx)
{
    FancyCtx *f = (FancyCtx *)ctx;
    Color a = col_lerp(f->top, f->bottom, row / 7.0f);
    Color b = col_lerp(f->top, f->bottom, (row + 1) / 7.0f);
    gfx_rect_v(x0, y0, x1, y1, a, b);
}

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

/* The runs (grown by o) that span the band [ya, yb), as sorted x-intervals
 * merged where they overlap or touch. Returns how many. */
static int band_spans(const RunList *l, float o, float ya, float yb, float *iv)
{
    int k = 0;
    for (int i = 0; i < l->n; i++) {
        const Box *b = &l->b[i];
        if (b->y0 - o > ya || b->y1 + o < yb) continue;
        float x0 = b->x0 - o, x1 = b->x1 + o;
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

static void outline_once(const RunList *l, float o, Color c)
{
    static float ys[MAX_RUNS * 4], outer[MAX_RUNS * 2], inner[MAX_RUNS * 2];
    int ny = 0;
    for (int i = 0; i < l->n; i++) {
        ys[ny++] = l->b[i].y0 - o;
        ys[ny++] = l->b[i].y0;
        ys[ny++] = l->b[i].y1;
        ys[ny++] = l->b[i].y1 + o;
    }
    qsort(ys, (size_t)ny, sizeof(float), cmp_float);
    for (int j = 0; j + 1 < ny; j++) {
        float ya = ys[j], yb = ys[j + 1];
        if (yb <= ya) continue;
        int no = band_spans(l, o, ya, yb, outer), ni = band_spans(l, 0.0f, ya, yb, inner);
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
    for_each_run(x, y, scale, align, s, run_plain, &c);
}

void font_draw_fancy(float x, float y, float scale, Color top, Color bottom, Color outline,
                     float outline_px, int align, const char *s)
{
    FancyCtx f = {top, bottom, outline, grid_w(outline_px)};
    if (outline_px > 0.0f && COL_A(outline) < 255) {
        static RunList runs;
        runs.n = 0;
        for_each_run(x, y, scale, align, s, run_collect, &runs);
        outline_once(&runs, f.o, outline);
    } else if (outline_px > 0.0f) {
        for_each_run(x, y, scale, align, s, run_outline, &f);
    }
    for_each_run(x, y, scale, align, s, run_fill, &f);
}
