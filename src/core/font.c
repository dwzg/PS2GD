#include "font.h"
#include "gfx.h"

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

/*
 * Pixel grid text is drawn on, in device pixels per virtual pixel (0: none).
 * When the virtual screen is scaled down (the PSP's, see common.h), font
 * pixels become whole device pixels, the nearest whole number to their
 * size, and text starts on a device pixel; otherwise small text would come
 * out with strokes of uneven width.
 */
#ifdef PIXEL_GRID_X
static float s_grid_x = PIXEL_GRID_X, s_grid_y = PIXEL_GRID_Y;
#else
static float s_grid_x, s_grid_y;
#endif

void font_set_pixel_grid(float gx, float gy)
{
    s_grid_x = gx;
    s_grid_y = gy;
}

static float grid_size(float v, float grid)
{
    float n = floorf(v * grid + 0.5f);
    return (n < 1.0f ? 1.0f : n) / grid;
}

/* A sixty-fourth of a pixel past the pixel edge: rounding errors stay on
 * that side of it, so renderers that truncate positions (SDL's software
 * one) or round them to a sixteenth of a pixel (the PSP's) put the edge
 * right on it. */
static float grid_pos(float v, float grid)
{
    return (floorf(v * grid + 0.5f) + 1.0f / 64.0f) / grid;
}

/* Size of one font pixel at a scale, across and down. */
static void pixel_size(float scale, float *px, float *py)
{
    if (s_grid_x > 0.0f) {
        *px = grid_size(scale, s_grid_x);
        *py = grid_size(scale, s_grid_y);
    } else {
        *px = *py = scale;
    }
}

float font_width(const char *s, float scale)
{
    int n = (int)strlen(s);
    if (n == 0) return 0.0f;
    float px, py;
    pixel_size(scale, &px, &py);
    return (n * 6 - 1) * px;
}

float font_height(float scale)
{
    float px, py;
    pixel_size(scale, &px, &py);
    return 7 * py;
}

typedef void (*RunFn)(float x0, float y0, float x1, float y1, int row, void *ctx);

/* Walk every horizontal run of set pixels in the string. */
static void for_each_run(float x, float y, float scale, int align, const char *s, RunFn fn, void *ctx)
{
    float px, py;
    pixel_size(scale, &px, &py);
    float w = font_width(s, scale);
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    if (s_grid_x > 0.0f) {
        x = grid_pos(x, s_grid_x);
        y = grid_pos(y, s_grid_y);
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
    Color a = col_lerp(f->top, f->bottom, row / 7.0f);
    Color b = col_lerp(f->top, f->bottom, (row + 1) / 7.0f);
    gfx_rect_v(x0, y0, x1, y1, a, b);
}

void font_draw(float x, float y, float scale, Color c, int align, const char *s)
{
    for_each_run(x, y, scale, align, s, run_plain, &c);
}

void font_draw_fancy(float x, float y, float scale, Color top, Color bottom, Color outline,
                     float outline_px, int align, const char *s)
{
    FancyCtx f = {top, bottom, outline, outline_px, outline_px};
    if (outline_px > 0.0f && s_grid_x > 0.0f) {
        f.ox = grid_size(outline_px, s_grid_x);
        f.oy = grid_size(outline_px, s_grid_y);
    }
    if (outline_px > 0.0f) for_each_run(x, y, scale, align, s, run_outline, &f);
    for_each_run(x, y, scale, align, s, run_fill, &f);
}
