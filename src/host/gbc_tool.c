/*
 * gbc_tool - builds the Game Boy Color version's data from the game's own
 * sources, and checks its physics against the reference.
 *
 *   gbc_tool export <dir> [version]   write the generated sources the ROM is
 *                                     built from: the levels as tile grids,
 *                                     tiles and sprites, palettes, and the
 *                                     songs arranged for the four channels
 *   gbc_tool levels                   the levels on the Game Boy, those that fit
 *                                     it: "<its number> <the game's number>"
 *                                     per line, and why the others don't fit
 *   gbc_tool difftest [lvl|all] [n]   play each level with the Game Boy's
 *                                     physics (src/gbc/gbsim.c) and the
 *                                     reference (src/core/sim.c) side by side,
 *                                     along the solver's run and n runs that
 *                                     leave it (default 2000): fails unless
 *                                     every tick of every run is the same
 *   gbc_tool script <lvl> <out>       the solver's inputs, for the emulator test:
 *                                     one line per press, "<tick> <ticks held>"
 *   gbc_tool replay <lvl> <script>    play a script, print the player every tick
 *   gbc_tool sheet <out.png> [pal]    every tile and sprite in a level palette
 *   gbc_tool view <lvl> <x> <out.png> the 160x144 screen with the player at x,
 *                                     drawn from the tiles (scaled 3x)
 *   gbc_tool music                    channel usage and size of every song
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_write.h"
#include "solver.h"
#include "../core/audio.h"
#include "../core/font.h"
#include "../core/game_internal.h" /* the title demo */
#include "../core/level.h"
#include "../core/platform.h"
#include "../core/songdata.h"
#include "../core/theme.h"
#include "../gbc/gbsim.h"
#include "../gbc/gfx_ids.h"
#include "../gbc/leveldata.h"
#include "../gbc/musicdata.h"

/* the core is linked whole; these are never called here */
int plat_save_read(void *buf, int size)
{
    (void)buf;
    (void)size;
    return -1;
}
int plat_save_write(const void *buf, int size)
{
    (void)buf;
    (void)size;
    return 0;
}
const char *plat_name(void) { return "TOOL"; }

/* ROM banks of the exported data (bank 0 holds the code and small tables) */
#define BANK_GFX 1
#define BANK_LEVEL0 2
#define BANK_SONG0 (BANK_LEVEL0 + 6)
#define BANK_SONG_LAST 12
#define BANK_DEMO 13      /* the title's demo level; the simulation's code is in bank 14, the menus' in 15 */
#define BANK_UI 15        /* with the menus, palette.c */
#define ROM_BANKS 16
#define BANK_SIZE 16384

static void die(const char *msg)
{
    fprintf(stderr, "gbc_tool: %s\n", msg);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Levels                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[32];
    int diff, stars, song, speed, pal;
    int width, height, ncoins;
    uint8_t *cells; /* width * GS_ROWS tiles, column-major */
    int ntrig;
    GbTrigger trig[LEVEL_MAX_TRIGGERS];
    int portal_overlaps; /* portal ends hidden behind other tiles */
} GLevel;

#define CELL(g, x, y) ((g)->cells[(size_t)(x) * GS_ROWS + (size_t)(y)])

static int obj_tile(const LevelObj *o)
{
    int t = o->type;
    if (t >= OBJ_SPIKE_UP && t <= OBJ_SPIKE_SM_DOWN) return T_SPIKE_UP + (t - OBJ_SPIKE_UP);
    if (t == OBJ_SAW_SMALL) return T_SAW;
    if (t >= OBJ_ORB_YELLOW && t <= OBJ_ORB_GREEN) return T_ORB_YELLOW + (t - OBJ_ORB_YELLOW);
    if (t >= OBJ_PAD_YELLOW && t <= OBJ_PAD_BLUE)
        return ((o->flags & OF_CEILING) ? T_PAD_YELLOW_C : T_PAD_YELLOW) + (t - OBJ_PAD_YELLOW);
    if (t >= OBJ_PORTAL_CUBE && t <= OBJ_SPEED_3) return T_PORTAL + 3 * (t - OBJ_PORTAL_CUBE) + 1;
    if (t == OBJ_COIN) return T_COIN + ((o->flags >> 4) & 3);
    return -1; /* big saws: a 3x3-tile object, not supported */
}

/* Why the last level build_level refused doesn't fit the Game Boy. */
static char s_unfit[160];

/* The level (src, numbered idx in messages) as the Game Boy stores it, or
 * -1 (and s_unfit) if it doesn't fit the Game Boy: the Game Boy plays the
 * levels that do. */
static int build_level_src(const char *const *src, int idx, GLevel *g)
{
    Level *L = level_parse(src);
    if (!L) die("out of memory");
    memset(g, 0, sizeof(*g));
    memcpy(g->name, L->name, sizeof(g->name));
    g->diff = L->difficulty;
    g->stars = L->stars;
    g->song = L->song;
    g->speed = L->start_speed;
    g->pal = L->start_pal;
    g->width = L->width;
    g->height = L->height;
    g->ncoins = L->ncoins;
    if (L->height > GS_ROWS) {
        snprintf(s_unfit, sizeof(s_unfit), "%d rows high, the Game Boy stores %d", L->height, GS_ROWS);
        level_free(L);
        return -1;
    }
    if ((size_t)L->width * GS_ROWS > BANK_SIZE) {
        snprintf(s_unfit, sizeof(s_unfit), "%d columns, a ROM bank holds %d", L->width, BANK_SIZE / GS_ROWS);
        level_free(L);
        return -1;
    }
    g->cells = (uint8_t *)calloc((size_t)L->width * GS_ROWS, 1);
    for (int y = 0; y < L->height; y++) {
        for (int x = 0; x < L->width; x++) {
            int t = L->grid[y * L->width + x], e = L->edges[y * L->width + x];
            if (t == OBJ_BLOCK) CELL(g, x, y) = (uint8_t)(T_BLOCK + e);
            else if (t == OBJ_SLAB_LO) CELL(g, x, y) = (uint8_t)(T_SLAB_LO + (e & 3));
            else if (t == OBJ_SLAB_HI) CELL(g, x, y) = (uint8_t)(T_SLAB_HI + (e & 3));
        }
    }
    for (int i = 0; i < L->nobjs; i++) {
        const LevelObj *o = &L->objs[i];
        int t = obj_tile(o);
        if (t < 0) {
            snprintf(s_unfit, sizeof(s_unfit), "object type %d at %d,%d (a big saw) isn't on the Game Boy", o->type,
                     o->cx, o->cy);
            free(g->cells);
            level_free(L);
            return -1;
        }
        if (CELL(g, o->cx, o->cy)) die("two objects in one cell");
        CELL(g, o->cx, o->cy) = (uint8_t)t;
    }
    /* a portal's top and bottom tiles, where nothing else is */
    for (int i = 0; i < L->nobjs; i++) {
        const LevelObj *o = &L->objs[i];
        if (o->type < OBJ_PORTAL_CUBE || o->type > OBJ_SPEED_3) continue;
        int base = T_PORTAL + 3 * (o->type - OBJ_PORTAL_CUBE);
        for (int part = 0; part < 3; part += 2) {
            int y = o->cy + (part == 0 ? 1 : -1);
            if (y < 0 || y >= GS_ROWS) continue;
            if (CELL(g, o->cx, y)) g->portal_overlaps++;
            else CELL(g, o->cx, y) = (uint8_t)(base + part);
        }
    }
    /* corridors: from a ship/ball/UFO/wave portal to the next cube portal
     * (portals are passed in column order, every one of them), the ceiling
     * and the ground above it, and a raised floor's surface */
    {
        int on = 0, fl = 0, cl = SIM_CORRIDOR;
        for (int x = 0; x < L->width; x++) {
            for (int i = L->col_start[x]; i < L->col_start[x + 1]; i++) {
                const LevelObj *o = &L->objs[i];
                if (o->type == OBJ_PORTAL_CUBE) on = 0;
                if (o->type >= OBJ_PORTAL_SHIP && o->type <= OBJ_PORTAL_WAVE) {
                    on = 1;
                    fl = maxi(0, o->cy - SIM_CORRIDOR / 2 + 1); /* as gbsim's enter_mode */
                    cl = fl + SIM_CORRIDOR;
                }
            }
            if (!on) continue;
            int sep = (x & 3) == 0;
            for (int y = cl; y < GS_ROWS; y++)
                if (!CELL(g, x, y)) CELL(g, x, y) = (uint8_t)((y == cl ? T_CEIL_EDGE : T_GROUND_FILL) + sep);
            if (fl > 0 && !CELL(g, x, fl - 1)) CELL(g, x, fl - 1) = (uint8_t)(T_GROUND_TOP + sep);
        }
    }
    /* the tile grid must say the same as the level about every cell */
    for (int x = 0; x < L->width; x++) {
        for (int y = 0; y < GS_ROWS; y++) {
            int want = y < L->height ? L->grid[y * L->width + x] : 0;
            for (int i = L->col_start[x]; i < L->col_start[x + 1]; i++)
                if (L->objs[i].cy == y) want = L->objs[i].type;
            int got = GTI_KIND(gs_tile_info[CELL(g, x, y)]);
            if (got != want) {
                fprintf(stderr, "level %d: cell %d,%d is kind %d in tiles, %d in the level\n", idx, x, y, got, want);
                exit(1);
            }
        }
    }
    /* the simulation remembers used objects in a short ring: no stretch of
     * the level the player can reach at once may hold more */
    for (int x = 0; x + 4 <= L->width; x++) {
        int n = 0;
        for (int i = L->col_start[x]; i < L->col_start[x + 4]; i++)
            if (L->objs[i].type >= OBJ_ORB_YELLOW) n++;
        if (n > GS_USED_N) {
            snprintf(s_unfit, sizeof(s_unfit), "%d orbs/pads/portals/coins within columns %d..%d, the Game Boy's "
                     "physics remembers %d", n, x, x + 3, GS_USED_N);
            free(g->cells);
            level_free(L);
            return -1;
        }
    }
    g->ntrig = L->ntrig;
    for (int i = 0; i < L->ntrig; i++) {
        g->trig[i].x = (uint16_t)L->trig[i].x;
        g->trig[i].pal = L->trig[i].pal;
    }
    level_free(L);
    return 0;
}

static int build_level(int idx, GLevel *g) { return build_level_src(g_levels[idx].src, idx, g); }

/* The levels on the Game Boy: those that fit, in the game's order, at most
 * one per level bank. */
#define GB_MAX_LEVELS (BANK_SONG0 - BANK_LEVEL0)
static int s_gb_level[GB_MAX_LEVELS], s_gb_count = -1;

static void gb_levels(int verbose)
{
    if (s_gb_count >= 0) return;
    s_gb_count = 0;
    for (int i = 0; i < g_level_count; i++) {
        GLevel g;
        if (build_level(i, &g) < 0) {
            LevelInfo info;
            level_info(i, &info);
            if (verbose) fprintf(stderr, "gbc_tool: level %d (%s) is left out: %s\n", i, info.name, s_unfit);
            continue;
        }
        free(g.cells);
        if (s_gb_count == GB_MAX_LEVELS) {
            if (verbose) fprintf(stderr, "gbc_tool: level %d is left out: the ROM has banks for %d\n", i, GB_MAX_LEVELS);
            continue;
        }
        s_gb_level[s_gb_count++] = i;
    }
}

static void use_level(const GLevel *g)
{
    gs_cells = g->cells;
    gs_width = (uint16_t)g->width;
    gs_height = (uint8_t)g->height;
    gs_ring_reset();
}

/* ------------------------------------------------------------------ */
/* Tiles                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t p[8][8];
} Tile;

static Tile s_bg[256];       /* VRAM bank 0 */
static uint8_t s_bg_pal[256];
static int s_bg_count;       /* tiles 0 .. s_bg_count-1 are loaded for a level */
static int s_logo_count;     /* logo tiles from BT_LOGO */
static Tile s_ui[256];       /* VRAM bank 1 */
static Tile s_saw[SAW_FRAMES];
/* the title logo: tile numbers of its cells */
#define LOGO_W 20
#define LOGO_H 8
static uint8_t s_logo_map[LOGO_H][LOGO_W];

static void tile_2bpp(const Tile *t, uint8_t out[16])
{
    for (int y = 0; y < 8; y++) {
        uint8_t lo = 0, hi = 0;
        for (int x = 0; x < 8; x++) {
            lo |= (uint8_t)((t->p[y][x] & 1) << (7 - x));
            hi |= (uint8_t)(((t->p[y][x] >> 1) & 1) << (7 - x));
        }
        out[y * 2] = lo;
        out[y * 2 + 1] = hi;
    }
}

/* ASCII art: keys[i] is the character of colour i. */
static void art(Tile *t, const char *const rows[8], const char *keys)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const char *k = strchr(keys, rows[y][x]);
            t->p[y][x] = (uint8_t)(k && rows[y][x] ? k - keys : 0);
        }
}

/* t as two tiles half a tile lower: its upper half in the lower 4 rows of
 * out[0], its lower half in the upper 4 rows of out[1] */
static void half_down(const Tile *t, Tile out[2])
{
    memset(out, 0, 2 * sizeof(Tile));
    for (int y = 0; y < 4; y++) {
        memcpy(out[0].p[y + 4], t->p[y], 8);
        memcpy(out[1].p[y], t->p[y + 4], 8);
    }
}

static void flip_v(Tile *t)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t a = t->p[y][x];
            t->p[y][x] = t->p[7 - y][x];
            t->p[7 - y][x] = a;
        }
}

/*
 * Shapes are drawn by a function giving the colour at a point (in pixels of
 * a canvas); each pixel takes the colour most of its 4x4 samples have. Dark
 * outline colours count a little more, so outlines stay unbroken.
 */
typedef int (*ShadeFn)(double x, double y, const void *ctx);

static int resolve(const int *cnt, int dark)
{
    int best = 0, bw = cnt[0] * 4;
    for (int c = 1; c < 4; c++) {
        int w = cnt[c] * (c == dark ? 5 : 4);
        if (w > bw) {
            bw = w;
            best = c;
        }
    }
    return best;
}

static void shade_canvas(uint8_t *out, int w, int h, ShadeFn f, const void *ctx, int dark)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int cnt[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) cnt[f(x + (sx + 0.5) / 4.0, y + (sy + 0.5) / 4.0, ctx) & 3]++;
            out[y * w + x] = (uint8_t)resolve(cnt, dark);
        }
}

/* Canvas (w x h, column of 8-pixel-wide tiles) into tiles: tile (col, row). */
static void canvas_tile(const uint8_t *cv, int w, int col, int row, Tile *t)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) t->p[y][x] = cv[(row * 8 + y) * w + col * 8 + x];
}

/* --- level tiles --- */

static void make_block(Tile *t, int edges, int y0, int y1)
{
    memset(t, 0, sizeof(*t));
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < 8; x++) {
            int e = ((edges & GE_EDGE_L) && x == 0) || ((edges & GE_EDGE_R) && x == 7) ||
                    ((edges & GE_EDGE_T) && y == y0) || ((edges & GE_EDGE_B) && y == y1);
            t->p[y][x] = e ? 2 : 1;
        }
}

/*
 * The small saw, turned by angle, by its pixels' middles: an 8x8 tile has
 * no room for teeth beyond the disc (drawn so, a tooth was a bump of a
 * pixel that came and went with the turn, and the saw an oval ring). So
 * the disc's rim is the edge's colour all round, as render_saw's outline,
 * and what turns is inside it, a ring dashed by the 6 teeth (the edge's
 * colour where a tooth is, the fill's between), round the edge's dot.
 */
static void saw_tile(Tile *t, double angle)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            double dx = x + 0.5 - 4.0, dy = y + 0.5 - 4.0, r = sqrt(dx * dx + dy * dy);
            double ph = (atan2(dy, dx) - angle) * 6.0 / (2.0 * PI);
            ph -= floor(ph);
            t->p[y][x] = r > 3.8 ? 0 : r > 2.8 || r < 0.8 ? 2 : fabs(r - 1.85) < 0.5 && ph < 0.5 ? 2 : 3;
        }
}

typedef struct {
    int col;
} PortalCtx;

/*
 * A speed portal's 1..4 chevrons into its 8x24 canvas: render.c's (0.62
 * of a block from the middle to a tip, 5 rows here, a pixel to the side
 * for each two), a pixel wide and 2 apart, every other one white (in one
 * colour, 2 pixels apart, they ran together); four are 9 pixels across,
 * the first one's outer rows cut by the tile's edge.
 */
static void speed_canvas(uint8_t *cv, int n, int col)
{
    enum { H = 5 };
    int x0 = n < 4 ? 4 - n : -1;
    memset(cv, 0, 8 * 24);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 2 * H; k++) {
            int x = x0 + 2 * i + (k < H ? k : 2 * H - 1 - k) / 2;
            if (x >= 0 && x < 8) cv[(12 - H + k) * 8 + x] = (uint8_t)(i & 1 ? 3 : col);
        }
}

static int shade_portal(double x, double y, const void *ctx)
{
    const PortalCtx *p = (const PortalCtx *)ctx;
    double cx = 4.0, cy = 12.0;
    double rx = 3.75, ry = 11.6;
    double d = (x - cx) * (x - cx) / (rx * rx) + (y - cy) * (y - cy) / (ry * ry);
    if (d > 1.0) return 0;
    double rx2 = rx - 1.45, ry2 = ry - 1.7;
    double d2 = (x - cx) * (x - cx) / (rx2 * rx2) + (y - cy) * (y - cy) / (ry2 * ry2);
    if (d2 > 1.0) return p->col;
    double rx3 = rx2 - 0.9, ry3 = ry2 - 1.0;
    double d3 = (x - cx) * (x - cx) / (rx3 * rx3) + (y - cy) * (y - cy) / (ry3 * ry3);
    return d3 > 1.0 ? 3 : 0;
}

static int shade_coin(double x, double y, const void *ctx)
{
    (void)ctx;
    double dx = x - 4.0, dy = y - 4.0, r = sqrt(dx * dx + dy * dy);
    if (r > 3.9) return 0;
    if (r > 3.0) return 2;
    if (fabs(dx + 1.0) < 0.6 && dy > -2.2 && dy < 0.4) return 3; /* shine */
    if (r < 1.6 && r > 0.8) return 2;
    return 1;
}

/* Palette and colour index of each portal kind (cube .. speed 3). */
static const uint8_t PORTAL_PAL[T_PORTAL_KINDS] = {PAL_BG, PAL_YP, PAL_OC, PAL_GD, PAL_OC, PAL_YP,
                                                   PAL_BG, PAL_GD, PAL_OC, PAL_BG, PAL_YP};
static const uint8_t PORTAL_COL[T_PORTAL_KINDS] = {2, 2, 1, 1, 2, 1, 1, 1, 2, 2, 2};

static void make_level_tiles(void)
{
    static const char *const SPIKE[8] = {"...EE...", "...EE...", "..ESSE..", "..ESSE..",
                                         ".ESSSSE.", ".ESSSSE.", "ESSSSSSE", "EEEEEEEE"};
    static const char *const SPIKE_SM[8] = {"........", "........", "........", "........",
                                            "...EE...", "..ESSE..", ".ESSSSE.", ".EEEEEE."};
    static const char *const ORB[8] = {"..CCCC..", ".C....C.", "C.WCC..C", "C.CCCC.C",
                                       "C.CCCC.C", "C..CC..C", ".C....C.", "..CCCC.."};
    static const char *const PAD[8] = {"........", "........", "........", "........",
                                       "........", "..CCCC..", ".CWWWWC.", "CCCCCCCC"};

    memset(s_bg, 0, sizeof(s_bg));
    for (int e = 0; e < 16; e++) {
        make_block(&s_bg[T_BLOCK + e], e, 0, 7);
        s_bg_pal[T_BLOCK + e] = PAL_WORLD;
    }
    for (int e = 0; e < 4; e++) {
        make_block(&s_bg[T_SLAB_LO + e], e | GE_EDGE_T | GE_EDGE_B, 4, 7);
        make_block(&s_bg[T_SLAB_HI + e], e | GE_EDGE_T | GE_EDGE_B, 0, 3);
        s_bg_pal[T_SLAB_LO + e] = s_bg_pal[T_SLAB_HI + e] = PAL_WORLD;
    }
    art(&s_bg[T_SPIKE_UP], SPIKE, ".?ES");
    art(&s_bg[T_SPIKE_DOWN], SPIKE, ".?ES");
    flip_v(&s_bg[T_SPIKE_DOWN]);
    art(&s_bg[T_SPIKE_SM_UP], SPIKE_SM, ".?ES");
    art(&s_bg[T_SPIKE_SM_DOWN], SPIKE_SM, ".?ES");
    flip_v(&s_bg[T_SPIKE_SM_DOWN]);
    for (int i = 0; i < 4; i++) s_bg_pal[T_SPIKE_UP + i] = PAL_WORLD;

    /* (its dashed ring is 12 pixels, a tooth 2 of them: what a turn can
     * show is the dashes and the gaps changing places, so the frames are
     * half a tooth apart, the two pictures in turn; and an eighth of a
     * tooth on, so that no pixel's middle is on a tooth's edge, its side
     * decided by rounding) */
    for (int f = 0; f < SAW_FRAMES; f++) saw_tile(&s_saw[f], (f * 0.5 + 0.125) * (2.0 * PI / 6.0));
    s_bg[T_SAW] = s_saw[0];
    s_bg_pal[T_SAW] = PAL_WORLD;

    /* orbs and pads: yellow, pink (palette YP), blue, green (palette BG) */
    static const char *const OKEYS[4] = {".C?W", ".?CW", ".C?W", ".?CW"};
    for (int i = 0; i < 4; i++) {
        art(&s_bg[T_ORB_YELLOW + i], ORB, OKEYS[i]);
        s_bg_pal[T_ORB_YELLOW + i] = i < 2 ? PAL_YP : PAL_BG;
    }
    for (int i = 0; i < 3; i++) {
        art(&s_bg[T_PAD_YELLOW + i], PAD, OKEYS[i]);
        art(&s_bg[T_PAD_YELLOW_C + i], PAD, OKEYS[i]);
        flip_v(&s_bg[T_PAD_YELLOW_C + i]);
        s_bg_pal[T_PAD_YELLOW + i] = s_bg_pal[T_PAD_YELLOW_C + i] = i < 2 ? PAL_YP : PAL_BG;
    }

    for (int k = 0; k < T_PORTAL_KINDS; k++) {
        PortalCtx pc = {PORTAL_COL[k]};
        uint8_t cv[8 * 24];
        if (k >= OBJ_SPEED_0 - OBJ_PORTAL_CUBE) speed_canvas(cv, k - (OBJ_SPEED_0 - OBJ_PORTAL_CUBE) + 1, PORTAL_COL[k]);
        else shade_canvas(cv, 8, 24, shade_portal, &pc, 3);
        for (int part = 0; part < 3; part++) {
            canvas_tile(cv, 8, 0, part, &s_bg[T_PORTAL + 3 * k + part]);
            s_bg_pal[T_PORTAL + 3 * k + part] = PORTAL_PAL[k];
        }
    }

    {
        uint8_t cv[64];
        shade_canvas(cv, 8, 8, shade_coin, NULL, 2);
        for (int i = 0; i < 4; i++) {
            canvas_tile(cv, 8, 0, 0, &s_bg[T_COIN + i]);
            s_bg_pal[T_COIN + i] = PAL_GD;
        }
    }

    /* ground (palette GROUND: ground, dark, line, separator) */
    static const char *const GTOP[8] = {"LLLLLLLL", "gggggggg", "gggggggg", "gggggggg",
                                        "gggggggg", "gggggggg", "gggggggg", "gggggggg"};
    static const char *const GLOW[8] = {"gggggggg", "gggggggg", "gdgdgdgd", "dgdgdgdg",
                                        "dddddddd", "dddddddd", "dddddddd", "dddddddd"};
    static const char *const CEIL[8] = {"gggggggg", "gggggggg", "gggggggg", "gggggggg",
                                        "gggggggg", "gggggggg", "gggggggg", "LLLLLLLL"};
    const char *const *gsrc[4] = {GTOP, GLOW, CEIL, NULL};
    for (int i = 0; i < 4; i++) {
        Tile *a = &s_bg[T_GROUND_TOP + 2 * i], *b = a + 1;
        if (gsrc[i]) art(a, gsrc[i], "gdL");
        else memset(a, 0, sizeof(*a));
        *b = *a;
        for (int y = 0; y < 8; y++)
            if (b->p[y][0] != 2) b->p[y][0] = 3;
        s_bg_pal[T_GROUND_TOP + 2 * i] = s_bg_pal[T_GROUND_TOP + 2 * i + 1] = PAL_GROUND;
    }

    /* the finish line (render.c's end gate): a white line at the level's
     * end, 2 pixels, in a white glow rising from 3 blocks before it to 55%
     * and fading over 2 blocks after, dithered over the sky (palette YP:
     * sky, -, -, white) */
    {
        static const uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        for (int k = 0; k < T_FINISH_COLS; k++) {
            Tile *t = &s_bg[T_FINISH + k];
            for (int x = 0; x < 8; x++) {
                int px = (k - 3) * 8 + x; /* pixels from the line */
                double a = px < 0 ? 0.55 * (px + 24) / 24.0 : 0.55 * (1.0 - px / 16.0);
                for (int y = 0; y < 8; y++)
                    t->p[y][x] = px == -1 || px == 0 || a * 16 > BAYER[y & 3][x & 3] + 0.5 ? 3 : 0;
            }
            s_bg_pal[T_FINISH + k] = PAL_YP;
        }
    }
    s_bg_count = T_LEVEL_COUNT;
}

/* --- font, HUD and the logo --- */

static void glyph_tile(Tile *t, char ch, int fg)
{
    uint8_t rows[7];
    memset(t, 0, sizeof(*t));
    if (!font_glyph(ch, rows)) return;
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 5; x++)
            if (rows[y] & (1 << (4 - x))) t->p[y][x + 1] = (uint8_t)fg;
}

static void make_ui_tiles(void)
{
    memset(s_ui, 0, sizeof(s_ui));
    for (int c = 32; c < 96; c++) glyph_tile(&s_ui[UT_FONT + c - 32], (char)c, 1);
    for (int i = 0; SKYFONT_CHARS[i]; i++) glyph_tile(&s_ui[UT_SKYFONT + i], SKYFONT_CHARS[i], 3);
    glyph_tile(&s_ui[UT_ARROW_L], '\x06', 2);
    glyph_tile(&s_ui[UT_ARROW_R], '\x07', 2);
    glyph_tile(&s_ui[UT_STAR], '\x05', 2);
    static const char *const COIN[8] = {"..XXXX..", ".XX..XX.", "XX....XX", "X......X",
                                        "X......X", "XX....XX", ".XX..XX.", "..XXXX.."};
    static const char *const COINF[8] = {"..XXXX..", ".XXXXXX.", "XXWXXXXX", "XXWXXXXX",
                                         "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX.."};
    art(&s_ui[UT_COIN_NO], COIN, ".X");
    art(&s_ui[UT_COIN_YES], COINF, ".WX");
    for (int y = 1; y <= 6; y++) {
        s_ui[UT_BAR_L].p[y][7] = 1;
        s_ui[UT_BAR_R].p[y][0] = 1;
    }
    for (int k = 0; k <= 8; k++) {
        Tile *t = &s_ui[UT_BAR + k];
        for (int x = 0; x < 8; x++) {
            t->p[1][x] = t->p[6][x] = 1;
            for (int y = 2; y <= 5; y++) t->p[y][x] = (uint8_t)(x < k ? 2 : 3);
        }
    }
    static const char *const DIAMOND[8] = {"...X....", "..XXX...", ".XXXXX..", "XXXXXXX.",
                                           ".XXXXX..", "..XXX...", "...X....", "........"};
    art(&s_ui[UT_DIAMOND], DIAMOND, ".?X");
    for (int c = 1; c <= 3; c++) {
        Tile t;
        memset(&t, c, sizeof(t));
        half_down(&t, &s_ui[UT_SOLID + 2 * (c - 1)]);
    }
    /* the garage's cursor, a white box one pixel around a tile (sprite
     * colour 2 is white in the effects' palette) */
    for (int k = 0; k < 8; k++) {
        s_ui[ST_BOX].p[0][k] = s_ui[ST_BOX].p[7][k] = s_ui[ST_BOX].p[k][0] = s_ui[ST_BOX].p[k][7] = 2;
    }
}

/* "PULSE" over "DASH", in the font at 3x with an outline, top white,
 * bottom gold (palette TEXT: background, white, gold, outline). */
static void make_logo(void)
{
    enum { W = LOGO_W * 8, H = LOGO_H * 8, S = 3 };
    static uint8_t px[H][W];
    memset(px, 0, sizeof(px));
    const char *lines[2] = {"PULSE", "DASH"};
    for (int l = 0; l < 2; l++) {
        int n = (int)strlen(lines[l]);
        int tw = n * 6 * S - S;
        int x0 = (W - tw) / 2, y0 = 4 + l * (7 * S + 7);
        for (int i = 0; i < n; i++) {
            uint8_t rows[7];
            if (!font_glyph(lines[l][i], rows)) continue;
            for (int gy = 0; gy < 7 * S; gy++)
                for (int gx = 0; gx < 5 * S; gx++) {
                    if (!(rows[gy / S] & (1 << (4 - gx / S)))) continue;
                    int x = x0 + i * 6 * S + gx, y = y0 + gy;
                    px[y][x] = (uint8_t)(gy < 4 * S - 1 ? 1 : (gy == 4 * S - 1 ? ((x + y) & 1 ? 1 : 2) : 2));
                }
        }
    }
    /* outline: empty pixels next to the letters */
    static uint8_t out[H][W];
    memcpy(out, px, sizeof(px));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (px[y][x]) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < W && yy < H && px[yy][xx]) out[y][x] = 3;
                }
        }
    /* cut into tiles, reusing identical ones */
    s_logo_count = 0;
    for (int ty = 0; ty < LOGO_H; ty++)
        for (int tx = 0; tx < LOGO_W; tx++) {
            Tile t;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) t.p[y][x] = out[ty * 8 + y][tx * 8 + x];
            int found = -1;
            for (int i = 0; i < s_logo_count && found < 0; i++)
                if (!memcmp(&s_bg[BT_LOGO + i], &t, sizeof(t))) found = i;
            if (found < 0) {
                if (BT_LOGO + s_logo_count > 255) die("logo needs too many tiles");
                found = s_logo_count++;
                s_bg[BT_LOGO + found] = t;
                s_bg_pal[BT_LOGO + found] = PAL_TEXT;
            }
            s_logo_map[ty][tx] = (uint8_t)(BT_LOGO + found);
        }
}

/* --- sprites --- */

/* Rotate art (w x h, centred) by deg clockwise into a 16x16 frame: the art
 * k times as big (scale2x), each frame pixel the colour most of its
 * samples fall on there. */
typedef struct {
    const uint8_t *src;
    int w, h, k;
    double co, si;
} RotCtx;

static int shade_rot(double x, double y, const void *ctx)
{
    const RotCtx *r = (const RotCtx *)ctx;
    double px = x - 8.0, py = y - 8.0;
    double ax = (r->co * px + r->si * py + r->w * 0.5) * r->k, ay = (-r->si * px + r->co * py + r->h * 0.5) * r->k;
    int ix = (int)floor(ax), iy = (int)floor(ay);
    if (ix < 0 || iy < 0 || ix >= r->w * r->k || iy >= r->h * r->k) return 0;
    return r->src[iy * r->w * r->k + ix];
}

/*
 * Scale2x (EPX): w x h art twice as big, a pixel's four quarters each
 * taking the colour of the two neighbours beside it where those agree
 * (and the other two don't), so a diagonal edge comes out a diagonal, not
 * a staircase of 2x2 steps. Three times over, the art 8 times as big is
 * what the frames are turned from: turned straight from the art, each
 * edge pixel is a square that the turn breaks into lumps and ears.
 */
static void scale2x(const uint8_t *a, int w, int h, uint8_t *o)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t P = a[y * w + x], A = y > 0 ? a[(y - 1) * w + x] : 0, D = y < h - 1 ? a[(y + 1) * w + x] : 0;
            uint8_t C = x > 0 ? a[y * w + x - 1] : 0, B = x < w - 1 ? a[y * w + x + 1] : 0;
            uint8_t *q = &o[(2 * y) * 2 * w + 2 * x];
            q[0] = C == A && C != D && A != B ? A : P;
            q[1] = A == B && A != C && B != D ? B : P;
            q[2 * w] = D == C && D != B && C != A ? C : P;
            q[2 * w + 1] = B == D && B != A && D != C ? D : P;
        }
}

static void art_canvas(const char *const *rows, int w, int h, uint8_t *out)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const char *k = strchr(".PSK", rows[y][x]);
            out[y * w + x] = (uint8_t)(k && rows[y][x] ? k - ".PSK" : 0);
        }
}

/* 16x16 frame into four tiles at base: left column (top, bottom), right column. */
static void put_frame(int base, const uint8_t *cv)
{
    canvas_tile(cv, 16, 0, 0, &s_ui[base]);
    canvas_tile(cv, 16, 0, 1, &s_ui[base + 1]);
    canvas_tile(cv, 16, 1, 0, &s_ui[base + 2]);
    canvas_tile(cv, 16, 1, 1, &s_ui[base + 3]);
}

/* The art turned in n frames, deg0, deg0 + step, ...: upright, the art as
 * it is; turned, from it 8 times as big (scale2x). round: the art is round
 * (the ball), so every frame keeps the upright one's outline and only
 * what is inside it turns. */
static void rotated_frames(const char *const *rows, int w, int h, int base, int n, double deg0, double step, int round)
{
    static uint8_t src[16 * 16], s2[32 * 32], s4[64 * 64], s8[128 * 128];
    uint8_t cv[16 * 16], up[16 * 16];
    art_canvas(rows, w, h, src);
    scale2x(src, w, h, s2);
    scale2x(s2, 2 * w, 2 * h, s4);
    scale2x(s4, 4 * w, 4 * h, s8);
    RotCtx r0 = {src, w, h, 1, 1.0, 0.0};
    shade_canvas(up, 16, 16, shade_rot, &r0, 3);
    for (int f = 0; f < n; f++) {
        double deg = deg0 + step * f, a = deg * PI / 180.0;
        RotCtx r = {s8, w, h, 8, cos(a), sin(a)};
        if (fabs(deg) < 1e-9) memcpy(cv, up, sizeof(cv));
        else shade_canvas(cv, 16, 16, shade_rot, &r, 3);
        if (round)
            for (int i = 0; i < 16 * 16; i++) {
                int x = i % 16, y = i / 16;
                int edge = up[i] && (x == 0 || y == 0 || x == 15 || y == 15 || !up[i - 1] || !up[i + 1] || !up[i - 16] ||
                                     !up[i + 16]);
                cv[i] = !up[i] ? 0 : edge ? up[i] : cv[i] ? cv[i] : up[i];
            }
        put_frame(base + 4 * f, cv);
    }
}

/* The garage's eight cube icons (src/core/icons.c, the same names and
 * order), at the Game Boy's size: P primary colour, S secondary, K outline. */
static const char *const ICON_ART[ICON_COUNT][8] = {
    {"KKKKKKKK", "KPPPPPPK", "KPKKKKPK", "KPKSSKPK", "KPKSSKPK", "KPKKKKPK", "KPPPPPPK", "KKKKKKKK"}, /* CORE */
    {"KKKKKKKK", "KPPPPPPK", "KKKKKKKK", "KKSSSSKK", "KKKKKKKK", "KPPPPPPK", "KPKKKKPK", "KKKKKKKK"}, /* VISOR */
    {"KKKKKKKK", "KPPPPPPK", "KPSPPSPK", "KPSPPSPK", "KPPPPPPK", "KPKKKKPK", "KPPPPPPK", "KKKKKKKK"}, /* BUDDY */
    {"KKKKKKKK", "KPPPPPKK", "KPPPPKSK", "KPPPKSSK", "KPPKSSSK", "KPKSSKKK", "KKSSSKKK", "KKKKKKKK"}, /* SPLIT */
    {"KKKKKKKK", "KPPPPPPK", "KPSSSSPK", "KPSKKSPK", "KPSKKSPK", "KPSSSSPK", "KPPPPPPK", "KKKKKKKK"}, /* TARGET */
    {"KKKKKKKK", "KPPKKPPK", "KPPSSPPK", "KKSSSSKK", "KKSSSSKK", "KPPSSPPK", "KPPKKPPK", "KKKKKKKK"}, /* PLUS */
    {"KKKKKKKK", "KPPPPPPK", "KSPSSPSK", "KSPSSPSK", "KSPSSPSK", "KSPSSPSK", "KPPPPPPK", "KKKKKKKK"}, /* STRIPE */
    {"KKKKKKKK", "KPPKKPPK", "KPKSSKPK", "KKSPPSKK", "KKSPPSKK", "KPKSSKPK", "KPPKKPPK", "KKKKKKKK"}, /* GEM */
};
/* each icon's cube frames, as ST_CUBE holds them (the ROM copies the
 * player's there) */
static Tile s_icons[ICON_COUNT][CUBE_FRAMES * 4];

static void make_sprites(void)
{
    static const char *const SHIP[10] = {
        "......KKKK....",
        ".KK...KPPK....",
        ".KSK..KPPK....",
        ".KSSK.KKKKKK..",
        "..KKKKKKKKKKKK",
        ".KPPPPPPPPPPPK",
        ".KSSSSSSSSSSK.",
        ".KPPPPPPPPPKK.",
        "..KKKKKKKKK...",
        "..............",
    };
    static const char *const BALL[8] = {"..KKKK..", ".KPKKPK.", "KPPKKPPK", "KKKSSKKK",
                                        "KKKSSKKK", "KPPKKPPK", ".KPKKPK.", "..KKKK.."};
    static const char *const UFO[10] = {
        "....KKKK....",
        "....KPPK....",
        "...KKPPKK...",
        "..K.KKKK.K..",
        ".KKKKKKKKKK.",
        "KPPPPPPPPPPK",
        ".KKKSSSSKKK.",
        "...KKKKKK...",
        "............",
        "............",
    };
    static const char *const WAVE[8] = {"KK......", "KPKK....", "KPPPKK..", "KPSSPPK.",
                                        "KPSSPPK.", "KPPPKK..", "KPKK....", "KK......"};
    for (int i = 0; i < ICON_COUNT; i++) {
        rotated_frames(ICON_ART[i], 8, 8, ST_CUBE, CUBE_FRAMES, 0.0, 15.0, 0);
        memcpy(s_icons[i], &s_ui[ST_CUBE], sizeof(s_icons[i]));
        Tile t;
        art(&t, ICON_ART[i], ".PSK");
        half_down(&t, &s_ui[UT_ICON + 2 * i]);
    }
    memcpy(&s_ui[ST_CUBE], s_icons[0], sizeof(s_icons[0]));
    rotated_frames(SHIP, 14, 10, ST_SHIP, SHIP_FRAMES, -30.0, 10.0, 0);
    rotated_frames(BALL, 8, 8, ST_BALL, BALL_FRAMES, 0.0, 22.5, 1);
    rotated_frames(UFO, 12, 10, ST_UFO, UFO_FRAMES, -15.0, 15.0, 0);
    rotated_frames(WAVE, 8, 8, ST_WAVE, WAVE_FRAMES, -45.0, 45.0, 0);

    /* effects (8x16 sprites: art in the top tile) */
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 3; x++) s_ui[ST_PART].p[y][x] = 1;
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++) {
            s_ui[ST_PART_SM].p[y][x] = 2;
            s_ui[ST_DOT].p[y][x] = 2;
        }
    static const char *const CHECK[8] = {"...W....", "..WGW...", ".WGGGW..", "WGGDGGW.",
                                         ".WGGGW..", "..WGW...", "...W....", "........"};
    art(&s_ui[ST_CHECK], CHECK, ".GWD");
    for (int f = 0; f < 2; f++) {
        uint8_t cv[256];
        double r = f ? 7.2 : 5.0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                double d = sqrt((x + 0.5 - 8) * (x + 0.5 - 8) + (y + 0.5 - 8) * (y + 0.5 - 8));
                cv[y * 16 + x] = (uint8_t)(fabs(d - r) < 0.6 ? 2 : 0);
            }
        put_frame(ST_RING + 4 * f, cv);
    }
}

static void make_all_tiles(void)
{
    make_level_tiles();
    make_ui_tiles();
    make_logo();
    make_sprites();
}

/* ------------------------------------------------------------------ */
/* Palettes                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t r, g, b;
} Rgb;

static Rgb rgb_of(Color c) { return (Rgb){(uint8_t)COL_R(c), (uint8_t)COL_G(c), (uint8_t)COL_B(c)}; }
static Rgb rgb_mix(Rgb a, Rgb b, double t)
{
    return (Rgb){(uint8_t)lround(a.r + (b.r - a.r) * t), (uint8_t)lround(a.g + (b.g - a.g) * t),
                 (uint8_t)lround(a.b + (b.b - a.b) * t)};
}
static Rgb rgb_scale(Rgb a, double k)
{
    return (Rgb){(uint8_t)fmin(255, lround(a.r * k)), (uint8_t)fmin(255, lround(a.g * k)),
                 (uint8_t)fmin(255, lround(a.b * k))};
}

static void level_colors(int pal, Rgb out[LC_COUNT])
{
    const Palette *p = &g_palettes[clampi(pal, 0, PALETTE_COUNT - 1)];
    Rgb sky = rgb_mix(rgb_of(p->bg_top), rgb_of(p->bg_bot), 0.5);
    out[LC_SKY] = sky;
    out[LC_FILL] = rgb_mix(sky, rgb_of(p->block_fill), COL_A(p->block_fill) / 255.0);
    out[LC_EDGE] = rgb_of(p->block_edge);
    out[LC_SPIKE] = rgb_mix(sky, (Rgb){6, 6, 10}, 235 / 255.0);
    out[LC_GROUND] = rgb_of(p->ground);
    out[LC_GROUND_DARK] = rgb_scale(rgb_of(p->ground), 0.55);
    out[LC_LINE] = rgb_of(p->ground_line);
    out[LC_SEP] = rgb_scale(rgb_of(p->ground), 0.78);
    out[LC_HUD] = rgb_scale(sky, 0.30);
    out[LC_HUD_DIM] = rgb_scale(sky, 0.62);
    for (int k = 0; k < SKY_BANDS; k++)
        out[LC_BAND + k] = rgb_mix(rgb_of(p->bg_top), rgb_of(p->bg_bot), (k + 0.5) / SKY_BANDS);
}

/* Colours that don't change with the level (exported for the ROM) */
static const Rgb FIXED[FC_COUNT] = {
    [FC_YELLOW] = {255, 226, 40}, [FC_PINK] = {255, 84, 200}, [FC_BLUE] = {50, 170, 255},
    [FC_GREEN] = {70, 255, 110},  [FC_WHITE] = {255, 255, 255}, [FC_ORANGE] = {255, 96, 48},
    [FC_CYAN] = {40, 222, 255},   [FC_GOLD] = {255, 194, 40},  [FC_DARK_GOLD] = {176, 110, 16},
    [FC_BAR] = {80, 255, 120},    [FC_MENU] = {16, 20, 48},    [FC_MENU_GOLD] = {255, 206, 52},
    [FC_BLACK] = {6, 6, 12},
};

/* The eight background palettes for a level palette, as the ROM builds
 * them (video.c, pal_level). */
static void bg_palettes(int pal, Rgb out[8][4])
{
    Rgb c[LC_COUNT];
    level_colors(pal, c);
    Rgb s = c[LC_SKY];
    const Rgb *F = FIXED;
    Rgb p[8][4] = {
        {s, c[LC_FILL], c[LC_EDGE], c[LC_SPIKE]},
        {c[LC_GROUND], c[LC_GROUND_DARK], c[LC_LINE], c[LC_SEP]},
        {s, F[FC_YELLOW], F[FC_PINK], F[FC_WHITE]},
        {s, F[FC_BLUE], F[FC_GREEN], F[FC_WHITE]},
        {s, F[FC_ORANGE], F[FC_CYAN], F[FC_WHITE]},
        {s, F[FC_GOLD], F[FC_DARK_GOLD], F[FC_WHITE]},
        {c[LC_HUD], F[FC_WHITE], F[FC_BAR], c[LC_HUD_DIM]},
        {F[FC_MENU], F[FC_WHITE], F[FC_MENU_GOLD], F[FC_BLACK]},
    };
    memcpy(out, p, sizeof(p));
}

static const Rgb OBJ_PAL[3][4] = {
    {{0, 0, 0}, {255, 204, 0}, {0, 222, 255}, {8, 8, 12}},
    {{0, 0, 0}, {255, 204, 0}, {255, 255, 255}, {0, 222, 255}},
    {{0, 0, 0}, {80, 255, 120}, {255, 255, 255}, {20, 110, 50}},
};

/* ------------------------------------------------------------------ */
/* Music                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *b;
    int n, cap;
    int loop_off; /* byte offset of the loop point */
} Stream;

static void sbyte(Stream *s, int v)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 1024;
        s->b = (uint8_t *)realloc(s->b, (size_t)s->cap);
    }
    s->b[s->n++] = (uint8_t)v;
}

static void swait(Stream *s, long ticks)
{
    while (ticks > 0) {
        if (ticks <= MS_WAIT_MAX) {
            sbyte(s, MS_WAIT + (int)ticks - 1);
            return;
        }
        long w = ticks > 65535 ? 65535 : ticks;
        sbyte(s, MS_LONGWAIT);
        sbyte(s, (int)(w & 255));
        sbyte(s, (int)(w >> 8));
        ticks -= w;
    }
}

/* An event on a channel: note on (note > 0), note off (0), instrument (inst >= 0). */
typedef struct {
    long tick;
    int note, inst;
} CEv;

typedef struct {
    CEv *e;
    int n, cap;
} CEvList;

static void cev(CEvList *l, long tick, int note, int inst)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 1024;
        l->e = (CEv *)realloc(l->e, sizeof(CEv) * (size_t)l->cap);
    }
    l->e[l->n++] = (CEv){tick, note, inst};
}

static long step_tick(uint32_t step, int bpm) { return lround(step * 900.0 / bpm); }

static int gb_inst(int ins)
{
    switch (ins) {
    case INS_PLUCK: return GI_PLUCK;
    case INS_PLUCK_SAW: return GI_PLUCK_SAW;
    case INS_LEAD_SUPER: return GI_LEAD;
    case INS_LEAD_SQUARE: return GI_LEAD_SQUARE;
    case INS_LEAD_PULSE: return GI_LEAD_THIN;
    case INS_BELL: return GI_BELL;
    case INS_CHIP_TRI: return GI_CHIP;
    case INS_BASS_SAW: return GI_WAVE_SAW;
    case INS_BASS_SQUARE: return GI_WAVE_SQUARE;
    case INS_BASS_SUB: return GI_WAVE_TRI;
    default: return GI_PAD;
    }
}

/* Instruments that keep sounding until the note's end (the rest decay). */
static int gb_sustains(int gi) { return gi == GI_LEAD || gi == GI_LEAD_THIN || gi == GI_CHIP || gi >= GI_WAVE_SAW; }

static int fit_note(int note, int lo)
{
    while (note < lo) note += 12;
    while (note > 119) note -= 12;
    return note;
}

static int drum_of(int track, int ch)
{
    if (track == TR_KICK) return ch == 'r' ? DR_KICK_SOFT : DR_KICK;
    if (track == TR_SNARE) return ch == 'X' ? DR_SNARE_ACC : ch == 'c' ? DR_CLAP : ch == 'r' ? DR_ROLL : DR_SNARE;
    return ch == 'X' ? DR_HAT_ACC : ch == 'o' ? DR_HAT_OPEN : DR_HAT;
}

/* Turn a channel's events into its stream, the loop point at loop_tick. */
static void emit_stream(Stream *s, CEvList *l, long loop_tick, long end_tick)
{
    long t = 0;
    s->loop_off = -1;
    int cur_inst = -1;
    for (int i = 0; i <= l->n; i++) {
        long et = i < l->n ? l->e[i].tick : end_tick;
        if (s->loop_off < 0 && et >= loop_tick) {
            swait(s, loop_tick - t);
            t = loop_tick;
            s->loop_off = s->n;
            cur_inst = -1; /* restate the instrument after looping */
        }
        if (i == l->n) break;
        const CEv *e = &l->e[i];
        swait(s, e->tick - t);
        t = e->tick;
        if (e->inst >= 0) {
            if (e->inst != cur_inst) {
                sbyte(s, MS_INST);
                sbyte(s, e->inst);
                cur_inst = e->inst;
            }
        } else {
            sbyte(s, e->note);
        }
    }
    swait(s, end_tick - t);
    sbyte(s, MS_END);
    if (s->loop_off < 0) s->loop_off = 0;
}

typedef struct {
    Stream ch[4];
    int bpm;
} GSong;

/* Melodic notes of one track, `shift` semitones up: on at their start,
 * off at their end where the next note doesn't start right then. */
static void track_notes(CEvList *l, const AudioNote *ev, int nev, int track, int gi, int bpm, int lo, int shift)
{
    long last_off = -1;
    int top = -1;
    cev(l, 0, 0, gi);
    for (int i = 0; i < nev; i++) {
        if (ev[i].track != track) continue;
        /* chords: the highest note */
        int j = i, note = ev[i].note;
        while (j + 1 < nev && ev[j + 1].step == ev[i].step) {
            j++;
            if (ev[j].track == track && ev[j].note > note) note = ev[j].note;
        }
        long on = step_tick(ev[i].step, bpm), off = step_tick(ev[i].step + ev[i].len, bpm);
        if (last_off >= 0 && last_off < on) cev(l, last_off, 0, -1);
        cev(l, on, fit_note(note + shift, lo), -1);
        last_off = gb_sustains(gi) ? off : -1;
        top = note;
        i = j;
    }
    if (last_off >= 0) cev(l, last_off, 0, -1);
    (void)top;
}

static int ev_order(const void *a, const void *b)
{
    const CEv *x = (const CEv *)a, *y = (const CEv *)b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    /* instrument before note at the same tick */
    return (x->inst >= 0 ? 0 : 1) - (y->inst >= 0 ? 0 : 1);
}

static void build_song(int song, GSong *gs)
{
    const AudioNote *ev;
    uint32_t len, loop;
    int nev = audio_song_notes(song, &ev, &len, &loop);
    const SongDef *def = g_songs[song];
    int bpm = (int)lround(def->bpm);
    memset(gs, 0, sizeof(*gs));
    gs->bpm = bpm;
    long end = step_tick(len, bpm), loop_t = step_tick(loop, bpm);
    CEvList l[4];
    memset(l, 0, sizeof(l));

    /* channel 1: lead, else extra, else the pad's top note */
    {
        static const int pri[3] = {TR_LEAD, TR_EXTRA, TR_PAD};
        int owner_prev = -1, note_prev = -1;
        for (uint32_t st = 0; st < len; st++) {
            int owner = -1, note = -1, starts = 0;
            for (int p = 0; p < 3 && owner < 0; p++) {
                for (int i = 0; i < nev; i++) {
                    if (ev[i].track != pri[p] || ev[i].step > st || ev[i].step + ev[i].len <= st) continue;
                    if (owner < 0 || ev[i].note > note) {
                        owner = pri[p];
                        note = ev[i].note;
                        starts = ev[i].step == st;
                    }
                }
            }
            long t = step_tick(st, bpm);
            if (owner < 0) {
                if (owner_prev >= 0) cev(&l[0], t, 0, -1);
            } else if (starts || owner != owner_prev || note != note_prev) {
                int gi = owner == TR_PAD ? GI_PAD : gb_inst(def->inst[owner]);
                cev(&l[0], t, 0, gi);
                cev(&l[0], t, fit_note(note, 36), -1);
            }
            owner_prev = owner;
            note_prev = note;
        }
    }
    /* channel 2: arpeggio; channel 3: bass. The saw and square bass play
     * an octave up (55 Hz becomes 110): what the other versions play at
     * 40-130 Hz is little more than a hum on a Game Boy's speaker, with
     * the wave channel's 32 steps buzzing over it at under 2 kHz. The sub
     * bass (a soft triangle, under the calm songs) keeps its octave. */
    {
        int gi = gb_inst(def->inst[TR_BASS]);
        track_notes(&l[1], ev, nev, TR_ARP, gb_inst(def->inst[TR_ARP]), bpm, 36, 0);
        track_notes(&l[2], ev, nev, TR_BASS, gi, bpm, 24, gi == GI_WAVE_TRI ? 0 : 12);
    }
    /* channel 4: drums, the strongest of each step */
    for (int i = 0; i < nev;) {
        int best = -1, dr = 0;
        uint32_t st = ev[i].step;
        for (; i < nev && ev[i].step == st; i++) {
            if (ev[i].track > TR_HAT) continue;
            if (best < 0 || ev[i].track < best) {
                best = ev[i].track;
                dr = drum_of(ev[i].track, ev[i].note);
            }
        }
        if (best >= 0) cev(&l[3], step_tick(st, bpm), dr, -1);
    }
    for (int c = 0; c < 4; c++) {
        qsort(l[c].e, (size_t)l[c].n, sizeof(CEv), ev_order);
        emit_stream(&gs->ch[c], &l[c], loop_t, end);
        free(l[c].e);
    }
}

/* The songs the Game Boy plays: the menu's, practice's and its levels'. */
static int song_exported(int song)
{
    if (song == SONG_MENU || song == SONG_PRACTICE) return 1;
    gb_levels(0);
    for (int i = 0; i < s_gb_count; i++) {
        LevelInfo info;
        level_info(s_gb_level[i], &info);
        if (SONG_FIRST_LEVEL + info.song == song) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Export                                                              */
/* ------------------------------------------------------------------ */

static FILE *open_out(const char *dir, const char *name)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(1);
    }
    fprintf(f, "/* Generated by gbc_tool export from the game's sources; do not edit. */\n");
    return f;
}

static void put_bytes(FILE *f, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) fprintf(f, "%s0x%02x,%s", i % 16 ? "" : "    ", b[i], i % 16 == 15 || i + 1 == n ? "\n" : "");
}

static void put_tiles(FILE *f, const char *name, const Tile *t, int n)
{
    fprintf(f, "const uint8_t %s[%d] = {\n", name, n * 16);
    for (int i = 0; i < n; i++) {
        uint8_t b[16];
        tile_2bpp(&t[i], b);
        put_bytes(f, b, 16);
    }
    fprintf(f, "};\n");
}

static void put_rgb(FILE *f, Rgb c) { fprintf(f, "{%d, %d, %d}", c.r, c.g, c.b); }

/* git describe's version, shortened for the title screen's 20 columns:
 * "v1.1.0-6-g9906db9-dirty" -> "v1.1.0-6-9906db9*" (the ROM cuts what is
 * still too long) */
static void short_version(const char *v, char *out, size_t size)
{
    size_t n = 0, len = strlen(v);
    int dirty = len >= 6 && !strcmp(v + len - 6, "-dirty");
    if (dirty) len -= 6;
    for (size_t i = 0; i < len && n + 2 < size; i++) {
        /* the "g" before the commit hash */
        if (v[i] == 'g' && i > 0 && v[i - 1] == '-' && i + 1 < len && isxdigit((unsigned char)v[i + 1])) continue;
        if (v[i] == '"' || v[i] == '\\') continue;
        out[n++] = v[i];
    }
    if (dirty) out[n++] = '*';
    out[n] = 0;
}

static int cmd_export(const char *dir, const char *version)
{
    char name[64];
    make_all_tiles();

    /* the levels that fit, one bank each */
    GLevel lv[GB_MAX_LEVELS];
    gb_levels(1);
    if (!s_gb_count) die("no level fits the Game Boy");
    for (int i = 0; i < s_gb_count; i++) {
        build_level(s_gb_level[i], &lv[i]);
        snprintf(name, sizeof(name), "level%d.c", i);
        FILE *f = open_out(dir, name);
        fprintf(f, "#pragma bank %d\n#include <stdint.h>\n\n", BANK_LEVEL0 + i);
        fprintf(f, "/* %s: %d columns of %d tiles */\n", lv[i].name, lv[i].width, GS_ROWS);
        fprintf(f, "const uint8_t level%d_cells[%d] = {\n", i, lv[i].width * GS_ROWS);
        put_bytes(f, lv[i].cells, (size_t)lv[i].width * GS_ROWS);
        fprintf(f, "};\n");
        fclose(f);
    }

    /* the title's demo run: its level and its presses (as the other
     * versions' title screen plays them, src/core/demo.c), in a bank */
    GLevel demo;
    if (build_level_src(demo_level_src(), -1, &demo) < 0) die(s_unfit);
    {
        FILE *f = open_out(dir, "demo.c");
        fprintf(f, "#pragma bank %d\n#include <stdint.h>\n\n", BANK_DEMO);
        fprintf(f, "/* %d columns of %d tiles */\nconst uint8_t demo_cells[%d] = {\n", demo.width, GS_ROWS,
                demo.width * GS_ROWS);
        put_bytes(f, demo.cells, (size_t)demo.width * GS_ROWS);
        fprintf(f, "};\n\n/* the button is held while x (16.16) is in [x0, x1), or when a tick steps\n"
                   " * over the whole range: the other versions' x >= x0 and x < x1 in floats */\n");
        fprintf(f, "const uint32_t gbc_demo_press[%d][2] = {\n", demo_press_count());
        for (int i = 0; i < demo_press_count(); i++) {
            float x0, x1;
            demo_press(i, &x0, &x1);
            fprintf(f, "    {%ld, %ld},\n", (long)ceil((double)x0 * 65536.0), (long)ceil((double)x1 * 65536.0));
        }
        fprintf(f, "};\n");
        fclose(f);
    }

    /* songs, packed into banks */
    GSong songs[16];
    int song_bank[16], bank = BANK_SONG0, used = 0;
    for (int s = 0; s < g_song_count; s++) {
        if (!song_exported(s)) continue;
        build_song(s, &songs[s]);
        int size = 0;
        for (int c = 0; c < 4; c++) size += songs[s].ch[c].n;
        if (size > BANK_SIZE) die("song does not fit a bank");
        if (used + size > BANK_SIZE) {
            bank++;
            used = 0;
        }
        song_bank[s] = bank;
        used += size;
    }
    if (bank > BANK_SONG_LAST) die("out of ROM banks for the songs");
    for (int b = BANK_SONG0; b <= bank; b++) {
        snprintf(name, sizeof(name), "songs%d.c", b);
        FILE *f = open_out(dir, name);
        fprintf(f, "#pragma bank %d\n#include <stdint.h>\n\n", b);
        for (int s = 0; s < g_song_count; s++) {
            if (!song_exported(s) || song_bank[s] != b) continue;
            for (int c = 0; c < 4; c++) {
                fprintf(f, "/* %s, channel %d */\nconst uint8_t song%d_ch%d[%d] = {\n", g_songs[s]->title, c + 1, s,
                        c, songs[s].ch[c].n);
                put_bytes(f, songs[s].ch[c].b, (size_t)songs[s].ch[c].n);
                fprintf(f, "};\n");
            }
        }
        fclose(f);
    }

    /* tiles, in their own bank */
    {
        FILE *f = open_out(dir, "gfx.c");
        fprintf(f, "#pragma bank %d\n#include <stdint.h>\n\n", BANK_GFX);
        put_tiles(f, "gfx_bg", s_bg, s_bg_count);
        put_tiles(f, "gfx_logo", &s_bg[BT_LOGO], s_logo_count);
        put_tiles(f, "gfx_ui", s_ui, 256);
        put_tiles(f, "gfx_saw", s_saw, SAW_FRAMES);
        put_tiles(f, "gfx_icons", &s_icons[0][0], ICON_COUNT * CUBE_FRAMES * 4);
        fprintf(f, "const uint8_t gfx_logo_map[%d] = {\n", LOGO_W * LOGO_H);
        put_bytes(f, &s_logo_map[0][0], LOGO_W * LOGO_H);
        fprintf(f, "};\n");
        fclose(f);
    }

    /* bank 0: tables the game reads while a level's bank is mapped */
    {
        FILE *f = open_out(dir, "gbc_data.c");
        fprintf(f, "#include \"gbc_data.h\"\n\n");
        char ver[64];
        short_version(version, ver, sizeof(ver));
        fprintf(f, "const char gbc_version[] = \"%s\";\n\n", ver);
        /* channel 1/2 frequency registers of MIDI notes 36..119: 2048 - 131072 / f
         * (channel 3 plays a note an octave below its register's value) */
        fprintf(f, "const uint16_t gbc_freq[84] = {");
        for (int n = 36; n < 120; n++) {
            double hz = 440.0 * pow(2.0, (n - 69) / 12.0);
            fprintf(f, "%s%ld,", (n - 36) % 12 ? " " : "\n    ", lround(2048.0 - 131072.0 / hz));
        }
        fprintf(f, "\n};\n\n");
        fprintf(f, "/* palette of each background tile */\nconst uint8_t gfx_bg_attr[256] = {\n");
        put_bytes(f, s_bg_pal, 256);
        fprintf(f, "};\n\n");
        for (int i = 0; i < s_gb_count; i++) {
            fprintf(f, "static const GbTrigger trig%d[%d] = {", i, lv[i].ntrig ? lv[i].ntrig : 1);
            for (int t = 0; t < lv[i].ntrig; t++) fprintf(f, "{%d, %d}, ", lv[i].trig[t].x, lv[i].trig[t].pal);
            fprintf(f, "%s};\n", lv[i].ntrig ? "" : "{0, 0}");
        }
        fprintf(f, "\nconst GbLevel gbc_levels[%d] = {\n", s_gb_count);
        for (int i = 0; i < s_gb_count; i++) {
            const GLevel *g = &lv[i];
            fprintf(f, "    {\"%s\", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, level%d_cells, trig%d},\n", g->name,
                    g->diff, g->stars, g->song, g->speed, g->pal, g->height, g->ncoins, BANK_LEVEL0 + i, g->ntrig,
                    s_gb_level[i], g->width, i, i);
        }
        fprintf(f, "};\n\nstatic const GbTrigger trig_demo[1] = {{0, 0}};\n");
        fprintf(f, "const GbLevel gbc_demo_level = {\"DEMO\", 0, 0, %d, %d, %d, %d, 0, %d, 0, 255, %d, demo_cells, "
                   "trig_demo};\n", demo.song, demo.speed, demo.pal, demo.height, BANK_DEMO, demo.width);
        fprintf(f, "\nconst GbSong gbc_songs[%d] = {\n", g_song_count);
        for (int s = 0; s < g_song_count; s++) {
            if (!song_exported(s)) {
                fprintf(f, "    {0, 120, {0, 0, 0, 0}, {0, 0, 0, 0}},\n");
                continue;
            }
            fprintf(f, "    {%d, %d, {song%d_ch0, song%d_ch1, song%d_ch2, song%d_ch3}, {", song_bank[s], songs[s].bpm, s,
                    s, s, s);
            for (int c = 0; c < 4; c++) fprintf(f, "song%d_ch%d + %d%s", s, c, songs[s].ch[c].loop_off, c < 3 ? ", " : "");
            fprintf(f, "}},\n");
        }
        fprintf(f, "};\n");
        fclose(f);
    }

    /* colours, with palette.c */
    {
        FILE *f = open_out(dir, "pals.c");
        fprintf(f, "#pragma bank %d\n#include \"gbc_data.h\"\n\n", BANK_UI);
        /* d * t / 32 for a 5-bit colour difference d and blend t of 32 */
        fprintf(f, "const uint8_t gbc_lerp5[33][32] = {\n");
        for (int t = 0; t <= 32; t++) {
            fprintf(f, "    {");
            for (int d = 0; d < 32; d++) fprintf(f, "%d%s", d * t >> 5, d < 31 ? ", " : "},\n");
        }
        fprintf(f, "};\n\n");
        fprintf(f, "const GbLevelPal gbc_level_pals[%d] = {\n", PALETTE_COUNT);
        for (int p = 0; p < PALETTE_COUNT; p++) {
            Rgb c[LC_COUNT];
            level_colors(p, c);
            fprintf(f, "    {{");
            for (int k = 0; k < LC_COUNT; k++)
                fprintf(f, "{%d, %d, %d, 0}%s", c[k].r >> 3, c[k].g >> 3, c[k].b >> 3, k + 1 < LC_COUNT ? ", " : "}},\n");
        }
        fprintf(f, "};\n\nconst uint8_t gbc_fixed_colors[%d][3] = {", FC_COUNT);
        for (int k = 0; k < FC_COUNT; k++) {
            fprintf(f, k % 4 ? " " : "\n    ");
            put_rgb(f, FIXED[k]);
            fprintf(f, ",");
        }
        fprintf(f, "\n};\n\n/* the garage's player colours (src/core/theme.c), 5-bit */\n");
        fprintf(f, "const uint8_t gbc_player_colors[%d][3] = {", PLAYER_COLOR_COUNT);
        for (int k = 0; k < PLAYER_COLOR_COUNT; k++)
            fprintf(f, "%s{%d, %d, %d},", k % 4 ? " " : "\n    ", (int)COL_R(g_player_colors[k]) >> 3,
                    (int)COL_G(g_player_colors[k]) >> 3, (int)COL_B(g_player_colors[k]) >> 3);
        fprintf(f, "\n};\n\nconst char *const gbc_icon_names[%d] = {", ICON_COUNT);
        for (int k = 0; k < ICON_COUNT; k++) fprintf(f, "\"%s\",%s", g_icon_names[k], k + 1 < ICON_COUNT ? " " : "");
        fprintf(f, "};\n\n/* sprite palettes: player, effects, checkpoints */\nconst uint8_t gbc_obj_pals[3][4][3] = {\n");
        for (int k = 0; k < 3; k++) {
            fprintf(f, "    {");
            for (int c = 0; c < 4; c++) {
                put_rgb(f, OBJ_PAL[k][c]);
                fprintf(f, c < 3 ? ", " : "},\n");
            }
        }
        fprintf(f, "};\n\n");
        fclose(f);
    }

    /* header */
    {
        FILE *f = open_out(dir, "gbc_data.h");
        fprintf(f, "#ifndef GBC_DATA_H\n#define GBC_DATA_H\n\n#include <stdint.h>\n\n");
        fprintf(f, "#include \"../../../src/gbc/leveldata.h\"\n#include \"../../../src/gbc/musicdata.h\"\n\n");
        fprintf(f, "#define GBC_LEVEL_COUNT %d\n#define GBC_SONG_COUNT %d\n#define GBC_BANK_GFX %d\n", s_gb_count,
                g_song_count, BANK_GFX);
        fprintf(f, "#define GBC_ROM_BANKS %d\n#define GFX_BG_COUNT %d\n#define GFX_LOGO_COUNT %d\n", ROM_BANKS,
                s_bg_count, s_logo_count);
        fprintf(f, "#define GFX_LOGO_W %d\n#define GFX_LOGO_H %d\n\n", LOGO_W, LOGO_H);
        fprintf(f, "extern const char gbc_version[];\nextern const uint8_t gfx_bg_attr[256];\n");
        fprintf(f, "extern const uint16_t gbc_freq[84]; /* MIDI notes 36..119 */\n");
        fprintf(f, "#define GBC_PALETTE_COUNT %d\n", PALETTE_COUNT);
        fprintf(f, "/* bank %d */\nextern const GbLevelPal gbc_level_pals[%d];\n", BANK_UI, PALETTE_COUNT);
        fprintf(f, "extern const uint8_t gbc_fixed_colors[FC_COUNT][3];\nextern const uint8_t gbc_obj_pals[3][4][3];\n");
        fprintf(f, "extern const uint8_t gbc_lerp5[33][32];\n");
        fprintf(f, "#define ICON_COUNT %d\n#define PLAYER_COLOR_COUNT %d\n", ICON_COUNT, PLAYER_COLOR_COUNT);
        fprintf(f, "extern const uint8_t gbc_player_colors[PLAYER_COLOR_COUNT][3];\n");
        fprintf(f, "extern const char *const gbc_icon_names[ICON_COUNT];\n");
        fprintf(f, "extern const GbLevel gbc_levels[%d];\nextern const GbSong gbc_songs[%d];\n\n", s_gb_count,
                g_song_count);
        fprintf(f, "/* bank GBC_BANK_GFX */\nextern const uint8_t gfx_bg[], gfx_logo[], gfx_ui[], gfx_saw[], gfx_logo_map[];\n");
        fprintf(f, "extern const uint8_t gfx_icons[]; /* ICON_COUNT x CUBE_FRAMES x 4 tiles */\n");
        for (int i = 0; i < s_gb_count; i++) fprintf(f, "extern const uint8_t level%d_cells[];\n", i);
        fprintf(f, "/* the title's demo run (src/core/demo.c): its level, its presses (in its\n"
                   " * bank), and where it loops: from x = WRAP back by LOOP blocks */\n");
        fprintf(f, "extern const GbLevel gbc_demo_level;\nextern const uint8_t demo_cells[];\n");
        fprintf(f, "#define GBC_DEMO_PRESSES %d\nextern const uint32_t gbc_demo_press[GBC_DEMO_PRESSES][2];\n",
                demo_press_count());
        fprintf(f, "#define GBC_DEMO_LOOP %d\n#define GBC_DEMO_WRAP %d\n", DEMO_LOOP, (int)DEMO_WRAP);
        for (int s = 0; s < g_song_count; s++)
            if (song_exported(s)) fprintf(f, "extern const uint8_t song%d_ch0[], song%d_ch1[], song%d_ch2[], song%d_ch3[];\n", s, s, s, s);
        fprintf(f, "\n#endif\n");
        fclose(f);
    }
    int song_bytes = 0;
    for (int s = 0; s < g_song_count; s++)
        if (song_exported(s))
            for (int c = 0; c < 4; c++) song_bytes += songs[s].ch[c].n;
    printf("exported %d levels, %d background tiles + %d logo tiles, %d bytes of music in banks %d..%d\n",
           s_gb_count, s_bg_count, s_logo_count, song_bytes, BANK_SONG0, bank);
    free(demo.cells);
    for (int i = 0; i < s_gb_count; i++) {
        if (lv[i].portal_overlaps)
            printf("  note: level %d: %d portal ends hidden behind other tiles\n", s_gb_level[i], lv[i].portal_overlaps);
        free(lv[i].cells);
    }
    return 0;
}

static int cmd_music(void)
{
    for (int s = 0; s < g_song_count; s++) {
        if (!song_exported(s)) continue;
        GSong gs;
        build_song(s, &gs);
        printf("song %d %-16s bpm=%3d bytes: ch1=%5d ch2=%5d ch3=%5d ch4=%5d\n", s, g_songs[s]->title, gs.bpm,
               gs.ch[0].n, gs.ch[1].n, gs.ch[2].n, gs.ch[3].n);
        for (int c = 0; c < 4; c++) free(gs.ch[c].b);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Previews                                                            */
/* ------------------------------------------------------------------ */

static void blit(uint8_t *img, int iw, int ih, int x0, int y0, int scale, const Tile *t, const Rgb pal[4],
                 int transparent0, int flipy)
{
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int c = t->p[flipy ? 7 - y : y][x];
            if (transparent0 && !c) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    int px = x0 + x * scale + sx, py = y0 + y * scale + sy;
                    if (px < 0 || py < 0 || px >= iw || py >= ih) continue;
                    uint8_t *d = &img[((size_t)py * iw + px) * 3];
                    d[0] = pal[c].r;
                    d[1] = pal[c].g;
                    d[2] = pal[c].b;
                }
        }
}

static int cmd_sheet(const char *out, int pal)
{
    make_all_tiles();
    Rgb bp[8][4];
    bg_palettes(pal, bp);
    const int S = 3, cols = 16, W = cols * 9 * S, H = 34 * 9 * S;
    uint8_t *img = (uint8_t *)calloc((size_t)W * H * 3, 1);
    for (int i = 0; i < 256; i++) { /* bank 0 */
        blit(img, W, H, (i % cols) * 9 * S, (i / cols) * 9 * S, S, &s_bg[i], bp[s_bg_pal[i]], 0, 0);
    }
    for (int i = 0; i < 128; i++) { /* bank 1: UI */
        const Rgb *p = i >= UT_BAR_L && i <= UT_BAR + 8 ? bp[PAL_HUD] : bp[PAL_TEXT];
        blit(img, W, H, (i % cols) * 9 * S, (16 + i / cols) * 9 * S, S, &s_ui[i], p, 0, 0);
    }
    for (int i = 128; i < 256; i += 4) { /* sprites as 16x16 frames */
        int k = (i - 128) / 4, x = (k % 8) * 18 * S, y = (25 + (k / 8) * 2) * 9 * S;
        const Rgb *p = i >= ST_CHECK && i < ST_RING ? OBJ_PAL[2] : OBJ_PAL[0];
        Rgb sp[4] = {bp[0][0], p[1], p[2], p[3]};
        blit(img, W, H, x, y, S, &s_ui[i], sp, 0, 0);
        blit(img, W, H, x, y + 8 * S, S, &s_ui[i + 1], sp, 0, 0);
        blit(img, W, H, x + 8 * S, y, S, &s_ui[i + 2], sp, 0, 0);
        blit(img, W, H, x + 8 * S, y + 8 * S, S, &s_ui[i + 3], sp, 0, 0);
    }
    int r = png_write_rgb(out, img, W, H);
    free(img);
    return r;
}

/* The screen with the player at column x, standing on the ground, as the
 * ROM lays it out: progress bar row, level rows 14..0, two ground rows. */
static int cmd_view(int idx, double px, const char *out)
{
    make_all_tiles();
    GLevel g;
    if (build_level(idx, &g) < 0) die(s_unfit);
    const int S = 3, W = 160 * S, H = 144 * S;
    uint8_t *img = (uint8_t *)calloc((size_t)W * H * 3, 1);
    int pal = g.pal;
    for (int t = 0; t < g.ntrig; t++)
        if (g.trig[t].x <= px) pal = g.trig[t].pal;
    Rgb bp[8][4], lc[LC_COUNT];
    bg_palettes(pal, bp);
    level_colors(pal, lc);
    int cam = (int)lround(px * 8) - 45; /* camera in pixels */
    /* drawn once for each of the sky's bands (the ROM changes colour 0 of
     * the palettes but the ground's and the bar's at each band's first
     * line), each band's lines kept */
    uint8_t *tmp = (uint8_t *)calloc((size_t)W * H * 3, 1);
    for (int band = 0; band <= SKY_BANDS; band++) {
        Rgb bb[8][4];
        memcpy(bb, bp, sizeof(bb));
        if (band < SKY_BANDS)
            for (int p = 0; p < 8; p++)
                if (p != PAL_GROUND && p != PAL_HUD && p != PAL_TEXT) bb[p][0] = lc[LC_BAND + band];
        for (int sx = -8; sx < 168; sx += 8) {
            int wx = (int)floor((cam + sx) / 8.0), x = wx * 8 - cam;
            for (int row = 1; row < 18; row++) {
                int tile = T_EMPTY, ly = 15 - row;
                if (row == 16) tile = T_GROUND_TOP + ((wx & 3) == 0);
                else if (row == 17) tile = T_GROUND_LOW + ((wx & 3) == 0);
                else if (wx >= 0 && wx < g.width && ly < GS_ROWS) tile = CELL(&g, wx, ly);
                blit(tmp, W, H, x * S, row * 8 * S, S, &s_bg[tile], bb[s_bg_pal[tile]], 0, 0);
            }
        }
        /* band k: lines 8 + 15k .. 22 + 15k; then the rest (the ground) */
        int y0 = 8 + SKY_BAND_LINES * band, y1 = band < SKY_BANDS ? y0 + SKY_BAND_LINES : 144;
        memcpy(img + (size_t)y0 * S * W * 3, tmp + (size_t)y0 * S * W * 3, (size_t)(y1 - y0) * S * W * 3);
    }
    free(tmp);
    /* progress bar row */
    int pc = (int)(px / g.width * 100);
    blit(img, W, H, 2 * 8 * S, 0, S, &s_ui[UT_BAR_L], bp[PAL_HUD], 0, 0);
    for (int i = 0; i < 12; i++) {
        int fill = clampi(pc * 96 / 100 - i * 8, 0, 8);
        blit(img, W, H, (3 + i) * 8 * S, 0, S, &s_ui[UT_BAR + fill], bp[PAL_HUD], 0, 0);
    }
    blit(img, W, H, 15 * 8 * S, 0, S, &s_ui[UT_BAR_R], bp[PAL_HUD], 0, 0);
    char txt[8];
    snprintf(txt, sizeof(txt), "%3d%%", pc);
    for (int i = 0; txt[i]; i++) blit(img, W, H, (16 + i) * 8 * S, 0, S, &s_ui[UT_FONT + txt[i] - 32], bp[PAL_HUD], 0, 0);
    for (int x = 0; x < 2 * 8 * S; x += 8 * S) blit(img, W, H, x, 0, S, &s_ui[UT_BLANK], bp[PAL_HUD], 0, 0);
    /* the cube on the ground */
    Rgb sp[4] = {{0, 0, 0}, OBJ_PAL[0][1], OBJ_PAL[0][2], OBJ_PAL[0][3]};
    int ox = 45 - 8, oy = 128 - 4 - 8;
    blit(img, W, H, ox * S, oy * S, S, &s_ui[ST_CUBE], sp, 1, 0);
    blit(img, W, H, ox * S, (oy + 8) * S, S, &s_ui[ST_CUBE + 1], sp, 1, 0);
    blit(img, W, H, (ox + 8) * S, oy * S, S, &s_ui[ST_CUBE + 2], sp, 1, 0);
    blit(img, W, H, (ox + 8) * S, (oy + 8) * S, S, &s_ui[ST_CUBE + 3], sp, 1, 0);
    int r = png_write_rgb(out, img, W, H);
    free(img);
    free(g.cells);
    return r;
}

/* ------------------------------------------------------------------ */
/* Scripts for the emulator test                                       */
/* ------------------------------------------------------------------ */

/* Inputs that finish level idx (the solver's, preferring coarse ones) in
 * g_sol; returns the ticks to finish, or -1 (then g_sol: the furthest run). */
static int solution(int idx)
{
    Level *L = level_parse(g_levels[idx].src);
    int t = -1;
    for (int K = 3; K >= 1 && t < 0; K--) t = solve(L, K, 0, NULL);
    if (t < 0) memcpy(g_sol, g_best_sol, sizeof(g_sol));
    level_free(L);
    return t;
}

static int cmd_script(int idx, const char *out)
{
    LevelInfo info;
    level_info(idx, &info);
    int t = solution(idx);
    if (t < 0) die("no solution");
    FILE *f = fopen(out, "w");
    if (!f) die("cannot write the script");
    fprintf(f, "# %s: presses (tick from the attempt start, ticks held); finishes after %d ticks\n", info.name, t);
    for (int i = 0; i < t; i++) {
        if (!g_sol[i] || (i > 0 && g_sol[i - 1])) continue;
        int n = 0;
        while (i + n < t && g_sol[i + n]) n++;
        fprintf(f, "%d %d\n", i, n);
    }
    fclose(f);
    printf("%s: %d ticks\n", out, t);
    return 0;
}

/* Play a script (as written by cmd_script) and print the player each tick:
 * "tick x y vy mode grav dead done" with x, y, vy in their raw units. */
static int cmd_replay(int idx, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    static uint8_t held[MAX_TICKS];
    if (!f) die("cannot read the script");
    memset(held, 0, sizeof(held));
    while (fgets(line, sizeof(line), f)) {
        int t, n;
        if (line[0] != '#' && sscanf(line, "%d %d", &t, &n) == 2)
            for (int k = 0; k < n && t + k < MAX_TICKS; k++) held[t + k] = 1;
    }
    fclose(f);
    Level *L = level_parse(g_levels[idx].src);
    Player p;
    sim_reset(&p, L);
    int prev = 0;
    while (!p.done && !p.dead && p.ticks < MAX_TICKS) {
        int h = held[p.ticks];
        sim_tick(&p, L, h, h && !prev);
        prev = h;
        printf("%d %u %d %d %d %d %d %d\n", p.ticks, (uint32_t)p.x, p.y, p.vy, p.mode, p.grav, p.dead, p.done);
    }
    level_free(L);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The Game Boy's physics against the reference (src/core/sim.c)       */
/* ------------------------------------------------------------------ */

static uint64_t s_rng = 0x9e3779b97f4a7c15ULL;

static uint32_t rnd(uint32_t n)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 7;
    s_rng ^= s_rng << 17;
    return (uint32_t)(s_rng >> 32) % n;
}

/* Does the Game Boy's player say the same as the reference's? If not, why. */
static int same_player(const GsPlayer *a, const Player *b, const Level *L, char *why, size_t cap)
{
#define FIELD(name, va, vb) \
    if ((long long)(va) != (long long)(vb)) { \
        snprintf(why, cap, "%s: Game Boy %lld, reference %lld", name, (long long)(va), (long long)(vb)); \
        return 0; \
    }
    FIELD("x", a->x, (uint32_t)b->x);
    FIELD("x fraction", a->xfrac, b->xf);
    FIELD("y", a->y, b->y);
    FIELD("vy", a->vy, b->vy);
    FIELD("mode", a->mode, b->mode);
    FIELD("gravity", a->grav, b->grav);
    FIELD("speed", a->speed_idx, b->speed_idx);
    FIELD("grounded", a->grounded, b->grounded);
    FIELD("press buffered", a->buf, b->buf);
    FIELD("dead", a->dead, b->dead);
    FIELD("done", a->done, b->done);
    FIELD("coins", a->coins, b->coins);
    FIELD("events", a->events, b->events);
    FIELD("ticks", a->ticks, b->ticks);
    FIELD("jumps", a->jumps, b->jumps);
    if (a->mode != MODE_CUBE) {
        FIELD("corridor floor", a->floor_y, b->floor_y);
        FIELD("corridor ceiling", a->ceil_y, b->ceil_y);
    }
    if (b->events & (EV_ORB | EV_PAD | EV_PORTAL | EV_SPEED | EV_COIN)) {
        const LevelObj *o = &L->objs[b->ev_obj];
        FIELD("event's cell", a->ev_cell, (o->cx << 4) | o->cy);
    }
#undef FIELD
    return 1;
}

/* Play the inputs (held per tick, then released) on both; 0 and why at the
 * first tick they differ. */
static long s_diff_ticks;

static int diff_run(const GLevel *g, const Level *L, const uint8_t *in, int n, char *why, size_t cap)
{
    Player r;
    sim_reset(&r, L);
    use_level(g);
    gs_reset((uint8_t)g->speed);
    int prev = 0;
    while (!r.dead && !r.done && r.ticks < MAX_TICKS) {
        int t = r.ticks, h = t < n ? in[t] : 0;
        sim_tick(&r, L, h, h && !prev);
        gs_step((uint8_t)h, (uint8_t)(h && !prev));
        prev = h;
        s_diff_ticks++;
        if (!same_player(&gs_p, &r, L, why, cap)) {
            size_t k = strlen(why);
            snprintf(why + k, cap - k, " (tick %d, x=%.3f y=%.3f mode %d)", t, r.x / 65536.0, r.y / 65536.0, r.mode);
            return 0;
        }
    }
    return 1;
}

/* The solver's run of each level, and `runs` runs that leave it at a random
 * tick (one press more or less, a stretch of random presses, or random
 * presses from there on) and so go off on their own until they die. */
static int cmd_difftest(int first, int last, int runs)
{
    static uint8_t base[MAX_TICKS], in[MAX_TICKS];
    char why[256];
    int fails = 0;
    for (int idx = first; idx <= last; idx++) {
        GLevel g;
        if (build_level(idx, &g) < 0) {
            printf("level %d: not on the Game Boy (%s)\n", idx, s_unfit);
            continue;
        }
        Level *L = level_parse(g_levels[idx].src);
        int t_end = solution(idx);
        if (t_end < 0) t_end = MAX_TICKS;
        memcpy(base, g_sol, sizeof(base));
        long before = s_diff_ticks;
        int ok = diff_run(&g, L, base, t_end, why, sizeof(why));
        for (int k = 0; ok && k < runs; k++) {
            memcpy(in, base, sizeof(in));
            int t0 = (int)rnd((uint32_t)t_end), kind = (int)rnd(3);
            if (kind == 0) {
                in[t0] = (uint8_t)!in[t0];
            } else {
                int t1 = kind == 1 ? t0 + 10 + (int)rnd(80) : MAX_TICKS;
                for (int t = t0; t < t1 && t < MAX_TICKS;) {
                    int len = 1 + (int)rnd(rnd(4) ? 12 : 40), h = (int)rnd(2);
                    for (int j = 0; j < len && t < t1 && t < MAX_TICKS; j++) in[t++] = (uint8_t)h;
                }
            }
            ok = diff_run(&g, L, in, MAX_TICKS, why, sizeof(why));
        }
        printf("level %d \"%s\": %s (%ld ticks compared)\n", idx, g.name, ok ? "the same" : "DIFFERENT",
               s_diff_ticks - before);
        if (!ok) printf("  %s\n", why);
        fails += !ok;
        level_free(L);
        free(g.cells);
    }
    printf("%s\n", fails ? "THE GAME BOY'S PHYSICS DIFFERS FROM THE REFERENCE"
                         : "the Game Boy's physics gives the same results as the reference");
    return fails ? 1 : 0;
}

/* ------------------------------------------------------------------ */

static int level_arg(const char *s)
{
    int i = atoi(s);
    if (i < 0 || i >= g_level_count) {
        fprintf(stderr, "bad level %s\n", s);
        exit(2);
    }
    return i;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: gbc_tool export|levels|difftest|script|replay|sheet|view|music ...\n");
        return 2;
    }
    font_init();
    audio_init();
    const char *cmd = argv[1];
    if (!strcmp(cmd, "export") && argc >= 3) return cmd_export(argv[2], argc > 3 ? argv[3] : "dev");
    if (!strcmp(cmd, "music")) return cmd_music();
    if (!strcmp(cmd, "sheet") && argc >= 3) return cmd_sheet(argv[2], argc > 3 ? atoi(argv[3]) : 0);
    if (!strcmp(cmd, "view") && argc >= 5) return cmd_view(level_arg(argv[2]), atof(argv[3]), argv[4]);
    if (!strcmp(cmd, "script") && argc >= 4) return cmd_script(level_arg(argv[2]), argv[3]);
    if (!strcmp(cmd, "replay") && argc >= 4) return cmd_replay(level_arg(argv[2]), argv[3]);
    if (!strcmp(cmd, "levels")) {
        gb_levels(1);
        for (int i = 0; i < s_gb_count; i++) printf("%d %d\n", i, s_gb_level[i]);
        return 0;
    }
    if (!strcmp(cmd, "difftest")) {
        int first = 0, last = g_level_count - 1;
        if (argc > 2 && strcmp(argv[2], "all")) first = last = level_arg(argv[2]);
        return cmd_difftest(first, last, argc > 3 ? atoi(argv[3]) : 2000);
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
