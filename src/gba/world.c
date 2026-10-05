/*
 * The level's world on the GBA (world.h).
 *
 * BG1, the blocks and spikes: a block is 12 pixels, a tile 8, so a tile
 * shows quarters (4x4 pixels) of up to four cells. gba_tool puts every
 * tile of every level's world together at build time (cellmap.c,
 * src/host/gba_levels.c): a level's different tiles (under 200) go into
 * VRAM when it is shown, and the map of its world in them is written into
 * BG1's 32x32 map as the camera moves (a jump, a respawn, all of it: 651
 * entries), copied in the vertical blank.
 *
 * BG2, the ground and the corridor's bands, and BG3, the squares, are made
 * of a few fixed tiles; the ground's map is rewritten when the camera has
 * moved a tile or the corridor changed.
 *
 * The colours come from the level's palette every frame. The gradient
 * behind, and everything see-through over it, gets a colour for each
 * scanline (video.h HDMA_COLORS): these columns are straight lines from the
 * top of the screen to the bottom, worked out only when they change.
 */
#include <string.h>

#include "world.h"
#include "video.h"
#include "gen.h"
#include "game_internal.h"
#include "levels_gba.h"

int32_t g_cam_px, g_cam_py;

/* shared tiles (counted from char block 1) */
#define TILE_BLANK SHARED_TILE_FIRST
#define TILE_GROUND (SHARED_TILE_FIRST + 1)
#define TILE_SQ (TILE_GROUND + GT_COUNT)
/* the blank tile as BG1 (char block 0) counts it */
#define LEVEL_BLANK (512 + TILE_BLANK)


/* (inline: a constant divisor becomes a multiplication in the ARM code) */
static inline int floordiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* ------------------------------------------------------------------ */
/* Colours                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int r, g, b;
} Rgb;

static Rgb rgb_of(Color c)
{
    Rgb o = {(int)COL_R(c), (int)COL_G(c), (int)COL_B(c)};
    return o;
}

/* a + (b - a) * t / 256 */
static Rgb mix(Rgb a, Rgb b, int t)
{
    Rgb o = {a.r + (b.r - a.r) * t / 256, a.g + (b.g - a.g) * t / 256, a.b + (b.b - a.b) * t / 256};
    return o;
}

static Rgb scale(Rgb a, int k)
{
    Rgb o = {a.r * k / 256, a.g * k / 256, a.b * k / 256};
    return o;
}

static Rgb add(Rgb a, Rgb b)
{
    Rgb o = {a.r + b.r, a.g + b.g, a.b + b.b};
    return o;
}

/* a colour as shown: the run's flash added (flash / 256 of white: the
 * PC's added white), then the fade (k / 256) */
static Rgb lit(Rgb a, int k, int flash)
{
    int w = 255 * flash / 256;
    Rgb o = {a.r + w > 255 ? 255 : a.r + w, a.g + w > 255 ? 255 : a.g + w, a.b + w > 255 ? 255 : a.b + w};
    return scale(o, k);
}

static int clamp8(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static uint16_t rgb15(Rgb c)
{
    int r = (clamp8(c.r) + 4) >> 3, g = (clamp8(c.g) + 4) >> 3, b = (clamp8(c.b) + 4) >> 3;
    return (uint16_t)((r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10);
}

uint16_t world_rgb15(Color c)
{
    return rgb15(rgb_of(c));
}

static Rgb s_bg_top, s_bg_bot, s_ground, s_halo;

Color world_halo(void)
{
    return RGB(clamp8(s_halo.r), clamp8(s_halo.g), clamp8(s_halo.b));
}

Color world_ground(void)
{
    return RGB(clamp8(s_ground.r), clamp8(s_ground.g), clamp8(s_ground.b));
}

Color world_backdrop(int y)
{
    Rgb c = mix(s_bg_top, s_bg_bot, clampi(y, 0, SCR_H - 1) * 256 / (SCR_H - 1));
    return RGB(clamp8(c.r), clamp8(c.g), clamp8(c.b));
}

/* The per-scanline columns: each goes in a straight line from its colour at
 * the top of the screen to the one at the bottom, rounded to 15 bits (in
 * bands a few lines high, as smooth as the GBA's colours go). Each of the
 * two tables remembers what it holds. */
static struct {
    Rgb top[HDMA_COLORS], bot[HDMA_COLORS];
} s_hdma_key[2];
static uint8_t s_hdma_valid[2];

/* a channel (0..255) as 5 bits in 16.16 fixed point, at most 31.5 less a
 * little: rounded to the nearest, as rgb15 does, but never past 31 */
static int chan5(int c)
{
    c = clamp8(c) << 13;
    return c > (31 << 16) + 0x7FFF ? (31 << 16) + 0x7FFF : c;
}

/* lines [y0, y1) of a world colour's gradient from the top line to the
 * bottom one */
IWRAM_CODE static void hdma_rows(uint16_t (*t)[HDMA_COLORS], int idx, Rgb top, Rgb bot, int y0, int y1)
{
    int r = chan5(top.r), g = chan5(top.g), b = chan5(top.b);
    int dr = (chan5(bot.r) - r) / (SCR_H - 1), dg = (chan5(bot.g) - g) / (SCR_H - 1),
        db = (chan5(bot.b) - b) / (SCR_H - 1);
    uint16_t *p = &t[y0][idx];
    int y;
    r += 0x8000 + dr * y0;
    g += 0x8000 + dg * y0;
    b += 0x8000 + db * y0;
    for (y = y0; y < y1; y++) {
        *p = (uint16_t)((r >> 16) | (g >> 16) << 5 | (b >> 16) << 10);
        p += HDMA_COLORS;
        r += dr;
        g += dg;
        b += db;
    }
}

static void hdma_column(uint16_t (*t)[HDMA_COLORS], int idx, Rgb top, Rgb bot)
{
    hdma_rows(t, idx, top, bot, 0, SCR_H);
    t[SCR_H][idx] = t[SCR_H - 1][idx]; /* the line after the last, as it */
}

/* the corridor's bands BG2 shows (ground_tiles): whether, and the floor's
 * and the ceiling's heights in blocks */
static int corridor(const WorldView *v, int *floor, int *ceil)
{
    int bands = v->corr_alpha > 0.01f;
    *floor = bands && v->corr_floor > 0.01f ? (int)(v->corr_floor + 0.5f) : 0;
    *ceil = bands ? (int)(v->corr_ceil + 0.5f) : 0;
    return bands;
}

/* The halo's colours, line by line: the glow added to what BG2 shows
 * behind BG1 on that line (ground_row_tile): the gradient, a corridor's
 * band or its line, or the ground line's glow (BG1's glow pixels are
 * solid; on the PC the glow is added to whatever is there). Each part is
 * a straight line down the screen, as the gradient is. */
typedef struct {
    Rgb bg_top, bg_bot, ground, line, halo[2];
    int corr, bands, floor, ceil, k; /* (k: the fade, as video_fade's, in 256ths) */
    int flash;                       /* (the run's flash: lit) */
    int cam_py;                      /* (where the bands are: only with bands) */
} HaloBack;

/* what each of the two tables' halo columns were made from (bar the
 * ground's glow, which beats: its lines are made every frame), and the
 * first of those lines */
static HaloBack s_halo_key[2];
static uint8_t s_halo_valid[2];
static int s_halo_glow_y[2];

/* the halo's column idx but the ground glow's lines */
static void halo_column(uint16_t (*t)[HDMA_COLORS], int idx, const HaloBack *s, Rgb h)
{
    int lc = -s->ceil * BLOCK_PIX - 1 - s->cam_py, lf = -s->floor * BLOCK_PIX - s->cam_py;
    Rgb top = lit(add(s->bg_top, h), s->k, s->flash), bot = lit(add(s->bg_bot, h), s->k, s->flash);
    hdma_rows(t, idx, top, bot, 0, SCR_H);
    if (s->bands) {
        /* the bands: above the ceiling's line, under the floor's (the
         * floor's covering the ground's glow) */
        Rgb bt = lit(add(mix(s->bg_top, s->ground, s->corr), h), s->k, s->flash);
        Rgb bb = lit(add(mix(s->bg_bot, s->ground, s->corr), h), s->k, s->flash);
        Rgb lt = lit(add(mix(s->bg_top, s->line, s->corr), h), s->k, s->flash);
        Rgb lb = lit(add(mix(s->bg_bot, s->line, s->corr), h), s->k, s->flash);
        hdma_rows(t, idx, bt, bb, 0, clampi(lc, 0, SCR_H));
        if (lc >= 0 && lc < SCR_H) hdma_rows(t, idx, lt, lb, lc, lc + 1);
        if (s->floor > 0) {
            hdma_rows(t, idx, bt, bb, clampi(lf + 1, 0, SCR_H), SCR_H);
            if (lf >= 0 && lf < SCR_H) hdma_rows(t, idx, lt, lb, lf, lf + 1);
        }
    }
    t[SCR_H][idx] = t[SCR_H - 1][idx];
}

/* the halo's columns in table t (video_hdma_index) for this frame: made
 * again if what they are made from changed, else only the ground glow's
 * lines (5 over the ground's line, GT_GLOW; not under a floor's band),
 * put back to the gradient where they were and made where they are */
static void halo_columns(int t, const HaloBack *s, Rgb glow, Rgb glow2)
{
    uint16_t(*tab)[HDMA_COLORS] = g_hdma;
    int i, gy = -5 - g_cam_py, py;
    if (!s_halo_valid[t] || memcmp(&s_halo_key[t], s, sizeof(*s))) {
        for (i = 0; i < 2; i++) halo_column(tab, WC_HALO + i, s, s->halo[i]);
        s_halo_key[t] = *s;
        s_halo_valid[t] = 1;
    } else if (s_halo_glow_y[t] != gy) {
        int y0 = clampi(s_halo_glow_y[t], 0, SCR_H), y1 = clampi(s_halo_glow_y[t] + 5, 0, SCR_H);
        for (i = 0; i < 2; i++) {
            Rgb top = lit(add(s->bg_top, s->halo[i]), s->k, s->flash);
            Rgb bot = lit(add(s->bg_bot, s->halo[i]), s->k, s->flash);
            hdma_rows(tab, WC_HALO + i, top, bot, y0, y1);
            tab[SCR_H][WC_HALO + i] = tab[SCR_H - 1][WC_HALO + i];
        }
    }
    s_halo_glow_y[t] = gy;
    if (s->bands && s->floor > 0) return;
    for (i = 0; i < 2; i++) {
        uint16_t c1 = rgb15(lit(add(glow, s->halo[i]), s->k, s->flash));
        uint16_t c2 = rgb15(lit(add(glow2, s->halo[i]), s->k, s->flash));
        for (py = -5; py < 0; py++) {
            int y = py - g_cam_py;
            if (y >= 0 && y < SCR_H) tab[y][WC_HALO + i] = py >= -2 ? c1 : c2;
        }
        tab[SCR_H][WC_HALO + i] = tab[SCR_H - 1][WC_HALO + i];
    }
}

static int rgb_eq(Rgb a, Rgb b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void palettes(const WorldView *v)
{
    const Palette *p = v->pal;
    int pulse = (int)(v->pulse * 256.0f);
    int corr = (int)(v->corr_alpha * 256.0f);
    Rgb top[HDMA_COLORS], bot[HDMA_COLORS];
    Rgb accent = rgb_of(p->accent), fill = rgb_of(p->block_fill), edge = rgb_of(p->block_edge);
    Rgb ground = rgb_of(p->ground), line = rgb_of(p->ground_line);
    Rgb white = {255, 255, 255};
    int fill_a = (int)COL_A(p->block_fill) * 256 / 255;
    Rgb ec = mix(edge, white, 115 * pulse / 256); /* 0.45 on the beat */
    Rgb fill_top = mix(fill, edge, 26);           /* 0.10 */
    /* render_level's glow around blocks: 0.20 of the edge's colour, 0.30
     * on the beat, added; 0.82 and 0.47 of it in the two pixels it shows
     * in (gba_art.c). (The beat in 4 steps: 15-bit colours show few more,
     * and the columns are made again only when it steps.) */
    int halo_a = 51 + (pulse * 4 / 257) * 26 / 3;
    Rgb halo = scale(edge, halo_a * 210 / 256), halo2 = scale(edge, halo_a * 120 / 256);
    s_halo = scale(edge, halo_a);
    int i;

    s_bg_top = rgb_of(p->bg_top);
    s_bg_bot = rgb_of(p->bg_bot);
    for (i = 0; i < 2; i++) {
        Rgb bg = i ? s_bg_bot : s_bg_top;
        Rgb *o = i ? bot : top;
        o[WC_BACKDROP] = bg;
        /* the squares are fainter than this on the PC; 15-bit colours need
         * a bit more to show them at all */
        o[WC_SQ_FILL] = mix(bg, accent, 18);
        o[WC_SQ_EDGE] = mix(bg, accent, 34 + 14 * pulse / 256);
        o[WC_FILL] = mix(bg, fill, fill_a);
        o[WC_FILL_TOP] = mix(bg, fill_top, fill_a);
        o[WC_DECOR] = mix(o[WC_FILL], edge, 72); /* 0.28 */
        o[WC_BAND] = mix(bg, ground, corr);
        o[WC_BAND_LINE] = mix(bg, line, corr);
    }
    {
        /* (faded with the rest of the screen: video_fade) */
        int t = video_hdma_index(), k = 256 - v->fade * 16;
        for (i = 0; i < WC_HALO; i++) {
            Rgb a = lit(top[i], k, v->flash), b = lit(bot[i], k, v->flash);
            if (s_hdma_valid[t] && rgb_eq(s_hdma_key[t].top[i], a) && rgb_eq(s_hdma_key[t].bot[i], b)) continue;
            hdma_column(g_hdma, i, a, b);
            s_hdma_key[t].top[i] = a;
            s_hdma_key[t].bot[i] = b;
        }
        s_hdma_valid[t] = 1;
    }

    /* the per-frame colours */
    {
        Rgb spike = {6, 6, 10};
        Rgb tip = mix(scale(spike, 563), ec, 41); /* fill * 2.2, then 0.16 to the edge */
        int line_y = -g_cam_py, glow = 64 + 90 * pulse / 256; /* 0.25 + 0.35 on the beat */
        Rgb at = rgb_of(world_backdrop(line_y - 2));
        g_pal_bg[WC_EDGE] = rgb15(ec);
        g_pal_bg[WC_SPIKE] = rgb15(spike);
        g_pal_bg[WC_SPIKE_MID] = rgb15(mix(spike, tip, 128));
        g_pal_bg[WC_SPIKE_TIP] = rgb15(tip);
        Rgb g1 = add(at, scale(line, glow * 3 / 4)), g2 = add(at, scale(line, glow / 3));
        g_pal_bg[WC_GLOW] = rgb15(g1);
        g_pal_bg[WC_GLOW2] = rgb15(g2);
        {
            HaloBack hb;
            memset(&hb, 0, sizeof(hb)); /* (compared whole) */
            hb.bg_top = s_bg_top;
            hb.bg_bot = s_bg_bot;
            hb.ground = ground;
            hb.line = line;
            hb.halo[0] = halo;
            hb.halo[1] = halo2;
            hb.corr = corr;
            hb.bands = corridor(v, &hb.floor, &hb.ceil);
            hb.k = 256 - v->fade * 16;
            hb.flash = v->flash;
            hb.cam_py = hb.bands ? g_cam_py : 0;
            if (hb.bands) {
                hb.ground = ground;
            } else {
                /* (what the bands would take: nothing then) */
                hb.ground = hb.line = (Rgb){0, 0, 0};
                hb.corr = 0;
            }
            halo_columns(video_hdma_index(), &hb, g1, g2);
        }
    }
    /* the ground: darker towards the bottom (render_ground: to 0.55),
     * with separators 0.22 darker */
    {
        static const int depth_t[GROUND_DEPTHS] = {20, 77, 141, 200, 243};
        Rgb deep = scale(ground, 141);
        s_ground = mix(ground, deep, depth_t[1]);
        for (i = 0; i < GROUND_DEPTHS; i++) {
            Rgb s = mix(ground, deep, depth_t[i]);
            g_pal_bg[PAL_GROUND * 16 + GC_SHADE + i] = rgb15(s);
            g_pal_bg[PAL_GROUND * 16 + GC_SEP + i] = rgb15(scale(s, 200));
        }
        g_pal_bg[PAL_GROUND * 16 + GC_LINE] = rgb15(line);
    }
}

/* ------------------------------------------------------------------ */
/* BG1: the level's tiles                                              */
/* ------------------------------------------------------------------ */

/* The tiles of a layer drawn so far: [x0, x1) x [y0, y1). */
typedef struct {
    int x0, x1, y0, y1;
} Have;

/* Calls put(tx, ty) for each tile of the window [vx0, vx1) x [vy0, vy1)
 * that is not in *h (the rows and columns coming into view), and makes *h
 * the window. */
/* how many of the window's tiles h does not have yet */
static int missing(const Have *h, int vx0, int vx1, int vy0, int vy1)
{
    int w = (vx1 < h->x1 ? vx1 : h->x1) - (vx0 > h->x0 ? vx0 : h->x0);
    int ht = (vy1 < h->y1 ? vy1 : h->y1) - (vy0 > h->y0 ? vy0 : h->y0);
    if (h->x1 <= h->x0 || w < 0 || ht < 0) w = ht = 0;
    return (vx1 - vx0) * (vy1 - vy0) - w * ht;
}
/* more than this many: all of the window again, a row at a time (in IWRAM,
 * a tenth of the time a tile at a time takes) */
#define STREAM_MAX 64

static void stream(Have *h, int vx0, int vx1, int vy0, int vy1, void (*put)(int tx, int ty))
{
    int tx, ty;
    for (ty = vy0; ty < vy1; ty++) {
        if (ty < h->y0 || ty >= h->y1 || h->x1 <= h->x0) {
            for (tx = vx0; tx < vx1; tx++) put(tx, ty);
        } else {
            int a = vx1 < h->x0 ? vx1 : h->x0, b = vx0 > h->x1 ? vx0 : h->x1;
            for (tx = vx0; tx < a; tx++) put(tx, ty);
            for (tx = b; tx < vx1; tx++) put(tx, ty);
        }
    }
    h->x0 = vx0;
    h->x1 = vx1;
    h->y0 = vy0;
    h->y1 = vy1;
}

/* BG1's map as it is to be shown: copied in the vertical blank when it
 * changed (with the level's tiles when it is another level's), so that a
 * frame never shows it half written, not even after a respawn's jump */
static uint16_t s_bg1[32 * 32] ALIGN4;
static int s_bg1_dirty;
static const LevelArt *s_art;   /* the level whose tiles BG1 has (levels_gba.h) */
static Have s_have;             /* its tiles in the map */
static int s_off_px;            /* a shift of the tiles' x (the title's loop, see below) */
static int s_prev_x0;

/* tile (tx, ty) of s_art's world (tx counted with s_off_px) into the map */
static void put_tile(int tx, int ty)
{
    const LevelArt *a = s_art;
    int x = tx - s_off_px / 8, row = -1 - ty;
    uint16_t e = LEVEL_BLANK;
    if ((unsigned)x < (unsigned)a->tw && (unsigned)row < (unsigned)a->th) {
        uint16_t t = a->map[row * a->tw + x];
        if (t != LEVEL_EMPTY) e = t;
    }
    s_bg1[(ty & 31) * 32 + (tx & 31)] = e;
    s_bg1_dirty = 1;
}

/* all of the window [vx0, vx1) x [vy0, vy1) at once, a row at a time (a
 * jump: a respawn, a level's start; as ground_all) */
IWRAM_CODE static void tiles_all(int vx0, int vx1, int vy0, int vy1)
{
    const LevelArt *a = s_art;
    int ty, xoff = s_off_px / 8;
    for (ty = vy0; ty < vy1; ty++) {
        uint16_t *row = &s_bg1[(ty & 31) * 32];
        int r = -1 - ty, tx;
        if ((unsigned)r >= (unsigned)a->th) {
            for (tx = vx0; tx < vx1; tx++) row[tx & 31] = LEVEL_BLANK;
        } else {
            const uint16_t *src = a->map + r * a->tw;
            for (tx = vx0; tx < vx1; tx++) {
                int x = tx - xoff;
                uint16_t t = (unsigned)x < (unsigned)a->tw ? src[x] : LEVEL_EMPTY;
                row[tx & 31] = t == LEVEL_EMPTY ? LEVEL_BLANK : t;
            }
        }
    }
    s_bg1_dirty = 1;
}

static void level_tiles(const WorldView *v, int x0, int y0)
{
    const LevelArt *a = level_art(v->L);
    int vx0, vx1, vy0, vy1;
    if (!a) {
        /* no level: no blocks */
        g_vid.dispcnt &= (uint16_t)~DCNT_BG(1);
        s_art = NULL;
        memset(&s_have, 0, sizeof(s_have));
        return;
    }
    if (a != s_art) {
        /* another level (under the fade's black): its tiles, made by
         * gba_tool, into VRAM in the vertical blank, and all of the map */
        if (!video_queue(CHARBLOCK(CB_LEVEL), a->tiles, (uint32_t)a->ntiles * 8)) {
            g_vid.dispcnt &= (uint16_t)~DCNT_BG(1);
            return; /* (the queue is full: next frame) */
        }
        s_art = a;
        memset(&s_have, 0, sizeof(s_have));
        s_off_px = 0;
    } else if (v->loops && x0 - s_prev_x0 < -(DEMO_LOOP * BLOCK_PIX - 16) &&
               x0 - s_prev_x0 > -(DEMO_LOOP * BLOCK_PIX + 16)) {
        /* the title's run moved back by its loop onto the same picture: the
         * map stays, counted from further along (in a level, a respawn as
         * far back is a jump like any other) */
        s_off_px += DEMO_LOOP * BLOCK_PIX;
    }
    s_prev_x0 = x0;
    x0 += s_off_px;
    vx0 = floordiv(x0, 8);
    vx1 = floordiv(x0 + SCR_W - 1, 8) + 1;
    vy0 = floordiv(y0, 8);
    vy1 = floordiv(y0 + SCR_H - 1, 8) + 1;
    if (s_have.x1 > s_have.x0 && ((vx1 > s_have.x1 ? vx1 : s_have.x1) - (vx0 < s_have.x0 ? vx0 : s_have.x0) > 32 ||
                                  (vy1 > s_have.y1 ? vy1 : s_have.y1) - (vy0 < s_have.y0 ? vy0 : s_have.y0) > 32)) {
        /* too far for the 32x32 map (a jump): all of it again */
        memset(&s_have, 0, sizeof(s_have));
    }
    if (missing(&s_have, vx0, vx1, vy0, vy1) > STREAM_MAX) {
        tiles_all(vx0, vx1, vy0, vy1);
        s_have.x0 = vx0;
        s_have.x1 = vx1;
        s_have.y0 = vy0;
        s_have.y1 = vy1;
    } else {
        stream(&s_have, vx0, vx1, vy0, vy1, put_tile);
    }
    if (s_bg1_dirty && video_queue(SCREENBLOCK(SB_LEVEL), s_bg1, sizeof(s_bg1) / 4)) s_bg1_dirty = 0;
    g_vid.hofs[1] = (uint16_t)(x0 & 255);
    g_vid.vofs[1] = (uint16_t)(y0 & 255);
}

/* ------------------------------------------------------------------ */
/* BG2: the ground and the bands                                       */
/* ------------------------------------------------------------------ */

static struct {
    int floor, ceil, bands, off;
} s_gkey;
static Have s_ghave;
/* the ground looks the same every 48 pixels (its separators): a camera
 * that jumps far (the level select's wrapping around) is followed by that
 * much less, a multiple of 48, rather than all of it drawn again */
#define GROUND_PERIOD 48
static int s_gshift, s_gprev;

static inline uint16_t ground_row_tile(int ty, int floor, int ceil, int bands, int *sep_ok)
{
    *sep_ok = 0;
    if (bands) {
        int lf = -floor * BLOCK_PIX, lc = -ceil * BLOCK_PIX;
        int e = lf - ty * 8, d = lc - ty * 8;
        if (floor > 0) {
            if (e == 0) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_FLOOR_0);
            if (e == 4) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_FLOOR_4);
            if (e < 0) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_BAND);
        }
        if (d == 8) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_CEIL_7);
        if (d == 4) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_CEIL_3);
        if (d >= 12) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_BAND);
    }
    if (ty >= 0) {
        *sep_ok = 1;
        return SE_PAL(PAL_GROUND) | (TILE_GROUND + GT_GROUND + (ty < GROUND_DEPTHS ? ty : GROUND_DEPTHS - 1) * 2);
    }
    if (ty == -1) return SE_PAL(PAL_WORLD) | (TILE_GROUND + GT_GLOW);
    return TILE_BLANK;
}

/* BG2's map as it is to be shown, copied in the vertical blank when it
 * changed (as BG1's) */
static uint16_t s_bg2[32 * 32] ALIGN4;
static int s_bg2_dirty;

/* each map row's tile for the corridor in s_gkey (ground_row_tile), and
 * which row of the world it is for (its rows 32 apart share it) */
static uint16_t s_grow_t[32];
static uint8_t s_grow_sep[32];
static int s_grow_ty[32];

/* ground tile (tx, ty) for the corridor in s_gkey */
static void put_ground(int tx, int ty)
{
    int sep_ok, real = tx - s_gkey.off / 8, r = ty & 31;
    uint16_t t;
    if (s_grow_ty[r] != ty) {
        s_grow_ty[r] = ty;
        s_grow_t[r] = ground_row_tile(ty, s_gkey.floor, s_gkey.ceil, s_gkey.bands, &sep_ok);
        s_grow_sep[r] = (uint8_t)sep_ok;
    }
    t = s_grow_t[r];
    sep_ok = s_grow_sep[r];
    /* a separator every 4 blocks (48 pixels = 6 tiles) */
    s_bg2[(ty & 31) * 32 + (tx & 31)] = (uint16_t)(sep_ok && real - floordiv(real, 6) * 6 == 0 ? t + 1 : t);
    s_bg2_dirty = 1;
}

/* all of the window [vx0, vx1) x [vy0, vy1) at once, a row at a time (a
 * jump: a respawn, a level's start) */
IWRAM_CODE static void ground_all(int vx0, int vx1, int vy0, int vy1)
{
    int tx, ty;
    for (ty = vy0; ty < vy1; ty++) {
        int sep_ok, real = vx0 - s_gkey.off / 8, k = real - floordiv(real, 6) * 6;
        uint16_t t = ground_row_tile(ty, s_gkey.floor, s_gkey.ceil, s_gkey.bands, &sep_ok), *row = &s_bg2[(ty & 31) * 32];
        for (tx = vx0; tx < vx1; tx++) {
            row[tx & 31] = (uint16_t)(sep_ok && k == 0 ? t + 1 : t);
            k = k == 5 ? 0 : k + 1;
        }
    }
    s_bg2_dirty = 1;
}

static void ground_tiles(const WorldView *v, int x0, int y0)
{
    int floor, ceil, bands = corridor(v, &floor, &ceil);
    int off = v->L ? s_off_px : 0, vx0, vy0, jump = x0 + off - s_gshift - s_gprev;
    if (jump > SCR_W || jump < -SCR_W) s_gshift += floordiv(jump + GROUND_PERIOD / 2, GROUND_PERIOD) * GROUND_PERIOD;
    x0 += off - s_gshift;
    s_gprev = x0;
    vx0 = floordiv(x0, 8);
    vy0 = floordiv(y0, 8);
    g_vid.hofs[2] = (uint16_t)(x0 & 255);
    g_vid.vofs[2] = (uint16_t)(y0 & 255);
    if (s_gkey.floor != floor || s_gkey.ceil != ceil || s_gkey.bands != bands || s_gkey.off != off) {
        /* the corridor changed: all of it again */
        s_gkey.floor = floor;
        s_gkey.ceil = ceil;
        s_gkey.bands = bands;
        s_gkey.off = off;
        memset(&s_ghave, 0, sizeof(s_ghave));
        {
            int i;
            for (i = 0; i < 32; i++) s_grow_ty[i] = 0x7FFFFFFF; /* (the rows' tiles again) */
        }
    }
    {
        int vx1 = vx0 + SCR_W / 8 + 1, vy1 = vy0 + SCR_H / 8 + 1;
        if (missing(&s_ghave, vx0, vx1, vy0, vy1) > STREAM_MAX) {
            /* (most of it not there yet) */
            ground_all(vx0, vx1, vy0, vy1);
            s_ghave.x0 = vx0;
            s_ghave.x1 = vx1;
            s_ghave.y0 = vy0;
            s_ghave.y1 = vy1;
        } else {
            stream(&s_ghave, vx0, vx1, vy0, vy1, put_ground);
        }
    }
    if (s_bg2_dirty && video_queue(SCREENBLOCK(SB_GROUND), s_bg2, sizeof(s_bg2) / 4)) s_bg2_dirty = 0;
}

/* ------------------------------------------------------------------ */

void world_init(void)
{
    int y;
    volatile uint32_t *shared = CHARBLOCK(CB_SHARED);
    dma3_fill32(shared + TILE_BLANK * 8, 0, 8);
    dma3_copy32(shared + TILE_GROUND * 8, g_ground_tiles, GT_COUNT * 8);
    dma3_copy32(shared + TILE_SQ * 8, g_sq_tiles, SQ_TILE_COUNT * 8);
    /* the squares' 64x32 map: two screen blocks side by side */
    for (y = 0; y < SQ_MAP_H; y++) {
        int x;
        for (x = 0; x < SQ_MAP_W; x++) {
            uint16_t e = g_sq_map[y * SQ_MAP_W + x];
            SCREENBLOCK(SB_SQUARES + (x >> 5))[y * 32 + (x & 31)] =
                (uint16_t)(SE_PAL(PAL_WORLD) | (e & 0x0C00) | (TILE_SQ + (e & 0x3FF)));
        }
    }
    dma3_fill32(SCREENBLOCK(SB_LEVEL), LEVEL_BLANK | LEVEL_BLANK << 16, 512);
    {
        int i;
        for (i = 0; i < 32 * 32; i++) s_bg1[i] = LEVEL_BLANK;
    }
    dma3_fill32(SCREENBLOCK(SB_GROUND), TILE_BLANK | TILE_BLANK << 16, 512);
    {
        int i;
        for (i = 0; i < 32 * 32; i++) s_bg2[i] = TILE_BLANK;
    }
    s_gkey.bands = -1;
}

void world_draw(const WorldView *v)
{
    int x0 = (int)floorf(v->cam_x * BLOCK_PIX + 0.5f);
    int y0 = (int)floorf(-v->cam_y * BLOCK_PIX + 0.5f) - SCR_H;
    g_cam_px = x0;
    g_cam_py = y0;
    palettes(v);
    level_tiles(v, x0, y0);
    ground_tiles(v, x0, y0);
    {
        /* the squares: 0.3 of the camera's speed across (render_background's
         * near layer), 0.18 up and down; the picture's bottom row is the
         * ground's line seen without that */
        int sx = (int)(v->squares_x * (BLOCK_PIX * 0.3f));
        int sy = 96 - (int)(v->cam_y * (BLOCK_PIX * 0.18f));
        g_vid.hofs[3] = (uint16_t)(sx & 511);
        g_vid.vofs[3] = (uint16_t)(sy & 255);
    }
}
