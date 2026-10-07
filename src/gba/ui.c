/*
 * Text and menus on BG0 (ui.h).
 *
 * The surface is 512x160 pixels, a colour (bank * 16 + index) each;
 * drawing marks the 8x8 cells it touches. ui_flush packs the changed
 * cells: an empty one shows the blank tile, one of a single colour a tile
 * shared by every cell of that colour, any other a tile of its own from
 * the shared pool (video.h), freed when the cell no longer needs it. The
 * tiles and the map are kept in RAM too and copied in the vertical blank,
 * the tiles' changed range at once.
 */
#include <string.h>

#include "ui.h"
#include "video.h"
#include "world.h"
#include "sprites.h"
#include "gen.h"
#include "frame.h"

#define CW (UI_W / 8)
#define CH (SCR_H / 8)
#define UI_TILE_FIRST WORLD_TILES_END
#define UI_TILES (SHARED_TILE_END - UI_TILE_FIRST)
#define NO_TILE 0xFFFF

static uint8_t s_cv[SCR_H][UI_W] ALIGN4;
/* where drawing goes: the surface, or the sprite text's */
static uint8_t *s_t = &s_cv[0][0];
static int s_tw = UI_W, s_th = SCR_H;
#define PX(x, y) s_t[(y) * s_tw + (x)]
static uint32_t s_dirty[CH][2] IWRAM_BSS; /* a bit a cell: columns 0-31, 32-63 */
static uint16_t s_map[2][32 * 32] ALIGN4; /* BG0's two screen blocks: columns 0-31, 32-63 */
static uint32_t s_tiles[UI_TILES][8] ALIGN4;
static uint16_t s_cell[CH][CW];           /* the cell's own tile, or NO_TILE */
static uint16_t s_solid[256];             /* the tile of each single colour, or NO_TILE */
static uint16_t s_free[UI_TILES];
static int s_nfree, s_lo, s_hi, s_map_dirty, s_any_dirty, s_scroll;
/* ui_hold: 0 none, 1 until ui_show, 2 until all is packed; ui_show(1):
 * all to be packed this frame */
static int s_hold, s_pack_all;
/* a font row (bit 4 the leftmost) as pixels, bit 0 the leftmost; and
 * twice as wide (ui_init) */
static uint8_t s_font_bits[32] IWRAM_BSS;
static uint16_t s_font_bits2[32] IWRAM_BSS;
static struct {
    int n, x0[2], y0[2], x1[2], y1[2], level;
} s_dim;

/* UI palettes: per bank, indices 1-3 the same everywhere */
static const uint8_t PAL[UB_COUNT - UB_WHITE][16][3] = {
    /* white */ {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
                 {255, 255, 255}, {245, 248, 255}, {235, 242, 255}, {225, 236, 255}, {216, 229, 255}, {208, 224, 255}, {200, 220, 255},
                 {200, 220, 255}, {160, 170, 190}, {255, 255, 255}, {120, 255, 150}, {110, 120, 140}},
    /* gold; 15: gray (the label beside a gold value, menus.c) */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {255, 250, 200}, {255, 236, 168}, {255, 222, 136}, {255, 208, 104}, {255, 194, 80}, {255, 182, 60}, {255, 170, 40},
     {255, 240, 160}, {255, 206, 52}, {255, 230, 100}, {120, 70, 0}, {160, 170, 190}},
    /* green; 12: a chosen item's fill; 15: gold (what else is in a chosen
     * row, menus.c) */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {255, 255, 255}, {230, 255, 236}, {205, 255, 216}, {180, 255, 196}, {164, 255, 184}, {157, 255, 177}, {150, 255, 170},
     {120, 255, 150}, {48, 52, 65}, {160, 255, 170}, {80, 255, 120}, {255, 240, 160}},
    /* gray */ {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
                {170, 180, 200}, {160, 170, 190}, {150, 160, 180}, {140, 150, 170}, {130, 140, 160}, {120, 130, 150}, {110, 120, 140},
                {170, 180, 200}, {120, 128, 146}, {200, 210, 230}, {60, 66, 80}, {40, 44, 56}},
    /* bars: green from 4 (bright at 7), blue from 9; 8 and 13 their highlights; 14 the inside */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {90, 255, 120}, {130, 255, 155}, {170, 255, 185}, {210, 255, 210}, {160, 255, 180},
     {80, 200, 255}, {125, 215, 255}, {165, 228, 255}, {210, 240, 255}, {160, 225, 255}, {40, 46, 62}, {255, 255, 255}},
    /* red */ {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
               {255, 200, 200}, {255, 180, 180}, {255, 160, 160}, {255, 140, 140}, {255, 130, 130}, {255, 125, 125}, {255, 120, 120},
               {255, 120, 120}, {200, 80, 80}, {255, 255, 255}, {255, 230, 90}, {90, 255, 140}},
    /* the difficulties: 4.. as difficulty_color (theme.c) */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {90, 220, 255}, {90, 255, 120}, {255, 220, 60}, {255, 140, 40}, {255, 70, 160}, {255, 40, 40},
     {255, 255, 255}, {10, 10, 16}, {200, 200, 200}, {120, 120, 120}, {60, 60, 60}, {30, 30, 30}},
    /* chosen: white to pale yellow */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {255, 255, 255}, {255, 252, 240}, {255, 250, 226}, {255, 247, 210}, {255, 245, 196}, {255, 242, 178}, {255, 240, 160},
     {255, 255, 255}, {200, 200, 200}, {255, 240, 160}, {255, 255, 255}, {255, 255, 255}},
    /* pulsing hints */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {255, 255, 255}, {245, 248, 255}, {235, 242, 255}, {225, 236, 255}, {216, 229, 255}, {208, 224, 255}, {200, 220, 255},
     {255, 255, 255}, {160, 170, 190}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}},
    /* the lights (4..7 as they flash) */
    {{0}, {10, 14, 30}, {150, 156, 176}, {0, 0, 0},
     {45, 48, 58}, {45, 48, 58}, {45, 48, 58}, {45, 48, 58}, {0}, {0}, {0},
     {255, 255, 255}, {0}, {0}, {0}, {0}},
};

TextStyle ui_style(int bank, int fancy, uint8_t plain)
{
    TextStyle st;
    int r;
    for (r = 0; r < 7; r++) st.row[r] = UI(bank, fancy ? UI_G0 + r : (plain ? plain : UI_TEXT));
    st.outline = fancy ? UI(bank, UI_K) : 0;
    return st;
}

void ui_init(void)
{
    int b, i;
    for (i = 0; i < 32; i++) {
        int k;
        s_font_bits[i] = s_font_bits2[i] = 0;
        for (k = 0; k < 5; k++)
            if (i & (0x10 >> k)) {
                s_font_bits[i] |= (uint8_t)(1 << k);
                s_font_bits2[i] |= (uint16_t)(3 << (2 * k));
            }
    }
    for (b = UB_WHITE; b < UB_COUNT; b++)
        for (i = 1; i < 16; i++)
            g_pal_bg[b * 16 + i] = RGB15(PAL[b - UB_WHITE][i][0], PAL[b - UB_WHITE][i][1], PAL[b - UB_WHITE][i][2]);
    ui_clear();
    /* the whole map once (ui_flush copies the rows on screen) */
    dma3_copy32(SCREENBLOCK(SB_TEXT), s_map, sizeof(s_map) / 4);
}

/* bytes c from p to p + n, a word at a time where it can */
IWRAM_CODE static void fill(uint8_t *p, int n, uint8_t c)
{
    uint32_t w = c * 0x01010101u, *q;
    while (n > 0 && ((uintptr_t)p & 3)) {
        *p++ = c;
        n--;
    }
    q = (uint32_t *)p;
    for (; n >= 16; n -= 16, q += 4) q[0] = q[1] = q[2] = q[3] = w;
    for (; n >= 4; n -= 4) *q++ = w;
    p = (uint8_t *)q;
    while (n-- > 0) *p++ = c;
}

/* The surface is emptied lazily: ui_clear marks the cells drawn into
 * since the one before stale (a bit each, as s_dirty), and a cell's pixels
 * are emptied when something is next drawn into it (fresh, which every
 * drawing calls first), not all at once (emptying a screen's text took a
 * third of a frame). A stale cell is never packed: only one drawn into is
 * marked changed. A panel's or a fill's inside is left so too (solid_cells),
 * its cells stale in its colour: tinted, s_tint. */
static uint32_t s_stale[CH][2], s_used[CH][2], s_tinted[CH][2];
static uint8_t s_tint[CH][CW];
/* the cells whose map entry is not the empty tile's (or may not be) */
static uint32_t s_shown[CH][2];

IWRAM_CODE static void fresh(int x, int y, int w, int h)
{
    int cx0, cy0, cx1, cy1, cx, cy, r;
    if (s_t != &s_cv[0][0] || w <= 0 || h <= 0 || x >= UI_W || y >= SCR_H) return;
    cx0 = x < 0 ? 0 : x >> 3;
    cy0 = y < 0 ? 0 : y >> 3;
    cx1 = (x + w - 1) >> 3;
    cy1 = (y + h - 1) >> 3;
    if (cx1 >= CW) cx1 = CW - 1;
    if (cy1 >= CH) cy1 = CH - 1;
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) {
            uint32_t bit = 1u << (cx & 31);
            if (s_stale[cy][cx >> 5] & bit) {
                uint32_t *p = (uint32_t *)&s_cv[cy * 8][cx * 8];
                uint32_t c = s_tinted[cy][cx >> 5] & bit ? s_tint[cy][cx] * 0x01010101u : 0;
                for (r = 0; r < 8; r++, p += UI_W / 4) p[0] = p[1] = c;
                s_stale[cy][cx >> 5] &= ~bit;
            }
            s_used[cy][cx >> 5] |= bit;
        }
}

/* cell rows [cy0, cy1): what was drawn in them stale */
static void stale_rows(int cy0, int cy1)
{
    int cy;
    for (cy = cy0; cy < cy1; cy++) {
        s_stale[cy][0] |= s_used[cy][0];
        s_stale[cy][1] |= s_used[cy][1];
        s_used[cy][0] = s_used[cy][1] = 0;
        s_tinted[cy][0] = s_tinted[cy][1] = 0;
    }
}

/* (in IWRAM, with word fills: a new overlay over a running level clears
 * all of it in the frame) */
IWRAM_CODE void ui_clear(void)
{
    uint32_t *m = (uint32_t *)&s_map[0][0], blank = SHARED_TILE_FIRST * 0x10001u;
    int i;
    stale_rows(0, CH);
    fill((uint8_t *)s_dirty, sizeof(s_dirty), 0);
    for (i = 0; i < 32 * 32; i++) m[i] = blank;
    fill((uint8_t *)s_shown, sizeof(s_shown), 0);
    fill((uint8_t *)s_cell, sizeof(s_cell), 0xFF);
    fill((uint8_t *)s_solid, sizeof(s_solid), 0xFF);
    s_nfree = UI_TILES;
    for (i = 0; i < UI_TILES; i++) s_free[i] = (uint16_t)(UI_TILES - 1 - i);
    s_lo = UI_TILES;
    s_hi = 0;
    s_map_dirty = 3;
    s_any_dirty = 0;
    s_scroll = 0;
    s_dim.n = 0;
    s_hold = s_pack_all = 0;
}

void ui_hold(int until_packed)
{
    s_hold = until_packed ? 2 : 1;
}

void ui_show(int pack_all)
{
    s_hold = 0;
    s_pack_all = pack_all;
}

int ui_pending(void)
{
    int cy, n = 0;
    if (s_any_dirty)
        for (cy = 0; cy < CH; cy++) n += __builtin_popcount(s_dirty[cy][0]) + __builtin_popcount(s_dirty[cy][1]);
    return n;
}

static void mark(int x, int y, int w, int h)
{
    int cx0, cy0, cx1, cy1, cx, cy;
    if (w <= 0 || h <= 0 || s_t != &s_cv[0][0] || x >= UI_W || y >= SCR_H) return;
    cx0 = x < 0 ? 0 : x / 8;
    cy0 = y < 0 ? 0 : y / 8;
    cx1 = (x + w - 1) / 8;
    cy1 = (y + h - 1) / 8;
    if (cx1 >= CW) cx1 = CW - 1;
    if (cy1 >= CH) cy1 = CH - 1;
    for (cx = cx0; cx <= cx1; cx++) {
        uint32_t bit = 1u << (cx & 31);
        for (cy = cy0; cy <= cy1; cy++) s_dirty[cy][cx >> 5] |= bit;
    }
    s_any_dirty = 1;
}

/* clip a rectangle to the surface */
static int clip(int *x, int *y, int *w, int *h)
{
    if (*x < 0) {
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > s_tw) *w = s_tw - *x;
    if (*y + *h > s_th) *h = s_th - *y;
    return *w > 0 && *h > 0;
}

static void solid_cells(int x0, int y0, int x1, int y1, uint8_t c);

/* A fill's inside (a panel's, an erase): its cells are left out of the
 * drawing, their pixels filled lazily (solid_cells makes them stale, in its
 * colour), in pixels, whole cells; none if x1 <= x0 */
static struct {
    int x0, y0, x1, y1;
} s_skip;

static void skip_inside(int x0, int y0, int x1, int y1, uint8_t c)
{
    int cx0 = (x0 + 7) >> 3, cx1 = x1 >> 3, cy0 = (y0 + 7) >> 3, cy1 = y1 >> 3;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > CW) cx1 = CW;
    if (cy1 > CH) cy1 = CH;
    if (s_t != &s_cv[0][0] || cx0 >= cx1 || cy0 >= cy1 || (c && s_solid[c] == NO_TILE && !s_nfree)) {
        s_skip.x0 = s_skip.x1 = 0;
        return;
    }
    s_skip.x0 = cx0 * 8;
    s_skip.x1 = cx1 * 8;
    s_skip.y0 = cy0 * 8;
    s_skip.y1 = cy1 * 8;
}

/* fresh for [x, x + w) x [y, y + h) but s_skip's cells */
static void fresh_around(int x, int y, int w, int h)
{
    if (s_skip.x1 <= s_skip.x0) {
        fresh(x, y, w, h);
        return;
    }
    fresh(x, y, w, s_skip.y0 - y);
    fresh(x, s_skip.y1, w, y + h - s_skip.y1);
    fresh(x, s_skip.y0, s_skip.x0 - x, s_skip.y1 - s_skip.y0);
    fresh(s_skip.x1, s_skip.y0, x + w - s_skip.x1, s_skip.y1 - s_skip.y0);
}

/* a row from x0 to x1 (excluded) but s_skip's cells, clipped */
static void span(int y, int x0, int x1, uint8_t c);
static void span_around(int y, int x0, int x1, uint8_t c)
{
    if (s_skip.x1 > s_skip.x0 && y >= s_skip.y0 && y < s_skip.y1) {
        span(y, x0, s_skip.x0, c);
        span(y, s_skip.x1, x1, c);
    } else {
        span(y, x0, x1, c);
    }
}

void ui_fill(int x, int y, int w, int h, uint8_t c)
{
    int j;
    if (!clip(&x, &y, &w, &h)) return;
    skip_inside(x, y, x + w, y + h, c);
    fresh_around(x, y, w, h);
    for (j = 0; j < h; j++) span_around(y + j, x, x + w, c);
    mark(x, y, w, h);
    solid_cells(x, y, x + w, y + h, c);
}

void ui_fill_n(int x, int y, int w, int h, int step, int n, uint8_t c)
{
    int bw = (n - 1) * step + w, j, k;
    if (n <= 0 || w <= 0 || h <= 0) return;
    /* (none of them covers a cell: written as they are, no solid cells) */
    fresh(x, y, bw, h);
    for (j = 0; j < h; j++)
        for (k = 0; k < n; k++) span(y + j, x + k * step, x + k * step + w, c);
    mark(x, y, bw, h);
}

void ui_erase(int x, int y, int w, int h)
{
    ui_fill(x, y, w, h, 0);
}

static inline void put(int x, int y, uint8_t c)
{
    if ((unsigned)x < (unsigned)s_tw && (unsigned)y < (unsigned)s_th) PX(x, y) = c;
}

/* a row from x0 to x1 (excluded), clipped */
static void span(int y, int x0, int x1, uint8_t c)
{
    if ((unsigned)y >= (unsigned)s_th) return;
    if (x0 < 0) x0 = 0;
    if (x1 > s_tw) x1 = s_tw;
    if (x1 > x0) fill(&PX(x0, y), x1 - x0, c);
}

void ui_panel(int x0, int y0, int x1, int y1, uint8_t fill, uint8_t edge)
{
    /* corners rounded by 3 pixels: the first rows cut in by 2 and 1 */
    int y;
    skip_inside(x0 + 1, y0 + 2, x1 - 1, y1 - 2, fill);
    fresh_around(x0, y0, x1 - x0, y1 - y0);
    for (y = y0; y < y1; y++) {
        int r = y - y0 < y1 - 1 - y ? y - y0 : y1 - 1 - y;
        if (r == 0) {
            span(y, x0 + 2, x1 - 2, edge);
        } else if (r == 1) {
            put(x0 + 1, y, edge);
            put(x1 - 2, y, edge);
            span(y, x0 + 2, x1 - 2, fill);
        } else {
            put(x0, y, edge);
            put(x1 - 1, y, edge);
            span_around(y, x0 + 1, x1 - 1, fill);
        }
    }
    mark(x0, y0, x1 - x0, y1 - y0);
    solid_cells(x0 + 1, y0 + 2, x1 - 1, y1 - 2, fill);
}

int ui_text_w(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? (n * 6 - 1) * scale : 0;
}

/* bit i of 4 as byte i of a word */
static const uint32_t s_nibble_bytes[16] IWRAM_DATA = {
    0x00000000, 0x000000FF, 0x0000FF00, 0x0000FFFF, 0x00FF0000, 0x00FF00FF, 0x00FFFF00, 0x00FFFFFF,
    0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF, 0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF,
};

/* one glyph with its top left at (x, y), scaled: its outline (the letter
 * grown by a pixel each way) or its body (the style's colour for each of
 * the font's rows), each pixel written once */
IWRAM_CODE static void glyph(int x, int y, int scale, unsigned char ch, const TextStyle *st, int outline)
{
    const uint8_t *rows = g_font + (ch & 127) * 8;
    uint32_t m[2 * 7 + 2];
    int r, py, h = 7 * scale + 2;
    /* the letter's pixels, a bit each (bit 0 the leftmost), a pixel's
     * margin around them */
    m[0] = m[h - 1] = 0;
    if (scale == 1) {
        for (r = 0; r < 7; r++) m[1 + r] = (uint32_t)s_font_bits[rows[r] & 31] << 1;
    } else {
        for (r = 0; r < 7; r++) m[1 + 2 * r] = m[2 + 2 * r] = (uint32_t)s_font_bits2[rows[r] & 31] << 1;
    }
    if (outline) {
        uint32_t prev = 0;
        for (py = 0; py < h; py++) {
            uint32_t cur = m[py], next = py + 1 < h ? m[py + 1] : 0, v = prev | cur | next;
            prev = cur;
            m[py] = v | v << 1 | v >> 1;
        }
    }
    x--;
    y--;
    for (py = 0; py < h; py++) {
        uint32_t bits = m[py];
        int yy = y + py;
        uint8_t c;
        if (!bits || (unsigned)yy >= (unsigned)s_th) continue;
        c = outline ? st->outline : st->row[(py - 1) / scale];
        if (x >= 0 && x + 5 * scale + 2 <= s_tw) {
            /* a word (4 pixels) at a time: those of the letter replaced */
            uint32_t *w = (uint32_t *)&PX(x & ~3, yy), cw = c * 0x01010101u;
            for (bits <<= x & 3; bits; bits >>= 4, w++)
                if (bits & 15) {
                    uint32_t mk = s_nibble_bytes[bits & 15];
                    *w = (*w & ~mk) | (cw & mk);
                }
        } else {
            /* at an edge: a pixel at a time, those inside */
            int px;
            for (px = 0; bits; px++, bits >>= 1)
                if ((bits & 1) && (unsigned)(x + px) < (unsigned)s_tw) PX(x + px, yy) = c;
        }
    }
}

int ui_text_st(int x, int y, int scale, int align, const TextStyle *st, const char *s)
{
    int w, pass, i;
    scale = scale > 1 ? 2 : 1;
    w = ui_text_w(s, scale);
    if (align == UI_CENTER) x -= w / 2;
    else if (align == UI_RIGHT) x -= w;
    fresh(x - 1, y - 1, w + 2, 7 * scale + 2);
    for (pass = st->outline ? 0 : 1; pass < 2; pass++)
        for (i = 0; s[i]; i++)
            if (s[i] != ' ') glyph(x + i * 6 * scale, y, scale, (unsigned char)s[i], st, pass == 0);
    mark(x - 1, y - 1, w + 2, 7 * scale + 2);
    return w;
}

int ui_text(int x, int y, int scale, int align, int bank, int fancy, uint8_t plain, const char *s)
{
    TextStyle st = ui_style(bank, fancy, plain);
    return ui_text_st(x, y, scale, align, &st, s);
}

void ui_disc(int cx, int cy, int r, uint8_t c)
{
    int dy;
    fresh(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
    for (dy = -r; dy <= r; dy++) {
        /* the row's half width: inside r + 0.5 */
        int w = 0;
        while ((w + 1) * (w + 1) + dy * dy <= r * r + r) w++;
        span(cy + dy, cx - w, cx + w + 1, c);
    }
    mark(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
}

void ui_bar(int x0, int y0, int x1, int y1, int frac, int bank)
{
    /* the frame black, the inside faint; the fill from the bar's colour to
     * a lighter one at its end, its top third lighter (render_progress_bar) */
    int blue = bank > UB_BAR, base = blue ? 9 : 4, hi = blue ? 13 : 8;
    int xm = x0 + (x1 - x0) * frac / 256, x, y, k, bx[5];
    ui_fill(x0 - 1, y0 - 1, x1 - x0 + 2, y1 - y0 + 2, UI(UB_BAR, UI_K));
    ui_fill(x0, y0, x1 - x0, y1 - y0, UI(UB_BAR, 14));
    /* lighter towards the fill's end, as far as frac goes, in 4 steps:
     * where each begins (bx[k], up to xm), then the rows a span each */
    bx[0] = x0;
    for (k = 1; k <= 4; k++) bx[k] = xm;
    for (x = x0, k = 1; x < xm && k < 4; x++) {
        int step = (x - x0) * 4 * frac / (256 * (xm - x0 > 0 ? xm - x0 : 1));
        while (k < 4 && step >= k) bx[k++] = x;
    }
    for (y = y0; y < y1; y++) {
        if ((y - y0) * 3 < (y1 - y0)) span(y, x0, xm, UI(UB_BAR, hi));
        else
            for (k = 0; k < 4; k++) span(y, bx[k], bx[k + 1], UI(UB_BAR, base + k));
    }
}

void ui_scroll(int x)
{
    s_scroll = x;
}

void ui_dim(int x0, int y0, int x1, int y1, int level)
{
    int n = s_dim.n;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    if (n >= 2 || x1 <= x0 || y1 <= y0) return;
    s_dim.x0[n] = x0;
    s_dim.y0[n] = y0;
    s_dim.x1[n] = x1;
    s_dim.y1[n] = y1;
    s_dim.level = level;
    s_dim.n = n + 1;
}

void ui_hint_level(int k, Color behind)
{
    g_pal_bg[UB_HINT * 16 + UI_TEXT] = world_rgb15(col_lerp(behind, COL_WHITE, k / 16.0f));
}

/* ------------------------------------------------------------------ */

static void free_cell(int cx, int cy)
{
    if (s_cell[cy][cx] != NO_TILE) {
        s_free[s_nfree++] = s_cell[cy][cx];
        s_cell[cy][cx] = NO_TILE;
    }
}

static void touch_tile(int t)
{
    if (t < s_lo) s_lo = t;
    if (t + 1 > s_hi) s_hi = t + 1;
}

/* 8 pixels (two words of bytes, the leftmost lowest) as a 4-bit tile row:
 * each byte's low nibble */
static inline uint32_t pack8(uint32_t a, uint32_t b)
{
    a &= 0x0F0F0F0Fu;
    b &= 0x0F0F0F0Fu;
    a = (a | (a >> 4)) & 0x00FF00FFu;
    b = (b | (b >> 4)) & 0x00FF00FFu;
    return ((a | (a >> 8)) & 0xFFFFu) | ((b | (b >> 8)) & 0xFFFFu) << 16;
}

IWRAM_CODE static void pack_cell(int cx, int cy)
{
    uint32_t t[8];
    int y, bank = -1, solid = 1;
    uint8_t first = s_cv[cy * 8][cx * 8];
    uint32_t first4 = first * 0x01010101u;
    uint16_t entry, *map;
    for (y = 0; y < 8; y++) {
        const uint32_t *row = (const uint32_t *)&s_cv[cy * 8 + y][cx * 8];
        uint32_t a = row[0], b = row[1];
        if (a != first4 || b != first4) solid = 0;
        /* the bank: that of the first colour not in all of them (1-3) */
        if (bank < 0 && ((a | b) & 0x0C0C0C0Cu)) {
            int k;
            const uint8_t *p = (const uint8_t *)row;
            for (k = 0; k < 8 && bank < 0; k++)
                if ((p[k] & 15) >= 4) bank = p[k] >> 4;
        }
        t[y] = pack8(a, b);
    }
    if (bank < 0) bank = UB_WHITE; /* (only colours all banks have) */
    if (solid && !first) {
        free_cell(cx, cy);
        entry = SHARED_TILE_FIRST;
    } else if (solid) {
        free_cell(cx, cy);
        if (s_solid[first] == NO_TILE && s_nfree) {
            int n = s_free[--s_nfree];
            s_solid[first] = (uint16_t)n;
            for (y = 0; y < 8; y++) s_tiles[n][y] = t[y];
            touch_tile(n);
        }
        entry = (uint16_t)(s_solid[first] == NO_TILE ? SHARED_TILE_FIRST : (UI_TILE_FIRST + s_solid[first]) | SE_PAL(bank));
    } else {
        if (s_cell[cy][cx] == NO_TILE) {
            if (!s_nfree) return; /* out of tiles: the cell keeps what it shows */
            s_cell[cy][cx] = s_free[--s_nfree];
        }
        {
            uint32_t *d = s_tiles[s_cell[cy][cx]];
            if (d[0] != t[0] || d[1] != t[1] || d[2] != t[2] || d[3] != t[3] || d[4] != t[4] || d[5] != t[5] ||
                d[6] != t[6] || d[7] != t[7]) {
                for (y = 0; y < 8; y++) d[y] = t[y];
                touch_tile(s_cell[cy][cx]);
            }
        }
        entry = (uint16_t)((UI_TILE_FIRST + s_cell[cy][cx]) | SE_PAL(bank));
    }
    map = &s_map[cx >> 5][cy * 32 + (cx & 31)];
    if (*map != entry) {
        *map = entry;
        s_map_dirty |= 1 << (cx >> 5);
    }
    if (entry != SHARED_TILE_FIRST) s_shown[cy][cx >> 5] |= 1u << (cx & 31);
}

IWRAM_CODE void ui_clear_rows(int y0, int y1)
{
    int cy, cx, cy0 = y0 < 0 ? 0 : y0 / 8, cy1 = (y1 + 7) / 8;
    if (cy1 > CH) cy1 = CH;
    stale_rows(cy0, cy1);
    for (cy = cy0; cy < cy1; cy++) {
        int k;
        /* (only the cells that show something, or are to be packed) */
        for (k = 0; k < 2; k++) {
            uint32_t bits = s_shown[cy][k] | s_dirty[cy][k];
            for (cx = k * 32; bits; cx++, bits >>= 1) {
                uint16_t *map = &s_map[k][cy * 32 + (cx & 31)];
                if (!(bits & 1)) continue;
                free_cell(cx, cy);
                if (*map != SHARED_TILE_FIRST) {
                    *map = SHARED_TILE_FIRST;
                    s_map_dirty |= 1 << k;
                }
            }
            s_shown[cy][k] = s_dirty[cy][k] = 0;
        }
    }
}

/* The cells wholly inside [x0, x1) x [y0, y1) of the surface, all colour
 * c now (a panel's inside, a fill, an erase): shown at once as the tile of
 * that one colour pack_cell would give them, rather than packed (most of
 * a panel's cells: what is drawn over them later is packed as usual). */
static void solid_cells(int x0, int y0, int x1, int y1, uint8_t c)
{
    int cx0 = (x0 + 7) >> 3, cx1 = x1 >> 3, cy0 = (y0 + 7) >> 3, cy1 = y1 >> 3, cx, cy, bank, y;
    uint32_t m[2] = {0, 0};
    uint16_t entry;
    if (s_t != &s_cv[0][0]) return;
    if (cx0 < 0) cx0 = 0;
    if (cy0 < 0) cy0 = 0;
    if (cx1 > CW) cx1 = CW;
    if (cy1 > CH) cy1 = CH;
    if (cx0 >= cx1 || cy0 >= cy1) return;
    if (!c) {
        entry = SHARED_TILE_FIRST;
    } else {
        if (s_solid[c] == NO_TILE) {
            int n;
            if (!s_nfree) return; /* (out of tiles: packed as usual) */
            n = s_free[--s_nfree];
            s_solid[c] = (uint16_t)n;
            for (y = 0; y < 8; y++) s_tiles[n][y] = (c & 15) * 0x11111111u;
            touch_tile(n);
        }
        bank = (c & 15) >= 4 ? c >> 4 : UB_WHITE;
        entry = (uint16_t)((UI_TILE_FIRST + s_solid[c]) | SE_PAL(bank));
    }
    for (cx = cx0; cx < cx1; cx++) m[cx >> 5] |= 1u << (cx & 31);
    for (cy = cy0; cy < cy1; cy++) {
        /* (their pixels c when next drawn into, if they are: fresh) */
        int k;
        for (k = 0; k < 2; k++) {
            s_stale[cy][k] |= m[k];
            s_used[cy][k] &= ~m[k];
            s_dirty[cy][k] &= ~m[k];
            if (c) {
                s_tinted[cy][k] |= m[k];
                s_shown[cy][k] |= m[k];
            } else {
                s_tinted[cy][k] &= ~m[k];
            }
        }
        for (cx = cx0; cx < cx1; cx++) {
            uint16_t *map = &s_map[cx >> 5][cy * 32 + (cx & 31)];
            s_tint[cy][cx] = c;
            free_cell(cx, cy);
            if (*map != entry) {
                *map = entry;
                s_map_dirty |= 1 << (cx >> 5);
            }
        }
    }
}

/* the lines a frame keeps after packing (frame.h frame_lines_left): for
 * the fade (about 14) and a row of 8 cells (about 4) */
#define PACK_MARGIN 22

/* pack row cy's changed cells that are in want (columns 0-31, 32-63), at
 * most budget of them; returns how many */
IWRAM_CODE static int pack_row(int cy, const uint32_t want[2], int budget)
{
    int n = 0, h;
    for (h = 0; h < 2; h++) {
        uint32_t bits = s_dirty[cy][h] & want[h];
        int cx = h * 32;
        for (; bits && n < budget; bits >>= 1, cx++)
            if (bits & 1) {
                pack_cell(cx, cy);
                s_dirty[cy][h] &= ~(1u << (cx & 31));
                n++;
            }
    }
    return n;
}

void ui_flush(void)
{
    int cy, k;
    if (s_any_dirty) {
        /* the columns on screen first (BG0 scrolled by s_scroll), then the
         * others as far as the budget goes; what is left stays changed */
        static const uint32_t all[2] = {~0u, ~0u};
        uint32_t vis[2] = {0, 0};
        int first = (s_scroll & (UI_W - 1)) / 8;
        for (k = 0; k < SCR_W / 8 + 1; k++) {
            int cx = (first + k) & (CW - 1);
            vis[cx >> 5] |= 1u << (cx & 31);
        }
        /* (8 cells at a time, while the frame has time for them: about 4
         * lines; the frame's palettes' fade after this takes 12. What is
         * left shows next frame: text drawn in a busy frame, a screen's
         * first, appears over two) */
        for (cy = 0; cy < CH; cy++)
            while (((s_dirty[cy][0] & vis[0]) | (s_dirty[cy][1] & vis[1])) && (s_pack_all || frame_lines_left() > PACK_MARGIN))
                pack_row(cy, vis, 8);
        for (cy = 0; cy < CH; cy++)
            while ((s_dirty[cy][0] | s_dirty[cy][1]) && (s_pack_all || frame_lines_left() > PACK_MARGIN + 16))
                pack_row(cy, all, 8);
        s_any_dirty = 0;
        for (cy = 0; cy < CH; cy++) s_any_dirty |= (s_dirty[cy][0] | s_dirty[cy][1]) != 0;
    }
    s_pack_all = 0;
    if (s_hold == 2 && !s_any_dirty) s_hold = 0;
    /* (held: VRAM keeps the tiles and the map it has, which agree; what
     * the queue has no room for goes next frame, the map after its tiles) */
    if (!s_hold) {
        if (s_hi > s_lo &&
            video_queue(CHARBLOCK(CB_SHARED) + (UI_TILE_FIRST + s_lo) * 8, s_tiles[s_lo], (uint32_t)(s_hi - s_lo) * 8)) {
            s_lo = UI_TILES;
            s_hi = 0;
        }
        for (k = 0; k < 2 && s_hi <= s_lo; k++)
            if ((s_map_dirty & (1 << k)) && video_queue(SCREENBLOCK(SB_TEXT + k), s_map[k], 32 * CH / 2))
                s_map_dirty &= ~(1 << k);
    }
    g_vid.hofs[0] = (uint16_t)(s_scroll & (UI_W - 1));
    g_vid.vofs[0] = 0;
    if (s_dim.n) {
        /* inside the windows the world behind is darkened; text stays */
        g_vid.dispcnt |= DCNT_WIN0 | (s_dim.n > 1 ? DCNT_WIN1 : 0);
        g_vid.win0h = (uint16_t)(s_dim.x0[0] << 8 | s_dim.x1[0]);
        g_vid.win0v = (uint16_t)(s_dim.y0[0] << 8 | s_dim.y1[0]);
        g_vid.win1h = (uint16_t)(s_dim.x0[1] << 8 | s_dim.x1[1]);
        g_vid.win1v = (uint16_t)(s_dim.y0[1] << 8 | s_dim.y1[1]);
        g_vid.winin = 0x3F3F;
        g_vid.winout = 0x1F;
        g_vid.bldcnt = (uint16_t)(BLD_BG(1) | BLD_BG(2) | BLD_BG(3) | BLD_BACKDROP | BLD_BLACK);
        g_vid.bldy = (uint16_t)s_dim.level;
        s_dim.n = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Text in sprites                                                     */
/* ------------------------------------------------------------------ */

#define OT_MAX (1024 - OBJ_TEXT_TILE)
static uint32_t s_obj[OT_MAX][8] ALIGN4; /* what the text tiles hold */

void ui_obj_text(ObjText *t, int tile, int scale, const TextStyle *st, const char *s)
{
    static uint8_t buf[16][256] ALIGN4;
    int w = ui_text_w(s, scale) + 2, pieces = (w + 31) / 32, p, ty, tx, y;
    int first = tile - OBJ_TEXT_TILE;
    if (pieces * 8 + first > OT_MAX) pieces = (OT_MAX - first) / 8;
    memset(buf, 0, sizeof(buf));
    s_t = &buf[0][0];
    s_tw = 256;
    s_th = 16;
    ui_text_st(1, 1, scale, UI_LEFT, st, s);
    s_t = &s_cv[0][0];
    s_tw = UI_W;
    s_th = SCR_H;
    for (p = 0; p < pieces; p++)
        for (ty = 0; ty < 2; ty++)
            for (tx = 0; tx < 4; tx++) {
                uint32_t *out = s_obj[first + p * 8 + ty * 4 + tx];
                for (y = 0; y < 8; y++) {
                    const uint32_t *row = (const uint32_t *)&buf[ty * 8 + y][p * 32 + tx * 8];
                    out[y] = pack8(row[0], row[1]);
                }
            }
    if (pieces > 0) video_queue(OBJ_TILES + tile * 8, s_obj[first], (uint32_t)pieces * 64);
    t->tile = tile;
    t->pieces = pieces;
    t->w = w;
    t->h = 7 * scale + 2;
}

void ui_obj_text_show(const ObjText *t, int cx, int y, int pal, int k)
{
    int p, x0 = cx - t->w / 2, aff = -1;
    if (k != 256) aff = video_aff(0, k, k);
    for (p = 0; p < t->pieces; p++) {
        int px = x0 + p * 32;
        if (k == 256) {
            spr(px, y, W32x16, t->tile + p * 8, pal, 0, 0);
        } else if (aff >= 0) {
            /* each piece's centre moves away from the text's as it grows */
            int pcx = cx + (px + 16 - cx) * k / 256, pcy = y + t->h / 2 + (8 - t->h / 2) * k / 256;
            spr_aff(pcx, pcy, 32, 16, W32x16, t->tile + p * 8, pal, aff, 1, 0);
        }
    }
}
