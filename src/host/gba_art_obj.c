/*
 * The Game Boy Advance's sprites (gba_tool export), drawn by the core's own
 * renderer (render.c, icons.c, draw.c) at the GBA's scale, 12 pixels to a
 * block: draw.h's pixel grid set to 12/34 GBA pixels a virtual pixel puts
 * their edges and strokes on whole GBA pixels, as it does for the PSP's
 * screen (but the player's vehicles, whose thin lines and bands of colour
 * that rounding loses, are fitted to the pixels another way:
 * vehicle_design). Each picture is drawn into a transparent canvas through
 * the PC build's software renderer (gfx_sdl.c) and turned into 16-colour
 * tiles.
 *
 * Pictures that differ only in colour (the four orbs, seven portals) share
 * their tiles: they are drawn once in each colour, and the pixels are
 * grouped by their colours in all of them at once, so each colour gets its
 * own palette for the same tiles. The player's vehicles are drawn in
 * stand-in colours and sorted into the parts the garage colours (outline,
 * colour 1, its highlight, colour 2, the UFO's dome), which the ROM fills
 * with the chosen colours.
 */
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_gen.h"
#include "gfx_sdl.h"
#include "png_write.h"
#include "../core/draw.h"
#include "../core/font.h"
#include "../core/icons.h"
#include "../core/render.h"
#include "../core/theme.h"
#include "../core/game_internal.h"
#include "../gba/art.h"

/* GBA pixels a virtual (PC) pixel: a block is 34 virtual pixels, 12 GBA ones */
#define K (12.0f / 34.0f)

/* ------------------------------------------------------------------ */
/* Canvases                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int w, h;
    uint32_t *px; /* ARGB */
} Img;

typedef void (*DrawFn)(const void *arg);

/* The pictures are drawn SS times as fine as the GBA shows them, then
 * brought down (render: averaged; render_crisp: by majority; the player's
 * vehicles, VS times as fine and fitted: vehicle_design): the renderer
 * doesn't smooth edges, and drawn at the GBA's size a thin outline or a
 * round edge falls on a pixel or misses it by where its centre is, broken
 * and lumpy. (What has parts thinner than a pixel is drawn pixel by pixel
 * instead: pix_*.) */
#define SS 4

/* Draw fn into a (w * ss) x (h * ss) canvas cleared to `clear`, at ss times
 * the GBA's scale, with virtual (0, 0) at the canvas's centre, on a pixel
 * grid of `grid` canvas pixels a virtual one (draw_set_pixel_grid: K puts
 * lines on whole GBA pixels). */
static Img render_fine(int w, int h, int ss, DrawFn fn, const void *arg, uint32_t clear, float grid)
{
    static int inited;
    if (!inited) {
        draw_init();
        font_init();
        inited = 1;
    }
    int fw = w * ss, fh = h * ss;
    Img im = {fw, fh, malloc((size_t)fw * fh * 4)};
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, fw, fh, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(s);
    SDL_FillRect(s, NULL, clear);
    gfx_sdl_begin(r, K * ss, K * ss);
    gfx_sdl_offset(w * 0.5f / K, h * 0.5f / K);
    draw_set_pixel_grid(grid);
    fn(arg);
    gfx_sdl_flush();
    SDL_RenderPresent(r);
    for (int y = 0; y < fh; y++) memcpy(im.px + y * fw, (uint8_t *)s->pixels + y * s->pitch, (size_t)fw * 4);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(s);
    return im;
}

static int alpha_of(uint32_t c) { return (int)(c >> 24); }

/* Draw fn into a w x h canvas (render_fine, each pixel the average of its
 * SS x SS: its alpha the part covered, its colour that of what covers it). */
static Img render(int w, int h, DrawFn fn, const void *arg, uint32_t clear)
{
    Img f = render_fine(w, h, SS, fn, arg, clear, K), im = {w, h, malloc((size_t)w * h * 4)};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            /* (over a transparent canvas the renderer leaves a see-through
             * pixel's colour times its alpha: summed as it is, divided by
             * the alpha's sum) */
            unsigned sa = 0, sr = 0, sg = 0, sb = 0;
            for (int j = 0; j < SS; j++)
                for (int i = 0; i < SS; i++) {
                    uint32_t c = f.px[(y * SS + j) * f.w + x * SS + i];
                    unsigned a = c >> 24;
                    sa += a;
                    sr += (c >> 16 & 255) * (clear ? 255 : a) / 255;
                    sg += (c >> 8 & 255) * (clear ? 255 : a) / 255;
                    sb += (c & 255) * (clear ? 255 : a) / 255;
                }
            unsigned a = (sa + SS * SS / 2) / (SS * SS), r = 0, g = 0, b = 0;
            if (clear) {
                r = sr / (SS * SS);
                g = sg / (SS * SS);
                b = sb / (SS * SS);
            } else if (sa) {
                r = sr * 255 / sa;
                g = sg * 255 / sa;
                b = sb * 255 / sa;
            }
            im.px[y * w + x] = a << 24 | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
        }
    free(f.px);
    return im;
}
static int r_of(uint32_t c) { return (int)((c >> 16) & 255); }
static int g_of(uint32_t c) { return (int)((c >> 8) & 255); }
static int b_of(uint32_t c) { return (int)(c & 255); }

static uint16_t to15(int r, int g, int b)
{
    r = (r + 4) >> 3;
    g = (g + 4) >> 3;
    b = (b + 4) >> 3;
    return (uint16_t)((r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10);
}

/* ------------------------------------------------------------------ */
/* Colours shared across variants                                      */
/* ------------------------------------------------------------------ */

/*
 * A group of pictures quantized together: npic pictures (any sizes), each
 * drawn in nvar colourings. Pixels with alpha >= 128 in variant 0 are kept;
 * they are put into at most ncol clusters by their colours in all
 * variants, giving an index picture per picture (1..ncol, 0 transparent)
 * and a palette per variant. `fixed` colours (variant-independent, e.g.
 * white and black) can be given indices of their own first.
 */
#define MAXV 8
typedef struct {
    int npic, nvar, ncol;
    Img pic[16][MAXV];
    uint8_t *idx[16];
    int pal[MAXV][16][3];
} Group;

static double dist2(const double *a, const double *b, int n)
{
    double d = 0;
    for (int i = 0; i < n; i++) d += (a[i] - b[i]) * (a[i] - b[i]);
    return d;
}

static void group_quantize(Group *g)
{
    int dim = g->nvar * 3, n = 0, cap = 0;
    double *pts = NULL;
    int *owner = NULL;
    for (int p = 0; p < g->npic; p++) {
        Img *im = &g->pic[p][0];
        g->idx[p] = calloc((size_t)im->w * im->h, 1);
        for (int i = 0; i < im->w * im->h; i++) {
            if (alpha_of(im->px[i]) < 128) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 1024;
                pts = realloc(pts, sizeof(double) * cap * dim);
                owner = realloc(owner, sizeof(int) * cap * 2);
            }
            for (int v = 0; v < g->nvar; v++) {
                uint32_t c = g->pic[p][v].px[i];
                pts[n * dim + v * 3 + 0] = r_of(c);
                pts[n * dim + v * 3 + 1] = g_of(c);
                pts[n * dim + v * 3 + 2] = b_of(c);
            }
            owner[n * 2] = p;
            owner[n * 2 + 1] = i;
            n++;
        }
    }
    /* distinct colours first: if there are few, each gets an index */
    double cent[15][MAXV * 3];
    int k = 0;
    for (int i = 0; i < n && k <= g->ncol; i++) {
        int found = 0;
        for (int c = 0; c < k; c++)
            if (dist2(&pts[i * dim], cent[c], dim) < 0.5) found = 1;
        if (!found) {
            if (k < g->ncol) memcpy(cent[k], &pts[i * dim], sizeof(double) * dim);
            k++;
        }
    }
    if (k > g->ncol) {
        /* k-means, seeded with the farthest-first points */
        k = g->ncol;
        memcpy(cent[0], &pts[0], sizeof(double) * dim);
        for (int c = 1; c < k; c++) {
            double best = -1;
            int bi = 0;
            for (int i = 0; i < n; i++) {
                double m = 1e30;
                for (int j = 0; j < c; j++) {
                    double d = dist2(&pts[i * dim], cent[j], dim);
                    if (d < m) m = d;
                }
                if (m > best) {
                    best = m;
                    bi = i;
                }
            }
            memcpy(cent[c], &pts[bi * dim], sizeof(double) * dim);
        }
        for (int it = 0; it < 30; it++) {
            double sum[15][MAXV * 3];
            int cnt[15];
            memset(sum, 0, sizeof(sum));
            memset(cnt, 0, sizeof(cnt));
            for (int i = 0; i < n; i++) {
                int bc = 0;
                double bd = 1e30;
                for (int c = 0; c < k; c++) {
                    double d = dist2(&pts[i * dim], cent[c], dim);
                    if (d < bd) {
                        bd = d;
                        bc = c;
                    }
                }
                cnt[bc]++;
                for (int j = 0; j < dim; j++) sum[bc][j] += pts[i * dim + j];
            }
            for (int c = 0; c < k; c++)
                if (cnt[c])
                    for (int j = 0; j < dim; j++) cent[c][j] = sum[c][j] / cnt[c];
        }
    }
    for (int i = 0; i < n; i++) {
        int bc = 0;
        double bd = 1e30;
        for (int c = 0; c < k; c++) {
            double d = dist2(&pts[i * dim], cent[c], dim);
            if (d < bd) {
                bd = d;
                bc = c;
            }
        }
        g->idx[owner[i * 2]][owner[i * 2 + 1]] = (uint8_t)(bc + 1);
    }
    memset(g->pal, 0, sizeof(g->pal));
    for (int v = 0; v < g->nvar; v++)
        for (int c = 0; c < k; c++)
            for (int j = 0; j < 3; j++) g->pal[v][c + 1][j] = (int)lround(cent[c][v * 3 + j]);
    g->ncol = k;
    free(pts);
    free(owner);
}

/* ------------------------------------------------------------------ */
/* Output                                                              */
/* ------------------------------------------------------------------ */

static uint32_t *s_tiles;
static int s_ntiles, s_tcap;
static uint16_t s_pals[16][16];

/* what was added, for the sheet: sprites (pieces of tiles) and the banks to show them in */
typedef struct {
    int tile, w, h, bank0, nbank;
    const uint16_t (*pals)[16]; /* the set's palettes, or NULL: the static ones */
} SheetSpr;
static SheetSpr s_sheet[256];
static int s_nsheet;
static int s_cur_bank0 = 0, s_cur_nbank = 1;
/* tiles go to OBJ VRAM from s_base; their names are prefixed s_prefix */
static int s_base = OBJ_STATIC_TILE;
static const char *s_prefix = "OT_";

/* Add a w x h index picture as sprite tiles (1D layout: rows of tiles) and
 * #define its first tile. */
static int add_tiles(GbaGen *g, const char *name, const uint8_t *idx, int w, int h, int x0, int y0, int sw, int sh)
{
    int first = s_ntiles;
    for (int ty = 0; ty < sh / 8; ty++)
        for (int tx = 0; tx < sw / 8; tx++) {
            if (s_ntiles == s_tcap) {
                s_tcap = s_tcap ? s_tcap * 2 : 256;
                s_tiles = realloc(s_tiles, (size_t)s_tcap * 32);
            }
            uint32_t *t = s_tiles + s_ntiles * 8;
            for (int y = 0; y < 8; y++) {
                uint32_t wd = 0;
                for (int x = 0; x < 8; x++) {
                    int px = x0 + tx * 8 + x, py = y0 + ty * 8 + y;
                    int v = px >= 0 && px < w && py >= 0 && py < h ? idx[py * w + px] : 0;
                    wd |= (uint32_t)(v & 15) << (x * 4);
                }
                t[y] = wd;
            }
            s_ntiles++;
        }
    if (name) fprintf(g->hdr, "#define %s%s %d\n", s_prefix, name, s_base + first);
    if (s_nsheet < 256) s_sheet[s_nsheet++] = (SheetSpr){first, sw, sh, s_cur_bank0, s_cur_nbank, NULL};
    return first;
}

static void set_pal(GbaGen *g, int bank, const char *name, int pal[16][3], int n)
{
    for (int i = 1; i < 16; i++) s_pals[bank][i] = i <= n ? to15(pal[i][0], pal[i][1], pal[i][2]) : 0;
    if (name) fprintf(g->hdr, "#define OP_%s %d\n", name, bank);
}

/* Glows. The PC draws a soft light around the player, the orbs, pads,
 * portals and coins (draw_glow: a colour fading out to nothing at the
 * edge, added to what is behind); the GBA adds it in hardware, with a
 * sprite blended additively behind everything (sprites.c). Its rings are
 * colours GLOW_FIRST..15 of the object's palette bank: the object's colour
 * at 1/10, 3/10 .. 9/10 of the glow's strength, from the edge in. So the
 * objects that glow are drawn in GLOW_FIRST - 1 colours. */
#define GLOW_FIRST 11
#define GLOW_LEVELS (16 - GLOW_FIRST)

static void glow_ramp(int bank, Color c, float alpha)
{
    for (int k = 0; k < GLOW_LEVELS; k++) {
        float a = alpha * (float)(2 * k + 1) / (2.0f * GLOW_LEVELS);
        s_pals[bank][GLOW_FIRST + k] =
            to15((int)(COL_R(c) * a + 0.5f), (int)(COL_G(c) * a + 0.5f), (int)(COL_B(c) * a + 0.5f));
    }
}

/* the glows' pictures: a disc (radius 16) whose rings get brighter
 * inwards, and for the pads a column (8 wide) brightest at its foot, gone
 * 11 pixels up (render_pad's glow, 0.9 blocks) */
static void emit_glows(GbaGen *g)
{
    uint8_t disc[32 * 32], col[16 * 16];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            double d = hypot(x + 0.5 - 16.0, y + 0.5 - 16.0);
            int level = d < 16.0 ? (int)ceil(GLOW_LEVELS * (1.0 - d / 16.0)) : 0;
            disc[y * 32 + x] = (uint8_t)(level ? GLOW_FIRST - 1 + level : 0);
        }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            double d = 15.5 - y, h = 0.9 * BLOCK_PIX; /* from the foot; the glow's height */
            int level = x >= 4 && x < 12 && d < h ? (int)ceil(GLOW_LEVELS * (1.0 - d / h)) : 0;
            col[y * 16 + x] = (uint8_t)(level ? GLOW_FIRST - 1 + level : 0);
        }
    s_cur_bank0 = OBJ_PAL_ORB;
    s_cur_nbank = 1;
    add_tiles(g, "GLOW", disc, 32, 32, 0, 0, 32, 32);
    add_tiles(g, "GLOW_PAD", col, 16, 16, 0, 0, 16, 16);
    {
        /* render_level's glow round a saw (1.5 blocks out, a small one's
         * 0.85; the saws 0.98 and 0.5 blocks): behind its teeth, as far as
         * it shows (as the blocks' glow: 0.28 of its strongest), in one
         * colour (MISC_GLOW, set every frame) */
        static const double saw_r[2] = {0.98, 0.5}, glow_r[2] = {1.5, 0.85};
        static const int size[2] = {32, 16};
        static const char *names[2] = {"SAW_GLOW_BIG", "SAW_GLOW_SMALL"};
        uint8_t ring[32 * 32];
        s_cur_bank0 = OBJ_PAL_MISC;
        for (int k = 0; k < 2; k++) {
            int n = size[k];
            double r0 = 0.8 * saw_r[k] * BLOCK_PIX, r1 = (1.0 - 0.28) * glow_r[k] * BLOCK_PIX;
            for (int y = 0; y < n; y++)
                for (int x = 0; x < n; x++) {
                    double d = hypot(x + 0.5 - n / 2.0, y + 0.5 - n / 2.0);
                    ring[y * n + x] = (uint8_t)(d >= r0 && d < r1 ? 14 : 0);
                }
            add_tiles(g, names[k], ring, n, n, 0, 0, n, n);
        }
    }
    fprintf(g->hdr, "#define GLOW_FIRST %d\n#define GLOW_LEVELS %d\n", GLOW_FIRST, GLOW_LEVELS);
}

/* ------------------------------------------------------------------ */
/* The level's objects, drawn by render_level                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int type, flags;
    float time;
    int no_glow;
} ObjArg;

/* One object alone in a level, its cell's centre at virtual (0, 0). */
static void draw_obj(const void *a)
{
    const ObjArg *o = a;
    static uint8_t grid[3 * 5], edges[3 * 5];
    static int col_start[4];
    LevelObj obj = {(uint8_t)o->type, (uint8_t)o->flags, 0, 1, 2};
    Level L;
    memset(&L, 0, sizeof(L));
    L.width = 3;
    L.height = 5;
    L.grid = grid;
    L.edges = edges;
    L.objs = &obj;
    L.nobjs = 1;
    L.col_start = col_start;
    col_start[0] = 0;
    col_start[1] = 0;
    col_start[2] = col_start[3] = 1;
    L.end_x = 1000.0f;
    Palette pal = g_palettes[0];
    View v;
    /* view_sx(1.5) = 0 and view_sy(2.5) = 0 */
    v.cam_x = 1.5f;
    v.cam_y = 2.5f - SCREEN_H / BLOCK_PX;
    v.time = o->time;
    v.pulse = 0.0f;
    v.pal = &pal;
    render_level(&v, &L, NULL, 0);
}

static void group_add(Group *g, int w, int h, DrawFn fn, const void *const *args)
{
    for (int v = 0; v < g->nvar; v++) g->pic[g->npic][v] = render(w, h, fn, args[v], 0);
    g->npic++;
}

static void group_free(Group *g)
{
    for (int p = 0; p < g->npic; p++) {
        for (int v = 0; v < g->nvar; v++) free(g->pic[p][v].px);
        free(g->idx[p]);
    }
}

/* Draw fn into a w x h canvas, each pixel the colour most of its 8 x 8
 * points are (points within a small distance of each other counted as
 * one colour, so a gradient is one: the pixel the point most are near),
 * or clear if most are (less opaque than min_alpha): no in-between
 * colours at the edges, for pictures whose parts are each a pixel or more
 * across (render's averages make a smear of them there). */
static Img render_crisp(int w, int h, DrawFn fn, const void *arg, int min_alpha)
{
    enum { N = 8 };
    Img f = render_fine(w, h, N, fn, arg, 0, K), im = {w, h, calloc((size_t)w * h, 4)};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c[N * N];
            int n = 0, clear = 0, best = -1, bn = 0;
            for (int j = 0; j < N; j++)
                for (int i = 0; i < N; i++) {
                    /* (the renderer leaves a see-through colour times its
                     * alpha: undone) */
                    uint32_t p = f.px[(y * N + j) * f.w + x * N + i];
                    unsigned a = (unsigned)alpha_of(p);
                    if (a < (unsigned)min_alpha) clear++;
                    else c[n++] = (uint32_t)(r_of(p) * 255 / a) << 16 | (uint32_t)(g_of(p) * 255 / a) << 8 | (uint32_t)(b_of(p) * 255 / a);
                }
            if (clear * 2 >= N * N) continue;
            for (int a = 0; a < n; a++) {
                int k = 0;
                for (int b = 0; b < n; b++)
                    k += abs(r_of(c[a]) - r_of(c[b])) + abs(g_of(c[a]) - g_of(c[b])) + abs(b_of(c[a]) - b_of(c[b])) < 48;
                if (k > bn) {
                    bn = k;
                    best = a;
                }
            }
            im.px[y * w + x] = 0xFF000000u | c[best];
        }
    free(f.px);
    return im;
}

/* ------------------------------------------------------------------ */
/* Pictures drawn pixel by pixel                                       */
/* ------------------------------------------------------------------ */

/*
 * Rings and outlines thinner than a GBA pixel (the PC's 2-pixel lines are
 * 0.7 of one here) don't come through being drawn and brought down: as
 * averages they are smears of in-between colours, by majority broken
 * lines. The objects that have them are drawn here pixel by pixel, in the
 * renderer's shapes and proportions: a shape fills the pixels it covers
 * the most of; an outline is the shape's own edge pixels; a ring is the
 * pixels whose middles are within half a pixel of its radius (an unbroken
 * line a pixel wide). Positions are in the picture's pixels from its top
 * left corner; the colours are the renderer's, laid over each other as it
 * lays them (a see-through one mixed with what is under it).
 */
typedef int (*InsideFn)(float x, float y, const void *arg);

static Img pix_new(int w, int h)
{
    Img im = {w, h, calloc((size_t)w * h, 4)};
    return im;
}

/* c (its alpha too) laid over pixel (x, y) */
static void pix_put(Img *im, int x, int y, Color c)
{
    if (x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    uint32_t *d = &im->px[y * im->w + x];
    int a = (int)COL_A(c), da = (int)(*d >> 24), oa = a + da * (255 - a) / 255;
    if (!oa) return;
    int r = ((int)COL_R(c) * a + r_of(*d) * da * (255 - a) / 255) / oa;
    int g = ((int)COL_G(c) * a + g_of(*d) * da * (255 - a) / 255) / oa;
    int b = ((int)COL_B(c) * a + b_of(*d) * da * (255 - a) / 255) / oa;
    *d = (uint32_t)oa << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* the pixels a shape covers most of (8 x 8 points of each) */
static uint8_t *pix_mask(int w, int h, InsideFn in, const void *arg)
{
    uint8_t *m = calloc((size_t)w * h, 1);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int n = 0;
            for (int j = 0; j < 8; j++)
                for (int i = 0; i < 8; i++) n += in(x + (i + 0.5f) / 8, y + (j + 0.5f) / 8, arg) != 0;
            m[y * w + x] = n > 32;
        }
    return m;
}

/* a pixel of the mask beside (not corner to corner) one outside it */
static int pix_edge(const uint8_t *m, int w, int h, int x, int y)
{
    if (!m[y * w + x]) return 0;
    return x == 0 || y == 0 || x == w - 1 || y == h - 1 || !m[y * w + x - 1] || !m[y * w + x + 1] ||
           !m[(y - 1) * w + x] || !m[(y + 1) * w + x];
}

/* how far pixel (x, y)'s middle is from (cx, cy) */
static float pix_dist(int x, int y, float cx, float cy)
{
    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
    return sqrtf(dx * dx + dy * dy);
}

/* a ring a pixel wide, r out from (cx, cy) */
static void pix_ring(Img *im, float cx, float cy, float r, Color c)
{
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++)
            if (fabsf(pix_dist(x, y, cx, cy) - r) <= 0.5f) pix_put(im, x, y, c);
}

/* a polygon (up to 32 corners) */
typedef struct {
    int n;
    float xy[64];
} Poly;

static int in_poly(float x, float y, const void *a)
{
    const Poly *p = a;
    int in = 0;
    for (int i = 0, j = p->n - 1; i < p->n; j = i++) {
        float xi = p->xy[i * 2], yi = p->xy[i * 2 + 1], xj = p->xy[j * 2], yj = p->xy[j * 2 + 1];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}

/*
 * render_saw at size x size, its middle the picture's (r the PC's radius):
 * the teeth filled with the fill's glow from the middle (its 2.5 times
 * there) and edged, a ring `ring` pixels out and the middle's dot, `dot`
 * pixels round, the three in the edge's colour. (The PC's ring is at half
 * the radius, its dot 0.18 of it: a small saw is 12 pixels across here,
 * and those leave little between them and the teeth.)
 */
static Img pix_saw(int size, float r, float angle, Color fill, Color edge, float ring, float dot)
{
    Img im = pix_new(size, size);
    float c = size * 0.5f, R = r * K;
    Poly p = {24, {0}};
    for (int i = 0; i < 24; i++) {
        float a = angle + i / 24.0f * 2.0f * PI, rr = (i & 1) ? R * 0.80f : R;
        p.xy[i * 2] = c + cosf(a) * rr;
        p.xy[i * 2 + 1] = c + sinf(a) * rr;
    }
    uint8_t *m = pix_mask(size, size, in_poly, &p);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            if (!m[y * size + x]) continue;
            float t = pix_dist(x, y, c, c) / (R * 0.9f);
            pix_put(&im, x, y, pix_edge(m, size, size, x, y) ? edge : col_lerp(col_scale(fill, 2.5f), fill, t > 1 ? 1 : t));
        }
    free(m);
    pix_ring(&im, c, c, ring, edge);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            if (pix_dist(x, y, c, c) <= dot) pix_put(&im, x, y, edge);
    return im;
}

/* an index picture (0..15) as 4-bit tiles, row by row (a sprite's, 1D) */
static void idx_tiles(uint32_t *out, const uint8_t *idx, int w, int h)
{
    for (int ty = 0; ty < h / 8; ty++)
        for (int tx = 0; tx < w / 8; tx++)
            for (int y = 0; y < 8; y++) {
                uint32_t v = 0;
                for (int x = 0; x < 8; x++) v |= (uint32_t)(idx[(ty * 8 + y) * w + tx * 8 + x] & 15) << (x * 4);
                out[(ty * (w / 8) + tx) * 8 + y] = v;
            }
}

/*
 * render_orb at 16 x 16 (0.3 of a block round, as render_level draws it),
 * its dashes turned by angle: the disc in its glow from white, the dark
 * between it and the ring, the ring, then the four dashes, each a ring a
 * pixel wide. (The dark is the PC's see-through black: here a dark shade
 * of the colour, as it shows over the orb's glow.)
 */
static Img pix_orb(Color c, float angle)
{
    Img im = pix_new(16, 16);
    /* (the dashes a pixel clear of the ring: at the PC's 1.625 of the
     * radius they would touch it) */
    float s = BLOCK_PX * 0.30f * K, gap = 0.97f * s, dash = gap + 3.0f;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float d = pix_dist(x, y, 8.0f, 8.0f);
            if (d < gap - 0.5f) pix_put(&im, x, y, col_lerp(col_lerp(c, COL_WHITE, 0.75f), c, d / (0.92f * s)));
            else if (d <= gap + 0.5f) pix_put(&im, x, y, col_lerp(RGB(0, 0, 0), c, 0.2f));
            else if (d <= gap + 1.5f) pix_put(&im, x, y, c);
            else if (fabsf(d - dash) <= 0.5f) {
                /* the dashes: 0.75 radians from each quarter turn */
                float a = atan2f(y + 0.5f - 8.0f, x + 0.5f - 8.0f) - angle;
                a = fmodf(a + 8.0f * PI, PI * 0.5f);
                if (a <= 0.75f) pix_put(&im, x, y, col_with_alpha(c, 0.8f));
            }
        }
    return im;
}

/* render_coin at 16 x 16 (0.42 of a block round, face on): its dark edge
 * (the edge pixels of the PC's, 2 of its pixels wider), the coin, its
 * darker middle and the bar; the ghost (a coin already saved) in its
 * see-through white over the dark */
static Img pix_coin(int ghost)
{
    Img im = pix_new(16, 16);
    float r = BLOCK_PX * 0.42f * K;
    Color c = ghost ? RGBA(230, 240, 255, 115) : COL_COIN;
    uint8_t m[16 * 16];
    for (int i = 0; i < 16 * 16; i++) m[i] = pix_dist(i % 16, i / 16, 8.0f, 8.0f) <= r + 2.0f * K;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            if (!m[y * 16 + x]) continue;
            float d = pix_dist(x, y, 8.0f, 8.0f), dx = fabsf(x + 0.5f - 8.0f), dy = fabsf(y + 0.5f - 8.0f);
            pix_put(&im, x, y, RGBA(40, 20, 0, 204));
            if (pix_edge(m, 16, 16, x, y)) continue;
            pix_put(&im, x, y, c);
            if (d <= r * 0.68f) pix_put(&im, x, y, col_scale(c, 0.78f));
            if (dx <= 2.0f * K && dy <= r * 0.35f) pix_put(&im, x, y, col_scale(c, 1.2f));
        }
    return im;
}

/*
 * render_speed_portal's chevrons (idx + 1 of them) at 16 x 16, its height
 * and width, a pixel to the side for each two down: drawn as the PC draws
 * them, 6 of its pixels wide and 3.12 pixels apart, they run together
 * here, so each is 2 pixels wide with a pixel of its dark edge on its
 * right, 3 pixels apart.
 */
static Img pix_speed(int idx)
{
    static const Color cols[4] = {RGB(255, 170, 40), RGB(60, 200, 255), RGB(80, 255, 120), RGB(255, 90, 220)};
    enum { H = 7 }; /* rows from the middle to a tip: render_speed_portal's 0.62 of a block */
    Img im = pix_new(16, 16);
    int n = idx + 1, w = (n - 1) * 3 + 2 + (H - 1) / 2 + 1, x0 = (16 - w) / 2;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 2 * H; k++) {
            int x = x0 + i * 3 + (k < H ? k : 2 * H - 1 - k) / 2, y = 8 - H + k;
            pix_put(&im, x, y, cols[idx]);
            pix_put(&im, x + 1, y, cols[idx]);
            pix_put(&im, x + 2, y, RGBA(0, 0, 0, 170));
        }
    return im;
}

/* the orbs (their dashes turned through a quarter turn, ORB_FRAMES) and
 * pads: one set of tiles, a palette per colour */
static void emit_orbs(GbaGen *g)
{
    static const int pads[4] = {OBJ_PAD_YELLOW, OBJ_PAD_PINK, OBJ_PAD_BLUE, OBJ_PAD_YELLOW};
    static const Color glow[4] = {COL_ORB_YELLOW, COL_ORB_PINK, COL_ORB_BLUE, COL_ORB_GREEN};
    Group gr = {.npic = 0, .nvar = 4, .ncol = GLOW_FIRST - 1};
    ObjArg pa[4];
    const void *ap[4];
    for (int f = 0; f < ORB_FRAMES; f++)
        for (int v = 0; v < 4; v++) gr.pic[f][v] = pix_orb(glow[v], f * (PI * 0.5f) / ORB_FRAMES);
    gr.npic = ORB_FRAMES;
    for (int i = 0; i < 4; i++) {
        pa[i] = (ObjArg){pads[i], 0, 0.0f, 0};
        ap[i] = &pa[i];
    }
    group_add(&gr, 16, 16, draw_obj, ap);
    group_quantize(&gr);
    add_tiles(g, "ORB", gr.idx[0], 16, 16, 0, 0, 16, 16);
    {
        static uint32_t frames[ORB_FRAMES * 4][8];
        for (int f = 0; f < ORB_FRAMES; f++) idx_tiles(frames[f * 4], gr.idx[f], 16, 16);
        gba_gen_blob(g, "g_orb_frames", "uint32_t", frames, sizeof(frames));
    }
    /* the pad sits on the cell's bottom: rows 8..15 of the picture's 16 */
    add_tiles(g, "PAD", gr.idx[ORB_FRAMES], 16, 16, 0, 8, 16, 8);
    static const char *names[4] = {"ORB_YELLOW", "ORB_PINK", "ORB_BLUE", "ORB_GREEN"};
    for (int v = 0; v < 4; v++) {
        set_pal(g, OBJ_PAL_ORB + v, names[v], gr.pal[v], gr.ncol);
        glow_ramp(OBJ_PAL_ORB + v, glow[v], 0.45f); /* render_orb's and render_pad's */
    }
    group_free(&gr);
}

/* The portals: the ring (8 frames of its travelling highlight: pix_portal)
 * and the symbols inside, one set of tiles, a palette per portal colour. */
#define PORTAL_FRAMES 8
typedef struct {
    Color c;
    int icon, arrow;
} PortalArg;

/* render_portal's vehicle symbol alone, in the portal's colour and
 * white, solid (the PC's are 0.85 see-through, which over their own black
 * makes a shade of each for the palette) */
static void draw_portal_symbol(const void *a)
{
    const PortalArg *p = a;
    icon_draw_mode(p->icon, 0.0f, 0.0f, BLOCK_PX * 0.55f, 0.0f, 0, 0, p->c, COL_WHITE);
}

/* render_portal's gravity arrow (up for dir > 0) at 16 x 48: its head 0.6
 * of a block long and 0.44 across, a pixel out each side for every two
 * down, and its shaft, 2 pixels wide */
static Img pix_portal_arrow(int dir)
{
    static const uint8_t half[10] = {1, 1, 2, 2, 3, 3, 1, 1, 1, 1}; /* from the tip */
    Img im = pix_new(16, 48);
    for (int k = 0; k < 10; k++)
        for (int x = 8 - half[k]; x < 8 + half[k]; x++) pix_put(&im, x, dir > 0 ? 24 - 5 + k : 24 + 4 - k, COL_WHITE);
    return im;
}

/* how far (x, y) is from the ellipse of half-axes a and b, out (+) or in
 * (-): from the nearest of 1024 points round it */
static float ellipse_dist(float x, float y, float a, float b)
{
    float best = 1e9f;
    for (int i = 0; i < 1024; i++) {
        float t = i * (2.0f * PI / 1024.0f), dx = x - a * cosf(t), dy = y - b * sinf(t), d = dx * dx + dy * dy;
        if (d < best) best = d;
    }
    return x * x / (a * a) + y * y / (b * b) < 1.0f ? -sqrtf(best) : sqrtf(best);
}

/*
 * render_portal's rings at 16 x 48, the highlight from angle a: the
 * colour's ring (6 of the PC's pixels, here 2), the see-through black
 * round it (here a pixel each side) and the light ring inside (here a
 * pixel), by how far each pixel's middle is from the middle of the
 * colour's ring; the inside is left clear, as on the PC.
 */
static Img pix_portal(Color c, float a)
{
    const float rx = 0.40f * BLOCK_PX * K, ry = 1.45f * BLOCK_PX * K, t = 6.0f * K;
    Img im = pix_new(16, 48);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 16; x++) {
            float dx = x + 0.5f - 8.0f, dy = y + 0.5f - 24.0f, d = ellipse_dist(dx, dy, rx - t / 2, ry - t / 2);
            if (d > 2.0f || d < -3.0f) continue;
            if (d < -2.0f) pix_put(&im, x, y, col_lerp(c, COL_WHITE, 0.6f));
            else if (fabsf(d) > 1.0f) pix_put(&im, x, y, RGBA(8, 8, 12, 160)); /* (the icons' black: a colour less) */
            else {
                /* the highlight: 0.6 radians round from a (the ellipse's
                 * own angle, as draw_ellipse_ring takes it) */
                float e = fmodf(atan2f(dy / ry, dx / rx) - a + 4.0f * PI, 2.0f * PI);
                pix_put(&im, x, y, e <= 0.6f ? col_lerp(c, COL_WHITE, 0.7f) : c);
            }
        }
    return im;
}

/* the ball portal's symbol (icon_draw_ball at 0.55 of a block: its parts
 * thinner than a pixel), at 16 x 48: the outline, the colour and a white
 * cross, 7 pixels across */
static Img pix_portal_ball(Color c)
{
    Img im = pix_new(16, 48);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 16; x++) {
            float dx = x + 0.5f - 8.5f, dy = y + 0.5f - 24.5f, d = sqrtf(dx * dx + dy * dy);
            if (d > 3.3f) continue;
            pix_put(&im, x, y, d > 2.3f ? RGB(8, 8, 12) : (fabsf(dx) < 0.5f || fabsf(dy) < 0.5f) && d > 0.5f ? COL_WHITE : c);
        }
    return im;
}

static void emit_portals(GbaGen *g)
{
    static const Color cols[7] = {COL_PORTAL_CUBE, COL_PORTAL_SHIP, COL_PORTAL_BALL, COL_PORTAL_UFO,
                                  COL_PORTAL_WAVE, COL_PORTAL_FLIP, COL_PORTAL_NORMAL};
    static const char *names[7] = {"PORTAL_CUBE", "PORTAL_SHIP", "PORTAL_BALL", "PORTAL_UFO",
                                   "PORTAL_WAVE", "PORTAL_FLIP", "PORTAL_NORMAL"};
    Group gr = {.nvar = 7, .ncol = GLOW_FIRST - 1};
    for (int f = 0; f < PORTAL_FRAMES; f++) {
        for (int v = 0; v < 7; v++) {
            /* the highlight's angle is time * 3 */
            gr.pic[gr.npic][v] = pix_portal(cols[v], (float)(f * 2.0 * M_PI / PORTAL_FRAMES / 3.0));
        }
        gr.npic++;
    }
    /* the symbols: the five vehicles, gravity up and down */
    for (int s = 0; s < 7; s++) {
        for (int v = 0; v < 7; v++) {
            PortalArg a = {cols[v], s < 5 ? s : -1, s == 5 ? 1 : (s == 6 ? -1 : 0)};
            gr.pic[gr.npic][v] = s == MODE_BALL ? pix_portal_ball(cols[v])
                                 : s >= 5      ? pix_portal_arrow(a.arrow)
                                               : render_crisp(16, 48, draw_portal_symbol, &a, 64);
        }
        gr.npic++;
    }
    group_quantize(&gr);
    s_cur_bank0 = OBJ_PAL_PORTAL;
    s_cur_nbank = 7;
    for (int f = 0; f < PORTAL_FRAMES; f++) {
        /* a 16x48 picture as a 16x32 and a 16x16 sprite */
        int t = add_tiles(g, f ? NULL : "PORTAL", gr.idx[f], 16, 48, 0, 0, 16, 32);
        add_tiles(g, NULL, gr.idx[f], 16, 48, 0, 32, 16, 16);
        (void)t;
    }
    fprintf(g->hdr, "#define PORTAL_FRAMES %d\n#define PORTAL_FRAME_TILES 12\n", PORTAL_FRAMES);
    /* the symbols, 16x16 around the centre */
    for (int sy = 0; sy < 7; sy++) add_tiles(g, sy ? NULL : "PORTAL_SYM", gr.idx[PORTAL_FRAMES + sy], 16, 48, 0, 16, 16, 16);
    for (int v = 0; v < 7; v++) {
        set_pal(g, OBJ_PAL_PORTAL + v, names[v], gr.pal[v], gr.ncol);
        glow_ramp(OBJ_PAL_PORTAL + v, cols[v], 0.35f); /* render_portal's */
    }
    group_free(&gr);
}

/* Speed portals and coins (one palette). */
static void emit_speed(GbaGen *g)
{
    Group gr = {.nvar = 1, .ncol = GLOW_FIRST - 1};
    for (int i = 0; i < 4; i++) gr.pic[gr.npic++][0] = pix_speed(i);
    gr.pic[gr.npic++][0] = pix_coin(0);
    gr.pic[gr.npic++][0] = pix_coin(1);
    group_quantize(&gr);
    s_cur_bank0 = OBJ_PAL_SPEED;
    s_cur_nbank = 1;
    add_tiles(g, "SPEED", gr.idx[0], 16, 16, 0, 0, 16, 16);
    for (int i = 1; i < 4; i++) add_tiles(g, NULL, gr.idx[i], 16, 16, 0, 0, 16, 16);
    add_tiles(g, "COIN", gr.idx[4], 16, 16, 0, 0, 16, 16);
    add_tiles(g, "COIN_GHOST", gr.idx[5], 16, 16, 0, 0, 16, 16);
    set_pal(g, OBJ_PAL_SPEED, "SPEED", gr.pal[0], gr.ncol);
    /* the coins' glow (the speed portals, in four colours, share the bank:
     * theirs is left out) */
    glow_ramp(OBJ_PAL_SPEED, COL_COIN, 0.35f);
    group_free(&gr);
}

/* Saws (their outline is the level's: drawn green and magenta, the pixels
 * that differ get colour 15, which the ROM sets every frame), the
 * practice checkpoints and the finish line's glow. */

/* render_checkpoint at 16 x 16: its diamond (11 of the PC's pixels from
 * the middle to a corner) and its dark edge (3 more), by the pixels'
 * middles: a diamond 6 pixels across with a pixel's edge */
static Img pix_checkpoint(void)
{
    Img im = pix_new(16, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float d = fabsf(x + 0.5f - 8.0f) + fabsf(y + 0.5f - 8.0f);
            if (d <= 11.0f * K) pix_put(&im, x, y, RGB(90, 255, 120));
            else if (d <= 14.0f * K) pix_put(&im, x, y, RGB(0, 40, 10));
        }
    return im;
}

static void emit_misc(GbaGen *g)
{
    /* (13 colours: 14 is the saws' glow, 15 their outline) */
    /* (the saws' frames, turned through a tooth's 30 degrees, then the
     * checkpoint: pictures 0..SAW_FRAMES-1, SAW_FRAMES.., 2 * SAW_FRAMES) */
    Group gr = {.nvar = 2, .ncol = 13};
    for (int v = 0; v < 2; v++) {
        Color edge = v ? RGB(255, 0, 255) : RGB(0, 255, 0);
        for (int f = 0; f < SAW_FRAMES; f++) {
            /* render_level's: the big one's ring and dot the PC's (its
             * ring 2 pixels out from half the radius, at their middle) */
            float big = BLOCK_PX * 0.98f, angle = f * (PI / 6.0f) / SAW_FRAMES;
            gr.pic[f][v] = pix_saw(32, big, angle, SPIKE_FILL, edge, (big * 0.5f + 1.0f) * K, big * 0.18f * K);
            gr.pic[SAW_FRAMES + f][v] = pix_saw(16, BLOCK_PX * 0.5f, angle, SPIKE_FILL, edge, 2.5f, 0.8f);
        }
        gr.pic[2 * SAW_FRAMES][v] = pix_checkpoint();
    }
    gr.npic = 2 * SAW_FRAMES + 1;
    /* the finish line: a white line and its glow, a ramp to white; drawn by
     * hand here (render_level draws it with additive blending, which a
     * transparent canvas can't keep), in GATE_LEVELS colours of its own
     * after the others' (quantized with the saws, the ramp got few) */
    enum { GATE_LEVELS = 7 };
    gr.ncol = 13 - GATE_LEVELS;
    group_quantize(&gr);
    uint8_t *gate = malloc(32 * 64);
    for (int l = 0; l < GATE_LEVELS; l++) {
        /* to 0.55 of white, then the line itself (4 pixels on the PC):
         * level l the mean of its columns' */
        int sum = 0, n = 0;
        for (int x = 0; x < 32; x++) {
            int lx = x >= 31 ? GATE_LEVELS - 1 : x * (GATE_LEVELS - 1) / 31;
            if (lx != l) continue;
            sum += x >= 31 ? 255 : 16 + x * 124 / 30;
            n++;
            for (int y = 0; y < 64; y++) gate[y * 32 + x] = (uint8_t)(gr.ncol + 1 + l);
        }
        for (int v = 0; v < gr.nvar; v++)
            gr.pal[v][gr.ncol + 1 + l][0] = gr.pal[v][gr.ncol + 1 + l][1] = gr.pal[v][gr.ncol + 1 + l][2] = sum / n;
    }
    /* the colours that differ between the two drawings are the outline's */
    int edge = 0;
    for (int c = 1; c <= gr.ncol; c++) {
        int d = abs(gr.pal[0][c][0] - gr.pal[1][c][0]) + abs(gr.pal[0][c][1] - gr.pal[1][c][1]) +
                abs(gr.pal[0][c][2] - gr.pal[1][c][2]);
        if (d > 60)
            for (int p = 0; p < gr.npic; p++) {
                Img *im = &gr.pic[p][0];
                for (int i = 0; i < im->w * im->h; i++)
                    if (gr.idx[p][i] == c) gr.idx[p][i] = 15;
            }
        if (d > 60) edge++;
    }
    if (edge > 1) fprintf(stderr, "gba_tool: note: %d saw outline colours merged\n", edge);
    gr.pal[0][15][0] = 0;
    gr.pal[0][15][1] = 255;
    gr.pal[0][15][2] = 0;
    s_cur_bank0 = OBJ_PAL_MISC;
    s_cur_nbank = 1;
    add_tiles(g, "SAW_BIG", gr.idx[0], 32, 32, 0, 0, 32, 32);
    add_tiles(g, "SAW_SMALL", gr.idx[SAW_FRAMES], 16, 16, 0, 0, 16, 16);
    add_tiles(g, "CHECKPOINT", gr.idx[2 * SAW_FRAMES], 16, 16, 0, 0, 16, 16);
    {
        static uint32_t frames[SAW_FRAMES * 20][8];
        for (int f = 0; f < SAW_FRAMES; f++) {
            idx_tiles(frames[f * 16], gr.idx[f], 32, 32);
            idx_tiles(frames[SAW_FRAMES * 16 + f * 4], gr.idx[SAW_FRAMES + f], 16, 16);
        }
        gba_gen_blob(g, "g_saw_frames", "uint32_t", frames, sizeof(frames));
    }
    add_tiles(g, "GATE", gate, 32, 64, 0, 0, 32, 64);
    set_pal(g, OBJ_PAL_MISC, "MISC", gr.pal[0], 15);
    fprintf(g->hdr, "#define MISC_GLOW 14\n#define MISC_EDGE 15\n");
    {
        /* the finish line's colours (it adds its light: not flashed) */
        unsigned mask = 0;
        for (int i = 0; i < 32 * 64; i++) mask |= 1u << gate[i];
        fprintf(g->hdr, "#define MISC_GATE_COLORS 0x%04X\n", mask & ~1u);
    }
    free(gate);
    group_free(&gr);
}

/* Particle shapes: 1-bit masks, every pixel 1; the ROM multiplies them by
 * the colour it picks (a tile row times the colour index). */
static void emit_fx(GbaGen *g)
{
    static uint32_t m[17][8];
    memset(m, 0, sizeof(m));
    /* squares of 1..8 pixels, centred */
    for (int s = 1; s <= 8; s++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int o = (8 - s) / 2;
                if (x >= o && x < o + s && y >= o && y < o + s) m[s - 1][y] |= 1u << (x * 4);
            }
    /* discs of radius 1..4 (diameters 2..8) */
    for (int r = 1; r <= 8; r++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                double dx = x + 0.5 - 4, dy = y + 0.5 - 4, rr = r * 0.5;
                if (dx * dx + dy * dy <= rr * rr + 0.3) m[8 + r - 1][y] |= 1u << (x * 4);
            }
    /* the shapes in each of the 15 colours of the particles' palette:
     * OT_FX + (colour - 1) * 16 + shape (0..7 squares, 8..15 discs) */
    for (int c = 1; c < 16; c++)
        for (int sh = 0; sh < 16; sh++) {
            uint32_t t[8];
            for (int y = 0; y < 8; y++) t[y] = m[sh][y] * (uint32_t)c;
            if (s_ntiles == s_tcap) {
                s_tcap *= 2;
                s_tiles = realloc(s_tiles, (size_t)s_tcap * 32);
            }
            memcpy(s_tiles + s_ntiles * 8, t, 32);
            if (c == 1 && sh == 0) fprintf(g->hdr, "#define OT_FX %d\n", OBJ_STATIC_TILE + s_ntiles);
            s_ntiles++;
        }
    /* a ring, 32x32 (radius 15, 1.5 pixels thick), scaled by the sprite's matrix */
    static uint32_t ring[16][8];
    memset(ring, 0, sizeof(ring));
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            double dx = x + 0.5 - 16, dy = y + 0.5 - 16, d = sqrt(dx * dx + dy * dy);
            if (d >= 13.5 && d <= 15.5) ring[(y / 8) * 4 + x / 8][y & 7] |= 1u << ((x & 7) * 4);
        }
    gba_gen_blob(g, "g_fx_ring", "uint32_t", ring, sizeof(ring));
}

/* The player's vehicles for each icon, drawn by icons.c in stand-in colours
 * and sorted into the parts the garage colours (PC_* in art.h). */
#define S1 RGB(200, 40, 0)
#define S2 RGB(0, 60, 200)
typedef struct {
    int mode, icon;
    float angle, block; /* block: pixels a block, the PC's (BLOCK_PX in a run) */
} VehArg;

static void draw_vehicle(const void *a)
{
    const VehArg *v = a;
    /* (in a run the wave is drawn smaller: play_draw.c) */
    if (v->mode == MODE_WAVE && v->block == BLOCK_PX) icon_draw_wave(0.0f, 0.0f, BLOCK_PX * 0.85f, v->angle, S1, S2);
    else icon_draw_mode(v->mode, 0.0f, 0.0f, v->block, v->angle, 0, v->icon, S1, S2);
}

static uint8_t classify(uint32_t c)
{
    static const Color base[4] = {RGB(8, 8, 12), S1, RGB(250, 50, 0), S2};
    const Color dome = RGB(200, 240, 255);
    int a = alpha_of(c);
    if (a < 50) return 0;
    if (a < 128) return PC_DOME;
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 8; i++) {
        Color e = i < 4 ? base[i] : col_lerp(base[i - 4], dome, 0.35f);
        int dr = r_of(c) - (int)COL_R(e), dg = g_of(c) - (int)COL_G(e), db = b_of(c) - (int)COL_B(e);
        int d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    static const uint8_t role[8] = {PC_K, PC_C1, PC_C1HI, PC_C2, PC_K_DOME, PC_C1_DOME, PC_C1HI_DOME, PC_C2_DOME};
    return role[best];
}

/* how much of pixel (x, y) the ball's cross covers (bars half hw wide,
 * reaching len from the middle (cx, cy), turned by angle), 0..1 */
static float cross_cover(int x, int y, float cx, float cy, float angle, float hw, float len)
{
    enum { N = 8 };
    float co = cosf(angle), si = sinf(angle);
    int n = 0;
    for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
            float px = (float)x + (i + 0.5f) / N - cx, py = (float)y + (j + 0.5f) / N - cy;
            float u = fabsf(px * co + py * si), v = fabsf(py * co - px * si);
            n += (u < hw && v < len) || (v < hw && u < len);
        }
    return (float)n / (N * N);
}

/*
 * The ball, pixel by pixel. icons.c's rings are thinner than a pixel at
 * these sizes (in a run its cyan ring is half a pixel), so drawn and
 * brought down they come out as specks that change from frame to frame.
 * Here each ring is whole pixels round the middle of a pixel, r pixels
 * out (the ball 2r + 1 across: 13 in a run, where the PC's is 11.8, so
 * that it sits on the ground as the cube does; 15 in the garage): the
 * outline, colour 1, a dark ring, and the cross of colour 2 edged dark
 * (from the middle into colour 1's band), with colour 1's dot (or, every
 * other icon, diamond) on it.
 */
static void ball_idx(uint8_t *idx, int size, int r, float block, int icon, float angle)
{
    float cx = size / 2 - 0.5f, cy = cx, R = r + 0.3f, dot = 0.1f * block, cross = R - 1.8f;
    float co = cosf(angle), si = sinf(angle);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float u = fabsf(dx * co + dy * si), v = fabsf(dy * co - dx * si);
            uint8_t c;
            if (d > R) c = 0;
            else if (d > R - 1.0f) c = PC_K;
            else if (icon & 1 ? u + v <= 0.12f * block : d <= dot) c = PC_C1;
            else if (d <= cross && cross_cover(x, y, cx, cy, angle, 0.5f, R) > 0.5f) c = PC_C2;
            else if (d <= cross && cross_cover(x, y, cx, cy, angle, 1.5f, R) > 0.5f) c = PC_K;
            else if (d > R - 2.4f) c = PC_C1;
            else if (d > R - 3.4f) c = PC_K;
            else c = PC_C1;
            idx[y * size + x] = c;
        }
}

/*
 * The vehicles' pictures. The PC's lines are 2 of its pixels wide, 0.7 of
 * a pixel here, and the bands of colour between them often under 2: drawn
 * and brought down by majority, a line or a band falls on a pixel or
 * between two and is there or lost by where it falls, so it comes and goes
 * as the picture turns (and an upright cube's inner border, both its edges
 * rounded to the same pixel, was never there). So each vehicle has a
 * design: its upright picture at VS x VS points a pixel (parts as
 * classify's), drawn by icons.c with its corners moved so that its
 * straight edges are on whole pixels and every band between them is a
 * pixel or more across (a kind of band that can't be, left out whole:
 * vehicle_design).
 * Every step of its turn is a design turned, each pixel the part at its
 * middle (turn_design): a line or a band a pixel or more across can't
 * pass between the middles of two pixels next to each other, so it shows
 * at every step. Upright (and a quarter turn round) that's the design as
 * it is; the other steps are turned from a second design with its edges
 * left nearer the PC's and its slanted lines widened to a pixel.
 */
#define VS 8

static int dark_part(uint8_t p) { return p == PC_K || p == PC_K_DOME; }

/* what an outline edges: clear, or the dome's see-through glass */
static int open_part(uint8_t p) { return p == 0 || p == PC_DOME; }

/* part p seen through the dome's glass */
static uint8_t under_dome(uint8_t p)
{
    return p == PC_K ? PC_K_DOME : p == PC_C1 ? PC_C1_DOME : p == PC_C1HI ? PC_C1HI_DOME : p == PC_C2 ? PC_C2_DOME : p;
}

/* point i dark (as under the dome, if it is) */
static void make_dark(uint8_t *f, int i)
{
    f[i] = f[i] == PC_DOME || f[i] >= PC_K_DOME ? PC_K_DOME : PC_K;
}

/* The small cube riding in the ship and the UFO: its middle (x, y) and its
 * size, in blocks, where icons.c draws it (size 0: no rider) */
static float rider_at(int mode, float *x, float *y)
{
    *x = mode == MODE_SHIP ? -0.08f : 0.0f;
    *y = mode == MODE_SHIP ? -0.3f : -0.16f;
    return mode == MODE_SHIP ? 0.46f : mode == MODE_UFO ? 0.40f : 0.0f;
}

typedef struct {
    int mode, icon, middle; /* middle: drawn at virtual (0, 0), not where it rides */
    float block;
} RiderArg;

static void draw_rider(const void *a)
{
    const RiderArg *r = a;
    float x, y, s = rider_at(r->mode, &x, &y), g = draw_pixel_grid();
    if (r->middle) x = y = 0.0f;
    /* (its middle on the grid, as icons.c's xf_pt puts it) */
    icon_draw_cube(roundf(x * r->block * g) / g, roundf(y * r->block * g) / g, r->block * s, 0.0f, r->icon, S1, S2);
}

/* f (n x n points) made the same each side of its middle, across or
 * down, where it differs from that only along the edges between its
 * parts, by a point or two (the renderer puts a point an edge goes
 * through the middle of on one side of it, the same side whichever way
 * the edge slants, and an edge moved by the fitting can fall anywhere
 * between points) */
static void symmetric(uint8_t *f, int n)
{
    for (int down = 0; down < 2; down++) {
        int near = 1, any = 0;
        for (int y = 0; y < n && near; y++)
            for (int x = 0; x < n && near; x++) {
                uint8_t m = down ? f[(n - 1 - y) * n + x] : f[y * n + n - 1 - x];
                if (f[y * n + x] == m) continue;
                any = 1;
                /* (the part the other side within two points) */
                near = 0;
                for (int j = -2; j <= 2 && !near; j++)
                    for (int i = -2; i <= 2 && !near; i++)
                        near = x + i >= 0 && y + j >= 0 && x + i < n && y + j < n && f[(y + j) * n + x + i] == m;
            }
        if (any && near)
            for (int y = 0; y < (down ? n / 2 : n); y++)
                for (int x = 0; x < (down ? n : n / 2); x++)
                    f[y * n + x] = down ? f[(n - 1 - y) * n + x] : f[y * n + n - 1 - x];
    }
}

/* a part as the fitting compares two sides of a picture or two bands:
 * colour 1's highlight (only on top) as colour 1 */
static uint8_t fit_plain(uint8_t p) { return p == PC_C1HI ? PC_C1 : p == PC_C1HI_DOME ? PC_C1_DOME : p; }

/* patch[] (n x n): each point's patch of f (4-connected, of one part as
 * fit_plain sees it), as the first point of it; returns how many */
static int patches(int *patch, const uint8_t *f, int n)
{
    int *stack = malloc(sizeof(int) * n * n), count = 0;
    for (int i = 0; i < n * n; i++) patch[i] = -1;
    for (int s = 0; s < n * n; s++) {
        if (patch[s] >= 0) continue;
        int top = 0;
        stack[top++] = s;
        patch[s] = s;
        count++;
        while (top) {
            int q = stack[--top], qy = q / n, qx = q % n;
            const int nb[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (int k = 0; k < 4; k++) {
                int y = qy + nb[k][0], x = qx + nb[k][1], r = y * n + x;
                if (y < 0 || x < 0 || y >= n || x >= n || patch[r] >= 0 || fit_plain(f[r]) != fit_plain(f[s])) continue;
                patch[r] = s;
                stack[top++] = r;
            }
        }
    }
    free(stack);
    return count;
}

/*
 * Fitting a picture to the pixels, across (x) and down (y) on their own,
 * the way a cube's nested squares are drawn by hand. An edge is a place a
 * part changes along VS/2 lines or more (not a step of a slanted edge);
 * between two edges next to each other on a line is a band of one part
 * (colour 1's highlight counted as colour 1). The edges are put on whole
 * pixels as near their places as they can be, every band a pixel or more
 * across (fit_solve: a thin band takes its pixel from a wider one next to
 * it). Where that can't be, or where a band under a pixel across costs
 * the picture more kept than lost, a kind of band is left out (fit_warp):
 * the picture is drawn again with those bands squeezed to nothing, the
 * bands either side of each meeting at its middle (two lines either side
 * of a colour left out become one), and fitted again. A picture the same
 * each side of its middle (but for the highlight on top) is fitted the
 * same way each side.
 */
#define FIT_EDGES 128
#define FIT_DROPS 16

typedef struct {
    uint8_t part; /* (dark parts as PC_K; 255: not one part on every line) */
    uint8_t outline, inner, joins, seen; /* outline: a line between the picture and what's round it;
                                          * inner: between two lines on every line it's on;
                                          * joins: between two lines (not a line's two sides), which
                                          * leaving it out would join */
    short lo, hi; /* the first and last lines it is on */
    int lines;    /* how many (each side of the middle, in a picture the same each side) */
} FitBand;

typedef struct {
    int n, si, sj, sym, ne, edge[FIT_EDGES];
    int *at;                  /* [b]: the edge before point b, or -1 */
    uint8_t outer[FIT_EDGES]; /* an edge of the picture against what's round it */
    FitBand *band;            /* [a * ne + b]: between edges a < b on some line */
    int place[FIT_EDGES];     /* the pixel each edge went to (fit_solve) */
    float line;               /* what a line's pixel weighs against a colour's (fit_weight) */
    const int *patch;         /* (for fit_lines) the picture's patches */
} FitAxis;

/* (point j of line i of p, on axis e's side `side`) */
#define AT(p, i, j) (p)[(i) * e->si + (side ? e->n - 1 - (j) : (j)) * e->sj]

/* the bands on e's lines of f (point j of line i at f[i * si + j * sj];
 * in a picture the same each side of its middle, both sides as the right
 * one, side 1 the left one turned round) */
static void fit_lines(FitAxis *e, const uint8_t *f)
{
    const int n = e->n, ne = e->ne;
    for (int side = 0; side <= e->sym; side++)
        for (int i = 0; i < n; i++) {
            int prev = e->sym ? 0 : -1, weak = 0;
            for (int j = e->sym ? n / 2 + 1 : 1; j < n; j++) {
                uint8_t a = AT(f, i, j - 1), b = AT(f, i, j);
                if (fit_plain(a) == fit_plain(b)) continue;
                if (e->at[j] < 0) {
                    weak = 1;
                    continue;
                }
                if (open_part(a) != open_part(b)) e->outer[e->at[j]] = 1;
                if (prev >= 0) {
                    int lo = e->edge[prev], before = e->sym && prev == 0 ? n - 1 - j : lo - 1;
                    uint8_t part = fit_plain(AT(f, i, lo)), in = AT(f, i, before);
                    FitBand *k = &e->band[prev * ne + e->at[j]];
                    if (dark_part(part)) {
                        part = PC_K;
                        k->outline |= open_part(in) || open_part(b);
                    }
                    if (weak || (k->seen && k->part != part)) part = 255;
                    if (!k->seen) k->inner = 1;
                    k->inner &= dark_part(in) && dark_part(b);
                    k->joins |= dark_part(in) && dark_part(b) && AT(e->patch, i, before) != AT(e->patch, i, j);
                    if (!k->seen || i < k->lo) k->lo = (short)i;
                    if (!k->seen || i > k->hi) k->hi = (short)i;
                    k->lines += !(e->sym && prev == 0 && side);
                    k->part = part;
                    k->seen = 1;
                }
                prev = e->at[j];
                weak = 0;
            }
        }
}

/* the edges and bands across the lines of f (point j of line i at
 * f[i * si + j * sj]), its patches patch[] */
static void fit_scan(FitAxis *e, const uint8_t *f, const int *patch, int n, int si, int sj)
{
    int sym = 1, side = 0;
    e->n = n;
    e->si = si;
    e->sj = sj;
    e->patch = patch;
    for (int i = 0; i < n && sym; i++)
        for (int j = 0; j < n / 2 && sym; j++) sym = fit_plain(AT(f, i, j)) == fit_plain(AT(f, i, n - 1 - j));
    e->sym = sym;
    e->ne = 0;
    e->at = malloc(sizeof(int) * (n + 1));
    memset(e->outer, 0, sizeof(e->outer));
    /* (the middle the first edge, in a picture the same each side) */
    for (int b = 0; b <= n; b++) e->at[b] = -1;
    if (sym) {
        e->at[n / 2] = 0;
        e->edge[e->ne++] = n / 2;
    }
#define CUT(i, j) (AT(f, i, (j) - 1) != AT(f, i, j))
    for (int b = sym ? n / 2 + 1 : 1; b < n && e->ne < FIT_EDGES; b++) {
        /* (a run that goes on a point to the side, between the same two
         * parts, a step of a slanted edge, doesn't count unless it's long) */
        int strong = 0;
        for (side = 0; side <= sym && !strong; side++)
            for (int i = 0, run = 0; i <= n && !strong; i++) {
                if (i < n && CUT(i, b)) {
                    run++;
                    continue;
                }
                int step = 0;
                for (int s = -1; s <= 1 && run; s += 2)
                    for (int l = i - run - 1, m = i - run; l <= i; l += run + 1, m = i - 1)
                        step |= l >= 0 && l < n && b + s >= 1 && b + s < n && CUT(l, b + s) &&
                                AT(f, l, b + s - 1) == AT(f, m, b - 1) && AT(f, l, b + s) == AT(f, m, b);
                strong = run >= VS * 2 || (run >= VS / 2 && !step);
                run = 0;
            }
        if (strong) {
            e->at[b] = e->ne;
            e->edge[e->ne++] = b;
        }
    }
#undef CUT
    e->band = calloc((size_t)e->ne * e->ne, sizeof(FitBand));
    fit_lines(e, f);
}

#undef AT

/* band (a, b)'s width, in points */
static int fit_width(const FitAxis *e, int a, int b)
{
    return e->sym && a == 0 ? 2 * (e->edge[b] - e->n / 2) : e->edge[b] - e->edge[a];
}

/* band (a, b)'s area, in pixels */
static float fit_area(const FitAxis *e, int a, int b)
{
    return (float)(fit_width(e, a, b) * e->band[a * e->ne + b].lines) / (VS * VS);
}

/* whether bands (a, b) of axis da and (c, d) of axis db are within a
 * pixel of each other (each, in a picture the same each side of its
 * middle, on both sides) */
static int fit_near(const FitAxis *ax, int da, int a, int b, int db, int c, int d)
{
    int box[2][2][4];
    for (int k = 0; k < 2; k++) {
        const FitAxis *e = &ax[k ? db : da];
        int p = k ? c : a, q = k ? d : b, lo = e->edge[p], hi = e->edge[q], n = e->n;
        const FitBand *s = &e->band[p * e->ne + q];
        if (e->sym && p == 0) lo = n - hi;
        for (int m = 0; m < 2; m++) {
            int *along = (k ? db : da) ? &box[k][m][2] : &box[k][m][0];
            int *across = (k ? db : da) ? &box[k][m][0] : &box[k][m][2];
            along[0] = m && e->sym ? n - hi : lo;
            along[1] = m && e->sym ? n - lo : hi;
            across[0] = s->lo;
            across[1] = s->hi + 1;
        }
    }
    for (int m = 0; m < 2; m++)
        for (int o = 0; o < 2; o++) {
            const int *u = box[0][m], *v = box[1][o];
            if (u[0] - VS < v[1] && v[0] < u[1] + VS && u[2] - VS < v[3] && v[2] < u[3] + VS) return 1;
        }
    return 0;
}

/* what leaving out a pixel of band s (w points across) takes away,
 * against a colour's: a line's `line`; the body's colour (colour 1) half
 * as much again; a colour under a pixel across inside a line (between two
 * on every line it's on: a stripe's, a mouth's) half; four times that for
 * a band that would join two lines (a cube's three stripes into one) */
static float fit_weight(const FitBand *s, int w, float line)
{
    float k = s->part == PC_K ? line : s->part == PC_C1 || s->part == PC_C1_DOME ? 1.5f : s->inner && w < VS ? 0.5f : 1.0f;
    return s->joins ? 4.0f * k : k;
}

/*
 * The kind of band (a, b) of axis down round it: the bands of its part and
 * width (within a point), across and down, each within a pixel of another
 * of them (a frame's sides and ends; not a band of that width somewhere
 * else in the picture) or along the same lines (a cube's stripes), marked
 * in mark[] (a flag a band, each axis's as its band[]). Returns what
 * leaving them out takes away (fit_weight, by their areas in pixels).
 */
static float fit_kind(uint8_t *const mark[2], const FitAxis *ax, int down, int a, int b)
{
    const FitBand *s = &ax[down].band[a * ax[down].ne + b];
    int w = fit_width(&ax[down], a, b);
    float sum = 0.0f;
    for (int k = 0; k < 2; k++) memset(mark[k], 0, (size_t)ax[k].ne * ax[k].ne);
    mark[down][a * ax[down].ne + b] = 1;
    for (int grew = 1; grew;) {
        grew = 0;
        for (int k = 0; k < 2; k++)
            for (int c = 0; c < ax[k].ne; c++)
                for (int d = c + 1; d < ax[k].ne; d++) {
                    const FitBand *t = &ax[k].band[c * ax[k].ne + d];
                    uint8_t *m = &mark[k][c * ax[k].ne + d];
                    if (*m || !t->seen || t->outline || t->part != s->part || abs(fit_width(&ax[k], c, d) - w) > 1)
                        continue;
                    if (k == down && abs(t->lo - s->lo) <= 1 && abs(t->hi - s->hi) <= 1) *m = grew = 1;
                    for (int l = 0; l < 2 && !*m; l++)
                        for (int p = 0; p < ax[l].ne && !*m; p++)
                            for (int q = p + 1; q < ax[l].ne && !*m; q++)
                                if (mark[l][p * ax[l].ne + q] && fit_near(ax, l, p, q, k, c, d)) *m = grew = 1;
                }
    }
    for (int k = 0; k < 2; k++)
        for (int c = 0; c < ax[k].ne; c++)
            for (int d = c + 1; d < ax[k].ne; d++)
                if (mark[k][c * ax[k].ne + d]) sum += fit_area(&ax[k], c, d);
    return sum * fit_weight(s, w, ax[down].line);
}

/*
 * Put axis e's edges on pixels (e->place[]): each edge on a whole pixel
 * near it, in order, at the least cost over them, found edge by edge (for
 * each edge, each pixel it can go to and the first edge on that pixel
 * with it: the bands from that one on lost). The cost: the distance each
 * edge is moved, half how far each band between two edges next to each
 * other is from its width (of two ways as near, the one more like it),
 * and for a band lost what it takes away (fit_weight, by its area: a band
 * a pixel or more across only if it can't be kept, an outline never). An
 * edge of the picture against what's round it is only rounded; the
 * middle of a picture the same each side stays. Returns whether a band of
 * one part was lost, -1 if the edges can't be placed at all; the cost in
 * *total.
 */
static int fit_solve(FitAxis *e, float *total)
{
    const int n = e->n, ne = e->ne, size = n / VS, nc = 4;
    const float big = 1000.0f;
    float *keep = calloc((size_t)ne * ne, sizeof(float)), pos[FIT_EDGES];
    for (int a = 0; a < ne; a++) {
        pos[a] = (float)e->edge[a] / VS;
        for (int b = a + 1; b < ne; b++) {
            const FitBand *k = &e->band[a * ne + b];
            int w = fit_width(e, a, b);
            float c = fit_area(e, a, b);
            if (k->outline || (k->seen && open_part(k->part))) keep[a * ne + b] = big * big;
            else if (k->seen && k->part == 255 && w > VS / 2) keep[a * ne + b] = big + c;
            else if (k->seen && k->part != 255) keep[a * ne + b] = (w < VS ? 0.0f : big) + c * fit_weight(k, w, e->line);
        }
    }
    int *cand = malloc(sizeof(int) * ne * nc), *from = malloc(sizeof(int) * ne * nc * ne);
    float *cost = malloc(sizeof(float) * ne * nc * ne);
    for (int g = 0; g < ne; g++)
        for (int c = 0; c < nc; c++) {
            /* (the two pixels either side, and one more each way) */
            int p = (int)floorf(pos[g]) - 1 + c, r = (int)floorf(pos[g] + 0.5f);
            if (e->sym && g == 0) p = size / 2;
            else if (e->outer[g] && fabsf(pos[g] - floorf(pos[g]) - 0.5f) > 0.01f) p = r;
            else if (e->outer[g]) p = (int)floorf(pos[g]) + (c & 1);
            cand[g * nc + c] = p < 0 ? 0 : p > size ? size : p;
        }
    for (int i = 0; i < ne * nc * ne; i++) cost[i] = 1e30f;
    for (int g = 0; g < ne; g++)
        for (int c = 0; c < nc; c++) {
            /* (of two pixels as near, the one towards the picture's middle) */
            int p = cand[g * nc + c];
            float d = (float)p - pos[g], own = fabsf(d) + (float)abs(2 * p - size) * 0.0001f;
            if (!g) {
                cost[c * ne] = own;
                continue;
            }
            for (int pc = 0; pc < nc; pc++) {
                int q = cand[(g - 1) * nc + pc];
                if (q > p) continue;
                float wd = fabsf((float)(p - q) - (pos[g] - pos[g - 1])) * 0.5f;
                for (int s = 0; s < g; s++) {
                    float was = cost[((g - 1) * nc + pc) * ne + s], lose = 0.0f;
                    if (was >= 1e29f) continue;
                    if (q == p)
                        for (int h = s; h < g; h++) lose += keep[h * ne + g];
                    int to = (g * nc + c) * ne + (q == p ? s : g);
                    if (was + own + wd + lose < cost[to]) {
                        cost[to] = was + own + wd + lose;
                        from[to] = pc * ne + s;
                    }
                }
            }
        }
    int bc = 0, bs = 0, found = 0;
    *total = 0.0f;
    for (int c = 0; c < nc && ne; c++)
        for (int s = 0; s < ne; s++)
            if (cost[((ne - 1) * nc + c) * ne + s] < cost[((ne - 1) * nc + bc) * ne + bs]) {
                bc = c;
                bs = s;
            }
    if (ne) *total = cost[((ne - 1) * nc + bc) * ne + bs];
    if (*total >= 1e29f) found = -1;
    for (int g = ne - 1; g >= 0 && found >= 0; g--) {
        e->place[g] = cand[g * nc + bc];
        if (g) {
            int fr = from[(g * nc + bc) * ne + bs];
            bc = fr / ne;
            bs = fr % ne;
        }
    }
    for (int a = 0; a < ne && found >= 0; a++)
        for (int b = a + 1; b < ne; b++)
            if (keep[a * ne + b] > 0.0f && e->place[a] == e->place[b] && e->band[a * ne + b].part != 255) found = 1;
    free(keep);
    free(cand);
    free(from);
    free(cost);
    return found;
}

/* (the most knots a warp has, and the most warps one after another a
 * picture is drawn through) */
#define FIT_KNOTS (2 * FIT_EDGES + 2)
#define FIT_WARPS (FIT_DROPS + 1)

/* A warp, one way: knots src[k] -> dst[k] (in points, src ascending),
 * what's between two moved in proportion */
typedef struct {
    int n;
    float src[FIT_KNOTS], dst[FIT_KNOTS];
} FitWarp;

/* warps one after another, across and down; turned: the last, putting
 * the edges on pixels, for the picture turned (fit_knots's near) */
typedef struct {
    int n;
    FitWarp step[FIT_WARPS][2], turned[2];
} FitChain;

static float warp_at(const FitWarp *w, float v)
{
    int k = 1;
    while (k < w->n - 1 && w->src[k] < v) k++;
    float a = w->src[k - 1], b = w->src[k];
    if (b <= a) return w->dst[k];
    return w->dst[k - 1] + (v - a) * (w->dst[k] - w->dst[k - 1]) / (b - a);
}

/*
 * w: axis e's edges where they are (move: NULL), or to the pixels
 * fit_solve put them on (move: e->place; near: as near their places as
 * they can be and leave each pixel's middle on the same side, for the
 * picture turned), each side of the middle in a picture the same each
 * side; the bands marked in drop[] (if not NULL, as
 * fit_kind marks them) squeezed to nothing at their middle, those next to
 * each other as one. (A band's ends as half a point further out: an edge
 * drawn falls within half a point of the place a part changes.)
 */
static void fit_knots(FitWarp *w, const FitAxis *e, const int *move, int near, const uint8_t *drop)
{
    const int n = e->n, ne = e->ne;
    float lo[FIT_EDGES], hi[FIT_EDGES], s[FIT_KNOTS], d[FIT_KNOTS];
    int nr = 0, m = 0;
    /* (the ranges left out, right of the middle, in order, those that
     * meet as one) */
    for (int a = 0; a < ne && drop; a++)
        for (int b = a + 1; b < ne; b++) {
            if (!drop[a * ne + b]) continue;
            int i = nr++;
            for (; i > 0 && lo[i - 1] > (float)e->edge[a]; i--) {
                lo[i] = lo[i - 1];
                hi[i] = hi[i - 1];
            }
            lo[i] = (float)e->edge[a];
            hi[i] = (float)e->edge[b];
        }
    int ranges = nr > 0;
    for (int i = 1; i < nr; i++)
        if (lo[i] <= hi[ranges - 1]) hi[ranges - 1] = fmaxf(hi[ranges - 1], hi[i]);
        else {
            lo[ranges] = lo[i];
            hi[ranges++] = hi[i];
        }
    nr = ranges;
    /* (near: each edge within half a pixel of its pixel's edge, as near
     * its place as it can be, each band a pixel or more across: the
     * shapes nearer the PC's as the picture turns. The edges of a band a
     * pixel across can't come nearer: two that would go to their middle,
     * till none do.) */
    float off[FIT_EDGES], most = VS / 2 - 1;
    for (int g = 0; g < ne && move; g++) {
        float o = (float)(e->edge[g] - move[g] * VS);
        off[g] = !near || (e->sym && g == 0) ? 0.0f : o < -most ? -most : o > most ? most : o;
    }
    for (int round = 0, again = 1; move && again && round < 4 * ne; round++) {
        again = 0;
        for (int a = 0; a < ne; a++)
            for (int b = a + 1; b < ne; b++) {
                if (!e->band[a * ne + b].seen) continue;
                int c = e->sym && a == 0, w = (move[b] - move[a]) * VS * (c ? 2 : 1);
                float k = c ? 2.0f : 1.0f;
                if (w == 0 ? off[b] == off[a] : (float)w + k * (off[b] - off[a]) >= (float)VS - 0.01f) continue;
                float mid = (off[a] + off[b]) * 0.5f;
                off[a] = c ? 0.0f : mid;
                off[b] = c ? ((float)VS - (float)w) / k : mid;
                again = 1;
            }
    }
    for (int g = 0, r = 0; g <= ne; g++) {
        float at = g < ne ? (float)e->edge[g] : (float)n;
        for (; r < nr && lo[r] <= at; r++) {
            float mid = e->sym && lo[r] == (float)(n / 2) ? (float)(n / 2) : (lo[r] + hi[r]) * 0.5f;
            s[m] = lo[r] - 0.5f;
            d[m++] = mid;
            s[m] = hi[r] + 0.5f;
            d[m++] = mid;
        }
        if (g == ne || (r && at <= hi[r - 1] + 0.5f)) continue;
        s[m] = at;
        d[m] = at;
        if (move) d[m] = (float)(move[g] * VS) + off[g];
        m++;
    }
    /* then mirrored, each side of the middle */
    w->n = 0;
    w->src[w->n] = 0.0f;
    w->dst[w->n++] = 0.0f;
    for (int i = m - 1; e->sym && i >= 0; i--)
        if (s[i] > (float)(n / 2)) {
            w->src[w->n] = (float)n - s[i];
            w->dst[w->n++] = (float)n - d[i];
        }
    for (int i = 0; i < m; i++) {
        if (s[i] <= 0.0f || s[i] >= (float)n) continue;
        w->src[w->n] = s[i];
        w->dst[w->n++] = d[i];
    }
    w->src[w->n] = (float)n;
    w->dst[w->n++] = (float)n;
    for (int i = 1; i < w->n; i++)
        if (w->dst[i] < w->dst[i - 1]) w->dst[i] = w->dst[i - 1];
}

/* (the warps fine_parts draws through, on the points) */
static const FitChain *s_chain;

static void warp_point(float *x, float *y)
{
    for (int i = 0; i < s_chain->n; i++) {
        *x = warp_at(&s_chain->step[i][0], *x);
        *y = warp_at(&s_chain->step[i][1], *y);
    }
}

/* fn drawn into p (size x size pixels at VS x VS points each, parts as
 * classify's; on a grid of the points: edges on them the same way each
 * side of the middle, strokes their widths, which a grid of the GBA's
 * pixels rounds to one or none), through the warps c (if not NULL) */
static void fine_parts(uint8_t *p, int size, DrawFn fn, const void *arg, const FitChain *c)
{
    s_chain = c;
    gfx_sdl_warp(c && c->n ? warp_point : NULL);
    Img f = render_fine(size, size, VS, fn, arg, 0, K * VS);
    gfx_sdl_warp(NULL);
    for (int i = 0; i < f.w * f.h; i++) p[i] = classify(f.px[i]);
    free(f.px);
}

/* a picture fit_warp fits: drawn into f (n x n points) through c */
typedef void (*FitDraw)(uint8_t *f, int n, const void *arg, const FitChain *c);

/* ax[0], ax[1]: f's (n x n points) edges and bands across and down
 * (fit_scan), put on pixels (fit_solve), mark[] room for fit_kind's marks
 * (for fit_free); *count the number of each part's points (as fit_plain
 * sees it); returns the cost both ways (1e30 if a way's edges can't be
 * placed), *lost whether a band of one part was lost */
static float fit_both(FitAxis *ax, uint8_t **mark, int *count, const uint8_t *f, int n, float line, int *lost)
{
    float sum = 0.0f;
    int *patch = malloc(sizeof(int) * n * n);
    patches(patch, f, n);
    memset(count, 0, sizeof(int) * 16);
    for (int i = 0; i < n * n; i++) count[fit_plain(f[i]) & 15]++;
    fit_scan(&ax[0], f, patch, n, n, 1);
    fit_scan(&ax[1], f, patch, n, 1, n);
    free(patch);
    *lost = 0;
    for (int k = 0; k < 2; k++) {
        float c;
        ax[k].line = line;
        mark[k] = calloc((size_t)ax[k].ne * ax[k].ne + 1, 1);
        int got = fit_solve(&ax[k], &c);
        sum += got < 0 ? 1e30f : c;
        *lost |= got > 0;
    }
    return sum;
}

static void fit_free(FitAxis *ax, uint8_t **mark)
{
    for (int k = 0; k < 2; k++) {
        free(ax[k].at);
        free(ax[k].band);
        free(mark[k]);
    }
}

/* (what leaving out the last of a part, a colour gone from the picture,
 * takes away more, in pixels) */
#define FIT_GONE 30.0f

/*
 * c: the warps that fit the picture draw() draws (n x n points) to the
 * pixels, a line's pixel weighing `line` against a colour's (fit_weight).
 * Each time round, of leaving out no band and leaving out each kind of
 * band a pixel across or less (any kind, while fit_solve loses a band;
 * bands between two edges next to each other: what's between them on
 * every line goes), the one taken is one that loses no band if any does,
 * and of those the one that costs least (what it takes away, and the
 * fitting of what's left), until it's leaving out none; then the edges
 * are put on the pixels.
 */
static void fit_warp(FitChain *c, FitDraw draw, const void *arg, int n, float line)
{
    uint8_t *f = malloc((size_t)n * n);
    c->n = 0;
    for (;;) {
        FitAxis ax[2], bx[2];
        uint8_t *mark[2], *bmark[2], *tried[2], *pick[2] = {NULL, NULL};
        int lost, blost, least, count[16], bcount[16];
        draw(f, n, arg, c);
        float best = fit_both(ax, mark, count, f, n, line, &lost);
        least = lost;
        for (int k = 0; k < 2; k++) tried[k] = calloc((size_t)ax[k].ne * ax[k].ne + 1, 1);
        for (int k = 0; k < 2 && c->n < FIT_WARPS - 1; k++)
            for (int a = 0; a < ax[k].ne; a++) {
                int b = a + 1, i = a * ax[k].ne + b;
                const FitBand *s = &ax[k].band[i];
                if (b == ax[k].ne || !s->seen || s->outline || s->part == 255 || open_part(s->part) || tried[k][i]) continue;
                if (fit_width(&ax[k], a, b) > VS && !lost) continue;
                float cost = fit_kind(mark, ax, k, a, b);
                for (int j = 0; j < 2; j++) {
                    for (int q = 0; q < ax[j].ne * ax[j].ne; q++) tried[j][q] |= mark[j][q];
                    fit_knots(&c->step[c->n][j], &ax[j], NULL, 0, mark[j]);
                }
                c->n++;
                draw(f, n, arg, c);
                c->n--;
                cost += fit_both(bx, bmark, bcount, f, n, line, &blost);
                for (int p = 1; p < 16; p++)
                    if (count[p] && !bcount[p]) cost += FIT_GONE;
                fit_free(bx, bmark);
                if (blost < least || (blost == least && cost < best)) {
                    best = cost;
                    least = blost;
                    for (int j = 0; j < 2; j++) {
                        free(pick[j]);
                        pick[j] = malloc((size_t)ax[j].ne * ax[j].ne + 1);
                        memcpy(pick[j], mark[j], (size_t)ax[j].ne * ax[j].ne + 1);
                    }
                }
            }
        int done = !pick[0];
        for (int k = 0; k < 2; k++) {
            /* (edges that can't be placed: that way as it is) */
            if (done && ax[k].ne && ax[k].place[0] < 0) fit_knots(&c->step[c->n][k], &ax[k], NULL, 0, NULL);
            else fit_knots(&c->step[c->n][k], &ax[k], done ? ax[k].place : NULL, 0, pick[k]);
            if (done) c->turned[k] = c->step[c->n][k];
            if (done && (!ax[k].ne || ax[k].place[0] >= 0)) fit_knots(&c->turned[k], &ax[k], ax[k].place, 1, NULL);
            free(pick[k]);
            free(tried[k]);
        }
        c->n++;
        fit_free(ax, mark);
        if (done) break;
    }
    free(f);
}

/* f (n x n points) without slivers: a patch of one part (4-connected) no
 * more than two points across anywhere (no point of it with the four next
 * to it all of it), left where two shapes drawn over each other don't
 * quite meet, painted over with what's next to it (from the side towards
 * the picture's middle first), a point further in each time round */
static void tidy(uint8_t *f, int n)
{
    const int N = n * n;
    int *patch = malloc(sizeof(int) * N);
    uint8_t *core = calloc((size_t)N, 1), *mask = malloc((size_t)N), *next = malloc((size_t)N);
    patches(patch, f, n);
    for (int y = 1; y < n - 1; y++)
        for (int x = 1; x < n - 1; x++) {
            int p = y * n + x;
            if (patch[p - 1] == patch[p] && patch[p + 1] == patch[p] && patch[p - n] == patch[p] && patch[p + n] == patch[p])
                core[patch[p]] = 1;
        }
    for (int i = 0; i < N; i++) mask[i] = !core[patch[i]];
    for (int left = 1; left;) {
        memcpy(next, f, (size_t)N);
        left = 0;
        for (int y = 0; y < n; y++)
            for (int x = 0; x < n; x++) {
                int sx = x < n / 2 ? 1 : -1, sy = y < n / 2 ? 1 : -1;
                const int nb[4][2] = {{sx, 0}, {-sx, 0}, {0, sy}, {0, -sy}};
                for (int k = 0; mask[y * n + x] && k < 4; k++) {
                    int ax = x + nb[k][0], ay = y + nb[k][1];
                    if (ax < 0 || ay < 0 || ax >= n || ay >= n || mask[ay * n + ax]) continue;
                    next[y * n + x] = f[ay * n + ax];
                    mask[y * n + x] = 2;
                    left = 1;
                }
            }
        for (int i = 0; i < N; i++)
            if (mask[i] == 2) mask[i] = 0;
        memcpy(f, next, (size_t)N);
    }
    free(patch);
    free(core);
    free(mask);
    free(next);
}

/* d (n x n): the squared distance from each point to the nearest one
 * where src is 0 (Felzenszwalb and Huttenlocher's, a line at a time) */
static void distance_sq(int *d, const uint8_t *src, int n)
{
    int *v = malloc(sizeof(int) * n);
    float *g = malloc(sizeof(float) * n), *z = malloc(sizeof(float) * (n + 1));
    const float far = 1e7f;
    for (int pass = 0; pass < 2; pass++)
        for (int l = 0; l < n; l++) {
            /* down each column, then across each row */
            int o = pass ? l * n : l, s = pass ? 1 : n, k = 0;
            for (int i = 0; i < n; i++) g[i] = pass ? (float)d[o + i * s] : src[o + i * s] ? far : 0.0f;
            v[0] = 0;
            z[0] = -1e30f;
            z[1] = 1e30f;
            for (int q = 1; q < n; q++) {
                float x;
                for (;;) {
                    x = (g[q] + (float)(q * q) - g[v[k]] - (float)(v[k] * v[k])) / (2.0f * (float)(q - v[k]));
                    if (x > z[k]) break;
                    k--;
                }
                k++;
                v[k] = q;
                z[k] = x;
                z[k + 1] = 1e30f;
            }
            k = 0;
            for (int q = 0; q < n; q++) {
                while (z[k + 1] < (float)q) k++;
                float e = (float)((q - v[k]) * (q - v[k])) + g[v[k]];
                d[o + q * s] = e > far ? (int)far : (int)e;
            }
        }
    free(v);
    free(g);
    free(z);
}

/*
 * f (n x n points) with every dark stroke a pixel or more across: round
 * each point along the middle of one thinner (no point next to it further
 * from the stroke's edges; the shortest way across it, along a row, a
 * column or a diagonal, under a pixel), a disc of dark a pixel and a
 * point or two across.
 * Along a stroke's middle the points are all as far in, so the discs go
 * all along it; towards the tip of a wedge or a corner they're nearer the
 * edges, so those aren't blunted or drawn out (where a short stroke
 * between two corners has no such points, separate keeps what's either
 * side of it apart). The edges the fitting put on pixels have their bands a whole
 * pixel across already.
 */
static void widen(uint8_t *f, int n)
{
    const int N = n * n, R = VS / 2;
    static const int dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    int *d = malloc(sizeof(int) * N);
    uint8_t *m = calloc((size_t)N, 1), *add = calloc((size_t)N, 1);
    for (int i = 0; i < N; i++) m[i] = dark_part(f[i]);
    distance_sq(d, m, n);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            /* (not where it's a point across: the end of a wedge, which
             * a disc would carry on past its tip, a stub out of a corner) */
            int p = y * n + x, middle = m[p] && d[p] >= 2;
            for (int j = -1; j <= 1 && middle; j++)
                for (int i = -1; i <= 1 && middle; i++) {
                    int ax = x + i, ay = y + j;
                    middle = ax < 0 || ay < 0 || ax >= n || ay >= n || d[ay * n + ax] <= d[p];
                }
            if (!middle) continue;
            float across = (float)N;
            for (int k = 0; k < 4; k++) {
                int run = 1;
                for (int s = -1; s <= 1; s += 2)
                    for (int t = 1;; t++) {
                        int ax = x + s * t * dir[k][0], ay = y + s * t * dir[k][1];
                        if (ax < 0 || ay < 0 || ax >= n || ay >= n || !m[ay * n + ax]) break;
                        run++;
                    }
                /* (a diagonal's points sqrt 2 apart, its run from about
                 * half a step before the first to half after the last; a
                 * stroke along one a pixel and a point or more across, the
                 * points as squares a pixel apart, as separate wants) */
                float len = k < 2 ? (float)run : ((float)run - 0.5f) * 1.4142f;
                if (len < across) across = len;
            }
            if (across >= (float)VS) continue;
            for (int j = -R; j <= R; j++)
                for (int i = -R; i <= R; i++) {
                    int ax = x + i, ay = y + j;
                    if (i * i + j * j <= R * R && ax >= 0 && ay >= 0 && ax < n && ay < n) add[ay * n + ax] = 1;
                }
        }
    for (int i = 0; i < N; i++)
        if (add[i] && !m[i]) make_dark(f, i);
    free(d);
    free(m);
    free(add);
}

/* pid[] (n x n): each point's patch of the lighter parts of f (and of
 * clear; patches), numbered from 0 (dark: -1), area[] each one's points;
 * returns how many, *meet (for free) a flag each two that meet: along a
 * pixel or more, and of parts that meet in the PC's picture (touch[], a
 * flag each two parts as fit_plain sees them; where two touch in a few
 * points, or two the PC keeps apart touch, a line between them is
 * broken) */
static int light_patches(const uint8_t *f, int n, const uint8_t *touch, int *pid, int *area, uint8_t **meet)
{
    const int N = n * n;
    int *patch = malloc(sizeof(int) * N), *part = malloc(sizeof(int) * N), np = 0;
    patches(patch, f, n);
    for (int i = 0; i < N; i++) pid[i] = -1;
    for (int i = 0; i < N; i++)
        if (!dark_part(f[i]) && patch[i] == i) pid[i] = np++;
    for (int i = 0; i < N; i++) pid[i] = dark_part(f[i]) ? -1 : pid[patch[i]];
    int *meets = calloc((size_t)np * np, sizeof(int));
    memset(area, 0, sizeof(int) * np);
    for (int i = 0; i < N; i++) {
        if (pid[i] < 0) continue;
        area[pid[i]]++;
        if (i % n + 1 < n && pid[i + 1] >= 0) meets[pid[i] * np + pid[i + 1]]++;
        if (i + n < N && pid[i + n] >= 0) meets[pid[i] * np + pid[i + n]]++;
    }
    *meet = calloc((size_t)np * np, 1);
    for (int i = 0; i < N; i++)
        if (pid[i] >= 0) part[pid[i]] = fit_plain(f[i]) & 15;
    for (int i = 0; i < np * np; i++)
        (*meet)[i] = meets[i] + meets[(i % np) * np + i / np] >= VS && touch[part[i / np] * 16 + part[i % np]];
    free(meets);
    free(patch);
    free(part);
    return np;
}

/* touch[] (16 x 16): a flag each two parts of f (n x n points, as
 * fit_plain sees them) that meet along a pixel or more */
static void touching(uint8_t *touch, const uint8_t *f, int n)
{
    int count[256] = {0};
    for (int i = 0; i < n * n; i++) {
        int a = fit_plain(f[i]) & 15;
        if (i % n + 1 < n) count[a * 16 + (fit_plain(f[i + 1]) & 15)]++;
        if (i + n < n * n) count[a * 16 + (fit_plain(f[i + n]) & 15)]++;
    }
    for (int i = 0; i < 256; i++) touch[i] = count[i] + count[(i % 16) * 16 + i / 16] >= VS;
}

/*
 * f (n x n points) with every two lighter patches that don't meet a pixel
 * or more apart: where a line between them is thinner (at a corner, at the
 * tip of a wedge, where widen's discs don't reach), the smaller one's
 * points nearer the other are made dark. Two pixels' middles next to each
 * other are a pixel apart, so two patches that far apart (the points as
 * squares) can't show next to each other at any angle.
 */
static void separate(uint8_t *f, int n, const uint8_t *touch)
{
    const int N = n * n;
    int *pid = malloc(sizeof(int) * N), *d = malloc(sizeof(int) * N), *area = malloc(sizeof(int) * N);
    uint8_t *m = malloc((size_t)N), *meet;
    int np = light_patches(f, n, touch, pid, area, &meet);
    for (int a = 0; a < np; a++) {
        /* (as squares a point across: from the patch grown a point each
         * way, round the corners too) */
        for (int i = 0; i < N; i++) {
            int x = i % n, y = i / n, in = 0;
            for (int j = -1; j <= 1 && !in; j++)
                for (int k = -1; k <= 1 && !in; k++)
                    in = x + k >= 0 && y + j >= 0 && x + k < n && y + j < n && pid[(y + j) * n + x + k] == a;
            m[i] = !in;
        }
        distance_sq(d, m, n);
        for (int i = 0; i < N; i++) {
            int b = dark_part(f[i]) ? -1 : pid[i];
            if (b < 0 || b == a || meet[a * np + b] || d[i] >= VS * VS) continue;
            if (area[b] > area[a] || (area[b] == area[a] && b > a)) continue;
            make_dark(f, i);
        }
    }
    free(pid);
    free(d);
    free(area);
    free(m);
    free(meet);
}

/*
 * f (n x n points), the upright design, with every two lighter patches
 * that don't meet more than a pixel apart along each row and column: a
 * dark run between them (or none) shorter than a pixel made that long,
 * half each way. Upright, two pixels' middles next to each other are a
 * pixel apart along a row or a column, so they can't fall either side of
 * one, and a slanted line comes out a pixel to a row or a column, as
 * drawn by hand.
 */
static void upright_lines(uint8_t *f, int n, const uint8_t *touch)
{
    const int N = n * n;
    int *pid = malloc(sizeof(int) * N), *area = malloc(sizeof(int) * N);
    uint8_t *meet;
    int np = light_patches(f, n, touch, pid, area, &meet);
    for (int down = 0; down < 2; down++)
        for (int l = 0; l < n; l++)
            for (int j = 1; j < n;) {
                /* (point j of line l, the one before it light, a dark run
                 * from it to k (none, if j is light), light after) */
#define AT(j) ((down) ? (j) * n + l : l * n + (j))
                if (dark_part(f[AT(j - 1)])) {
                    j++;
                    continue;
                }
                int k = j;
                while (k < n && dark_part(f[AT(k)])) k++;
                int a = pid[AT(j - 1)], b = k < n ? pid[AT(k)] : -1, need = VS - (k - j);
                if (b >= 0 && a != b && !meet[a * np + b] && need > 0)
                    for (int s = 0; s < need; s++) {
                        int q = s % 2 ? k + s / 2 : j - 1 - s / 2;
                        if (q >= 0 && q < n && !dark_part(f[AT(q)])) make_dark(f, AT(q));
                    }
#undef AT
                j = k > j ? k + 1 : j + 1;
            }
    free(pid);
    free(area);
    free(meet);
}

/* (what a line's pixel weighs against a colour's: a cube's lines are
 * kept before its colours) */
#define FIT_LINE 2.0f

/* fit_warp's picture of the vehicle (VehArg) without its rider: what
 * shows of it cleared (left glass under the dome). Down each of the
 * rider's columns it shows as far as the first point of the rest over it
 * (one that isn't the rider's; under the glass, the first past it), but
 * for the points just above that of the same part (the rest's: the
 * hull's outline where the rider's own is behind it). Found with the
 * first icon's rider, so that it's the same for every icon. */
static void draw_rest(uint8_t *f, int n, const void *arg, const FitChain *c)
{
    const VehArg *a = arg;
    VehArg first = {a->mode, 0, 0.0f, a->block};
    RiderArg r = {a->mode, 0, 0, a->block};
    float x, y;
    fine_parts(f, n / VS, draw_vehicle, a, c);
    if (rider_at(a->mode, &x, &y) > 0.0f) {
        uint8_t *rid = malloc((size_t)n * n), *f0 = malloc((size_t)n * n);
        fine_parts(rid, n / VS, draw_rider, &r, c);
        if (a->icon) fine_parts(f0, n / VS, draw_vehicle, &first, c);
        else memcpy(f0, f, (size_t)n * n);
        for (int i = 0; i < n; i++) {
            int j = 0, top, glass = 0;
            while (j < n && !rid[j * n + i]) j++;
            for (top = j; j < n && rid[j * n + i]; j++) {
                uint8_t q = f0[j * n + i];
                if (q == PC_DOME || q >= PC_K_DOME) glass = 1;
                else if (q != rid[j * n + i] || glass) break;
            }
            for (uint8_t q = j < n ? f0[j * n + i] : 0; j > top && f0[(j - 1) * n + i] == q; j--) {}
            for (int k = top; k < j; k++) {
                f[k * n + i] = f[k * n + i] == PC_DOME || f[k * n + i] >= PC_K_DOME ? PC_DOME : 0;
                rid[k * n + i] = 255;
            }
        }
        /* (down a side of the rider its outline and the hull's are one:
         * a gap so cut in the hull's, under a pixel across, is closed) */
        for (int y = 0; y < n; y++)
            for (int i = 1, e; i < n; i = e + 1) {
                for (e = i; e < n && rid[y * n + e] == 255 && !f[y * n + e]; e++) {}
                if (e > i && e < n && e - i < VS && f[y * n + i - 1] == f[y * n + e] && !open_part(f[y * n + e]))
                    memset(f + y * n + i, f[y * n + e], (size_t)(e - i));
            }
        free(rid);
        free(f0);
    }
    symmetric(f, n);
}

/*
 * cube (m * VS points square): the rider's design, from a picture of its
 * own (r, at its middle). A cube five to seven pixels across has too few
 * for its lines and colours each a pixel or more, so the inside of its
 * outline is fitted to whole pixels (the same each way, as near the PC's
 * size as it can be, its middle the picture's) and brought down as the
 * PC's picture would be, each pixel the part most of its points are (dark
 * if as many), with a pixel of outline round it.
 */
static void rider_design(uint8_t *cube, int m, const RiderArg *r)
{
    const int n = m * VS;
    uint8_t *f = malloc((size_t)n * n);
    FitChain *c = malloc(sizeof(FitChain));
    int lo = n, hi = 0, t = 0;
    fine_parts(f, m, draw_rider, r, NULL);
    for (int i = 0; i < n * n; i++)
        if (f[i]) {
            lo = i % n < lo ? i % n : lo;
            hi = i % n + 1 > hi ? i % n + 1 : hi;
        }
    /* (the same each side of the middle, where it is drawn) */
    lo = lo < n - hi ? lo : n - hi;
    hi = n - lo;
    while (lo + t < n / 2 && dark_part(f[(n / 2) * n + lo + t]) && dark_part(f[(n / 2) * n + hi - 1 - t])) t++;
    /* (the inside's width, in pixels: the cube's less the outline, as
     * even or odd as the picture) */
    float in = (float)(hi - lo - 2 * t) / VS;
    int w = (int)((float)(hi - lo) / VS + 0.5f) - 2;
    if ((w + 2 - m) % 2) w += in > (float)w ? 1 : -1;
    float a = (float)((m - w) * VS) * 0.5f;
    c->n = 1;
    for (int k = 0; k < 2; k++) {
        FitWarp *s = &c->step[0][k];
        float src[4] = {0.0f, (float)(lo + t), (float)(hi - t), (float)n}, dst[4] = {0.0f, a, a + (float)(w * VS), (float)n};
        s->n = 4;
        memcpy(s->src, src, sizeof(src));
        memcpy(s->dst, dst, sizeof(dst));
    }
    fine_parts(f, m, draw_rider, r, c);
    symmetric(f, n);
    memset(cube, 0, (size_t)n * n);
    int p0 = (m - w) / 2 - 1, p1 = (m + w) / 2 + 1;
    for (int py = p0; py < p1; py++)
        for (int px = p0; px < p1; px++) {
            int count[16] = {0}, best = PC_K;
            for (int j = 0; j < VS; j++)
                for (int i = 0; i < VS; i++) count[f[(py * VS + j) * n + px * VS + i] & 15]++;
            for (int k = 1; k < 16; k++)
                if (count[k] > count[best]) best = k;
            if (px == p0 || py == p0 || px == p1 - 1 || py == p1 - 1) best = PC_K;
            for (int j = 0; j < VS; j++)
                for (int i = 0; i < VS; i++) cube[(py * VS + j) * n + px * VS + i] = (uint8_t)best;
        }
    free(f);
    free(c);
}

/* f (n x n points), made from was (widen, separate) by making points
 * dark: those of a picture the same each side of its middle, across or
 * down, made dark each side (the points as drawn round either way the
 * same way: the same each side again) */
static void mirror_dark(uint8_t *f, const uint8_t *was, int n)
{
    for (int down = 0; down < 2; down++) {
        int same = 1;
        for (int i = 0; i < n * n && same; i++)
            same = was[i] == was[down ? (n - 1 - i / n) * n + i % n : i - i % n + n - 1 - i % n];
        for (int i = 0; i < n * n && same; i++) {
            int j = down ? (n - 1 - i / n) * n + i % n : i - i % n + n - 1 - i % n;
            if (dark_part(f[j]) && !dark_part(f[i])) make_dark(f, i);
        }
    }
}

/*
 * des[0] (size * VS points square): the vehicle's design, its picture
 * upright fitted to the pixels (fit_warp), its lines a pixel long along
 * each row and column (upright_lines); des[1] the same with its edges
 * nearer their places (FitChain's turned) and its lines a pixel wide every
 * way (widen, separate), for it turned. The cube riding in the ship and
 * the UFO is designed on its own (rider_design: with the rest, its lines a
 * pixel each would squeeze the hull and the dome in its rows and
 * columns), in a picture of its own with its middle that picture's (a
 * pixel's middle if the picture is an odd number of pixels across: as
 * near the PC's size as it can be, but in the UFO, on the UFO's middle),
 * and put back under the rest, as the PC draws it, its middle across and
 * its top on the pixels nearest the PC's.
 */
static void vehicle_design(uint8_t *const des[2], int mode, int icon, int size, float block)
{
    const int n = size * VS;
    VehArg a = {mode, icon, 0.0f, block};
    RiderArg r = {mode, icon, 1, block};
    float rx, ry, rs = rider_at(mode, &rx, &ry), px = rs * block * K;
    int m = (int)(px + 0.5f) + 2, mn;
    if (rx == 0.0f && (m - size) % 2) m++;
    mn = m * VS;
    FitChain *c = malloc(sizeof(FitChain));
    uint8_t *cube = malloc((size_t)(n > mn ? n : mn) * (n > mn ? n : mn)), *rid = malloc((size_t)mn * mn), touch[256];
    fine_parts(des[0], size, draw_vehicle, &a, NULL);
    touching(touch, des[0], n);
    fit_warp(c, draw_rest, &a, n, FIT_LINE);
    if (rs > 0.0f) rider_design(rid, m, &r);
    for (int t = 0; t < 2; t++) {
        uint8_t *up = des[t];
        for (int k = 0; k < 2 && t; k++) c->step[c->n - 1][k] = c->turned[k];
        draw_rest(up, n, &a, c);
        if (rs > 0.0f) {
            int top = mn;
            for (int i = 0; i < mn * mn && top == mn; i++)
                if (rid[i]) top = i / mn / VS;
            int ox = (int)floorf(size * 0.5f + rx * block * K - m * 0.5f + 0.5f);
            int oy = (int)floorf(size * 0.5f + ry * block * K - px * 0.5f + 0.5f) - top;
            for (int y = 0; y < mn; y++)
                for (int x = 0; x < mn; x++) {
                    int X = ox * VS + x, Y = oy * VS + y;
                    uint8_t p = rid[y * mn + x];
                    if (!p || X < 0 || Y < 0 || X >= n || Y >= n) continue;
                    if (!up[Y * n + X]) up[Y * n + X] = p;
                    else if (up[Y * n + X] == PC_DOME) up[Y * n + X] = under_dome(p);
                }
        }
        tidy(up, n);
        memcpy(cube, up, (size_t)n * n);
        if (t) {
            widen(up, n);
            separate(up, n, touch);
        } else
            upright_lines(up, n, touch);
        mirror_dark(up, cube, n);
    }
    free(c);
    free(cube);
    free(rid);
}

/* idx (size x size): the design des (size * VS points square) turned by
 * angle round its middle, each pixel the part at its middle (where that's
 * on the edge between points, as it is upright, the point of the two
 * nearer the picture's middle: the same each side of it, and a run of VS
 * points has one pixel's middle) */
static void turn_design(uint8_t *idx, const uint8_t *des, int size, float angle)
{
    const int n = size * VS;
    float q = floorf(angle / (PI * 0.5f) + 0.5f), co = cosf(angle), si = sinf(angle), c = size * 0.5f;
    if (fabsf(angle - q * (PI * 0.5f)) < 0.0001f) {
        int k = ((int)q % 4 + 4) % 4;
        co = k == 0 ? 1.0f : k == 2 ? -1.0f : 0.0f;
        si = k == 1 ? 1.0f : k == 3 ? -1.0f : 0.0f;
    }
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float px = x + 0.5f - c, py = y + 0.5f - c;
            float fx = (px * co + py * si + c) * VS, fy = (py * co - px * si + c) * VS;
            int sx = (int)floorf(fx), sy = (int)floorf(fy);
            sx -= fx == (float)sx && sx > n / 2;
            sy -= fy == (float)sy && sy > n / 2;
            idx[y * size + x] = sx >= 0 && sy >= 0 && sx < n && sy < n ? des[sy * n + sx] : 0;
        }
}

/* the vehicle at an angle, in size x size pixels, block pixels (the PC's)
 * a block: its design turned (upright or a quarter turn, the design as
 * fitted); the ball as ball_idx draws it */
static void vehicle_idx(uint8_t *idx, int mode, int icon, int size, float angle, float block)
{
    static int up_mode = -1, up_icon, up_size;
    static float up_block;
    static uint8_t *des[2];
    if (mode == MODE_BALL) {
        ball_idx(idx, size, block == BLOCK_PX ? 6 : 7, block * K, icon, angle);
        return;
    }
    if (mode != up_mode || icon != up_icon || size != up_size || block != up_block) {
        for (int t = 0; t < 2; t++) des[t] = realloc(des[t], (size_t)size * VS * size * VS);
        vehicle_design(des, mode, icon, size, block);
        up_mode = mode;
        up_icon = icon;
        up_size = size;
        up_block = block;
    }
    float q = angle / (PI * 0.5f);
    turn_design(idx, des[fabsf(q - floorf(q + 0.5f)) > 0.001f], size, angle);
}

/* out (size x size): in turned a quarter turn clockwise */
static void quarter_turn(uint8_t *out, const uint8_t *in, int size)
{
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) out[x * size + size - 1 - y] = in[y * size + x];
}

/* the vehicle at an angle, as tiles */
static void vehicle_tiles_at(uint32_t *out, int mode, int icon, int size, float angle, float block)
{
    uint8_t *idx = malloc((size_t)size * size);
    vehicle_idx(idx, mode, icon, size, angle, block);
    idx_tiles(out, idx, size, size);
    free(idx);
}

static void vehicle_tiles(uint32_t *out, int mode, int icon, int size, float angle)
{
    vehicle_tiles_at(out, mode, icon, size, angle, BLOCK_PX);
}

/* (the garage's pictures are in the vehicles' order) */
typedef char gf_still_is_every_mode[(int)GF_STILL == (int)MODE_COUNT ? 1 : -1];

static void emit_player(GbaGen *g)
{
    static uint32_t t[ICON_COUNT][PLAYER_TILES][8];
    for (int i = 0; i < ICON_COUNT; i++) {
        vehicle_tiles(t[i][PT_CUBE], MODE_CUBE, i, 16, 0.0f);
        vehicle_tiles(t[i][PT_SHIP], MODE_SHIP, i, 32, 0.0f);
        vehicle_tiles(t[i][PT_BALL], MODE_BALL, i, 16, 0.0f);
        vehicle_tiles(t[i][PT_UFO], MODE_UFO, i, 32, 0.0f);
        vehicle_tiles(t[i][PT_WAVE], MODE_WAVE, i, 16, 0.0f);
    }
    gba_gen_blob(g, "g_player_tiles", "uint32_t", t, sizeof(t));
    {
        /* the vehicles turned (art.h: VF_*) */
        static uint32_t f[ICON_COUNT][VF_TILES][8];
        for (int i = 0; i < ICON_COUNT; i++) {
            /* (a quarter turn on, the cube is the same picture turned: it
             * looks the same whichever way up it lands) */
            static uint8_t cube[VF_CUBE][16 * 16];
            for (int k = 0; k < VF_CUBE; k++) {
                if (k < VF_CUBE / 4) vehicle_idx(cube[k], MODE_CUBE, i, 16, k * 2.0f * PI / VF_CUBE, BLOCK_PX);
                else quarter_turn(cube[k], cube[k - VF_CUBE / 4], 16);
                idx_tiles(f[i][VFT_CUBE + k * 4], cube[k], 16, 16);
            }
            for (int k = 0; k < VF_BALL; k++)
                vehicle_tiles(f[i][VFT_BALL + k * 4], MODE_BALL, i, 16, k * 2.0f * PI / VF_BALL);
            for (int k = 0; k < VF_SHIP; k++)
                vehicle_tiles(f[i][VFT_SHIP + k * 16], MODE_SHIP, i, 32, -PI / 2 + k * PI / (VF_SHIP - 1));
            for (int k = 0; k < VF_UFO; k++)
                vehicle_tiles(f[i][VFT_UFO + k * 16], MODE_UFO, i, 32,
                              -VF_UFO_TILT + k * 2.0f * VF_UFO_TILT / (VF_UFO - 1));
            for (int k = 0; k < VF_WAVE; k++)
                vehicle_tiles(f[i][VFT_WAVE + k * 4], MODE_WAVE, i, 16, -PI / 2 + k * PI / (VF_WAVE - 1));
        }
        gba_gen_blob(g, "g_player_frames", "uint32_t", f, sizeof(f));
    }
    {
        /* the garage's, bigger (art.h: GF_*) */
        static uint32_t f[ICON_COUNT][GF_PICS][16][8];
        for (int i = 0; i < ICON_COUNT; i++) {
            for (int m = 0; m < GF_STILL; m++)
                vehicle_tiles_at(f[i][m][0], m, i, 32, m == MODE_WAVE ? GARAGE_WAVE_ANGLE : 0.0f, GARAGE_BLOCK);
            for (int k = 0; k < GF_BALL; k++)
                vehicle_tiles_at(f[i][GF_STILL + k][0], MODE_BALL, i, 32, k * 2.0f * PI / GF_BALL, GARAGE_BLOCK);
        }
        gba_gen_blob(g, "g_garage_frames", "uint32_t", f, sizeof(f));
    }
    /* for the sheet */
    for (int i = 0; i < 2; i++) {
        int first = s_ntiles;
        for (int k = 0; k < PLAYER_TILES; k++) {
            if (s_ntiles == s_tcap) {
                s_tcap = s_tcap * 2;
                s_tiles = realloc(s_tiles, (size_t)s_tcap * 32);
            }
            memcpy(s_tiles + s_ntiles * 8, t[i * 4][k], 32);
            s_ntiles++;
        }
        static const int pw[5] = {16, 32, 16, 32, 16}, po[5] = {PT_CUBE, PT_SHIP, PT_BALL, PT_UFO, PT_WAVE};
        for (int m = 0; m < 5 && s_nsheet < 256; m++)
            s_sheet[s_nsheet++] = (SheetSpr){first + po[m], pw[m], pw[m], OBJ_PAL_PLAYER, 1, NULL};
    }
    {
        /* a sample garage palette for the sheet: yellow and cyan */
        Color c1 = g_player_colors[0], c2 = g_player_colors[1], k = RGB(8, 8, 12), dome = RGB(200, 240, 255);
        Color cs[9] = {0, k, c1, col_scale(c1, 1.25f), c2, col_with_alpha(dome, 1.0f), col_lerp(k, dome, 0.35f),
                       col_lerp(c1, dome, 0.35f), col_lerp(col_scale(c1, 1.25f), dome, 0.35f)};
        int p[16][3];
        memset(p, 0, sizeof(p));
        for (int i = 1; i < 9; i++) {
            p[i][0] = (int)COL_R(cs[i]);
            p[i][1] = (int)COL_G(cs[i]);
            p[i][2] = (int)COL_B(cs[i]);
        }
        p[PC_C2_DOME][0] = (int)COL_R(col_lerp(c2, dome, 0.35f));
        p[PC_C2_DOME][1] = (int)COL_G(col_lerp(c2, dome, 0.35f));
        p[PC_C2_DOME][2] = (int)COL_B(col_lerp(c2, dome, 0.35f));
        set_pal(g, OBJ_PAL_PLAYER, NULL, p, 9);
        s_pals[OBJ_PAL_PLAYER][0] = 0;
    }
}

/* ------------------------------------------------------------------ */
/* The menus' sets: pictures loaded when a screen opens, into OBJ tiles   */
/* from OBJ_TEXT_TILE (ST_* in gen.h), with palettes of their own        */
/* ------------------------------------------------------------------ */

static int s_set_first, s_set_sheet;
static uint16_t s_set_pals[16][16];
static uint16_t s_sheet_pals[8][16][16]; /* the sets' palettes, for the sheet */
static int s_nsheet_pals;

static void set_begin(void)
{
    s_set_first = s_ntiles;
    s_set_sheet = s_nsheet;
    s_base = OBJ_TEXT_TILE - s_ntiles;
    s_prefix = "ST_";
    memcpy(s_set_pals, s_pals, sizeof(s_pals));
}

/* write the set's tiles and its palettes (the banks given), and restore */
static void set_end(GbaGen *g, const char *name, int bank0, int nbank)
{
    char sym[64];
    int n = s_ntiles - s_set_first;
    snprintf(sym, sizeof(sym), "g_set_%s", name);
    gba_gen_blob(g, sym, "uint32_t", s_tiles + s_set_first * 8, (size_t)n * 32);
    snprintf(sym, sizeof(sym), "g_set_%s_pal", name);
    gba_gen_blob(g, sym, "uint16_t", s_pals[bank0], (size_t)nbank * 32);
    fprintf(g->hdr, "#define SET_%s_TILES %d\n#define SET_%s_BANK %d\n#define SET_%s_BANKS %d\n", name, n, name, bank0,
            name, nbank);
    /* (the tiles stay in s_tiles for the sheet, after the static ones) */
    if (s_nsheet_pals < 8) {
        memcpy(s_sheet_pals[s_nsheet_pals], s_pals, sizeof(s_pals));
        for (int i = s_set_sheet; i < s_nsheet; i++) s_sheet[i].pals = (const uint16_t (*)[16])s_sheet_pals[s_nsheet_pals];
        s_nsheet_pals++;
    }
    memcpy(s_pals, s_set_pals, sizeof(s_pals));
    s_base = OBJ_STATIC_TILE;
    s_prefix = "OT_";
}

typedef struct {
    float size;
    int sel, kind;
    Color c;
} ButtonArg;

static void draw_title_button(const void *a)
{
    const ButtonArg *b = a;
    g_game.t = 0.0f; /* the selected size's pulse at its middle */
    draw_button(0.0f, 0.0f, b->size, b->sel, b->c, b->kind);
}

static void draw_logo(const void *a)
{
    (void)a;
    /* title_render's logo, without its see-through shadow */
    font_draw_fancy(0.0f, -32.0f, 9.0f, RGB(255, 250, 200), RGB(255, 170, 40), RGB(20, 10, 0), 4.0f, ALIGN_CENTER,
                    GAME_TITLE);
}

static void draw_subtitle(const void *a)
{
    (void)a;
    /* title_render's line under the logo, for this platform */
    font_draw(0.0f, -8.0f, 2.0f, col_with_alpha(COL_WHITE, 0.85f), ALIGN_CENTER, "A RHYTHM PLATFORMER FOR GBA");
}

/* the options button's cog: render_saw's, 0.3 of the button (pix_saw) */
#define OPTIONS_SAW_R (74.0f * 0.3f)

static void quant1(Group *gr)
{
    gr->nvar = 1;
    gr->ncol = 15;
    group_quantize(gr);
}

static void emit_title_set(GbaGen *g)
{
    /* title_render's buttons: garage, play, options (draw_button) */
    static const Color cols[3] = {RGB(60, 190, 255), RGB(70, 220, 90), RGB(255, 150, 50)};
    set_begin();
    {
        Group gr = {0};
        gr.pic[0][0] = render(192, 32, draw_logo, NULL, 0);
        gr.pic[1][0] = render(192, 8, draw_subtitle, NULL, 0);
        gr.npic = 2;
        quant1(&gr);
        s_cur_bank0 = OBJ_PAL_TEXT;
        s_cur_nbank = 1;
        for (int k = 0; k < 3; k++) add_tiles(g, k ? NULL : "LOGO", gr.idx[0], 192, 32, k * 64, 0, 64, 32);
        for (int k = 0; k < 3; k++) add_tiles(g, k ? NULL : "SUBTITLE", gr.idx[1], 192, 8, k * 64, 0, 64, 8);
        set_pal(g, OBJ_PAL_TEXT, NULL, gr.pal[0], gr.ncol);
        group_free(&gr);
    }
    for (int b = 0; b < 3; b++) {
        Group gr = {0};
        int sz = b == 1 ? 64 : 32;
        for (int sel = 0; sel < 2; sel++) {
            ButtonArg a = {b == 1 ? 100.0f : 74.0f, sel, b == 1 ? 1 : -1, cols[b]};
            gr.pic[gr.npic++][0] = render(sz, sz, draw_title_button, &a, 0);
        }
        if (b == 2)
            for (int f = 0; f < COG_FRAMES; f++) {
                /* (turned through a tooth's 30 degrees: art.h) */
                float angle = f * (PI / 6.0f) / COG_FRAMES;
                gr.pic[gr.npic++][0] = pix_saw(16, OPTIONS_SAW_R, angle, RGB(30, 30, 40), COL_WHITE, 3.5f, 1.2f);
            }
        quant1(&gr);
        s_cur_bank0 = OBJ_PAL_PORTAL + b;
        s_cur_nbank = 1;
        static const char *names[3][2] = {{"BTN_GARAGE", "BTN_GARAGE_SEL"}, {"BTN_PLAY", "BTN_PLAY_SEL"},
                                          {"BTN_OPTIONS", "BTN_OPTIONS_SEL"}};
        for (int sel = 0; sel < 2; sel++) add_tiles(g, names[b][sel], gr.idx[sel], sz, sz, 0, 0, sz, sz);
        if (b == 2) {
            static uint32_t frames[COG_FRAMES * 4][8];
            add_tiles(g, "BTN_SAW", gr.idx[2], 16, 16, 0, 0, 16, 16);
            for (int f = 0; f < COG_FRAMES; f++) idx_tiles(frames[f * 4], gr.idx[2 + f], 16, 16);
            gba_gen_blob(g, "g_cog_frames", "uint32_t", frames, sizeof(frames));
        }
        set_pal(g, OBJ_PAL_PORTAL + b, NULL, gr.pal[0], gr.ncol);
        group_free(&gr);
    }
    set_end(g, "title", OBJ_PAL_TEXT, OBJ_PAL_PORTAL + 3 - OBJ_PAL_TEXT);
}

typedef struct {
    int d;
} FaceArg;

static void draw_face(const void *a)
{
    draw_diff_badge(0.0f, 0.0f, ((const FaceArg *)a)->d, 38.0f);
}

static void draw_arrow(const void *a)
{
    /* select_render's arrows */
    font_draw_fancy(0.0f, -16.0f, 5.0f, COL_WHITE, RGB(200, 210, 230), RGB(0, 0, 0), 3.0f, ALIGN_CENTER,
                    *(const int *)a ? GLYPH_RIGHT : GLYPH_LEFT);
}

/* draw_level_card's empty coin at 16 x 16: see-through dark (14 of the
 * PC's pixels round), a faint ring (its outer 3, here a pixel) */
static Img pix_coin_slot(void)
{
    Img im = pix_new(16, 16);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            float d = pix_dist(x, y, 8.0f, 8.0f);
            if (fabsf(d - 12.5f * K) <= 0.5f) pix_put(&im, x, y, RGB(110, 110, 120));
            else if (d <= 14.0f * K) pix_put(&im, x, y, RGB(0, 0, 0));
        }
    return im;
}

static void emit_select_set(GbaGen *g)
{
    set_begin();
    for (int d = 0; d < 6; d++) {
        Group gr = {0};
        FaceArg a = {d};
        gr.pic[0][0] = render_crisp(32, 32, draw_face, &a, 128);
        gr.npic = 1;
        quant1(&gr);
        s_cur_bank0 = OBJ_PAL_PORTAL + d;
        s_cur_nbank = 1;
        add_tiles(g, d ? NULL : "FACE", gr.idx[0], 32, 32, 0, 0, 32, 32);
        set_pal(g, OBJ_PAL_PORTAL + d, NULL, gr.pal[0], gr.ncol);
        group_free(&gr);
    }
    {
        Group gr = {0};
        int l = 0, r = 1;
        gr.pic[0][0] = render(16, 16, draw_arrow, &l, 0);
        gr.pic[1][0] = render(16, 16, draw_arrow, &r, 0);
        gr.pic[2][0] = pix_coin_slot();
        gr.npic = 3;
        quant1(&gr);
        s_cur_bank0 = OBJ_PAL_PORTAL + 6;
        s_cur_nbank = 1;
        add_tiles(g, "ARROW_L", gr.idx[0], 16, 16, 0, 0, 16, 16);
        add_tiles(g, "ARROW_R", gr.idx[1], 16, 16, 0, 0, 16, 16);
        add_tiles(g, "COIN_SLOT", gr.idx[2], 16, 16, 0, 0, 16, 16);
        set_pal(g, OBJ_PAL_PORTAL + 6, NULL, gr.pal[0], gr.ncol);
        group_free(&gr);
    }
    set_end(g, "select", OBJ_PAL_PORTAL, 7);
}

/* ------------------------------------------------------------------ */

int gba_art_obj_export(GbaGen *g)
{
    s_ntiles = 0;
    s_nsheet = 0;
    memset(s_pals, 0, sizeof(s_pals));
    emit_orbs(g);
    emit_portals(g);
    emit_speed(g);
    emit_misc(g);
    emit_fx(g);
    emit_glows(g);
    {
        int keep = s_ntiles, keep_sheet = s_nsheet;
        gba_gen_blob(g, "g_obj_tiles", "uint32_t", s_tiles, (size_t)s_ntiles * 32);
        emit_player(g);
        s_ntiles = keep;
        s_nsheet = keep_sheet;
        emit_title_set(g);
        emit_select_set(g);
        s_ntiles = keep;
        s_nsheet = keep_sheet;
    }
    gba_gen_blob(g, "g_obj_pals", "uint16_t", s_pals, sizeof(s_pals));
    fprintf(g->hdr, "#define OBJ_STATIC_COUNT %d\n", s_ntiles);
    return 0;
}

/* gba_tool objsheet <out.png>: the sprites in their palettes, magnified. */
int gba_art_obj_sheet(const char *out)
{
    GbaGen g;
    memset(&g, 0, sizeof(g));
    g.hdr = fopen("/dev/null", "w");
    g.data_s = g.hdr;
    g.src = g.hdr;
    s_ntiles = 0;
    s_nsheet = 0;
    s_nsheet_pals = 0;
    memset(s_pals, 0, sizeof(s_pals));
    emit_orbs(&g);
    emit_portals(&g);
    emit_speed(&g);
    emit_misc(&g);
    emit_player(&g);
    emit_title_set(&g);
    emit_select_set(&g);
    fclose(g.hdr);
    /* each sprite in each of its palettes, magnified 4 times, in rows */
    const int Z = 4, W = 1200;
    int x = 0, y = 0, rowh = 0;
    typedef struct { int x, y; } Pos;
    Pos *pos = calloc((size_t)s_nsheet * 8, sizeof(Pos));
    for (int i = 0; i < s_nsheet; i++)
        for (int b = 0; b < s_sheet[i].nbank; b++) {
            int w = s_sheet[i].w * Z + 4, h = s_sheet[i].h * Z + 4;
            if (x + w > W) {
                x = 0;
                y += rowh;
                rowh = 0;
            }
            pos[i * 8 + b] = (Pos){x, y};
            x += w;
            if (h > rowh) rowh = h;
        }
    int H = y + rowh;
    uint8_t *img = malloc((size_t)W * H * 3);
    for (int i = 0; i < W * H; i++) {
        img[i * 3] = 40;
        img[i * 3 + 1] = 44;
        img[i * 3 + 2] = 56;
    }
    for (int i = 0; i < s_nsheet; i++) {
        const SheetSpr *sp = &s_sheet[i];
        for (int b = 0; b < sp->nbank; b++)
            for (int py = 0; py < sp->h; py++)
                for (int px = 0; px < sp->w; px++) {
                    int t = sp->tile + (py / 8) * (sp->w / 8) + px / 8;
                    int v = (s_tiles[t * 8 + (py & 7)] >> ((px & 7) * 4)) & 15;
                    if (!v) continue;
                    uint16_t c = (sp->pals ? sp->pals : (const uint16_t (*)[16])s_pals)[sp->bank0 + b][v];
                    for (int zy = 0; zy < Z; zy++)
                        for (int zx = 0; zx < Z; zx++) {
                            uint8_t *p = img + ((size_t)(pos[i * 8 + b].y + py * Z + zy) * W + pos[i * 8 + b].x + px * Z + zx) * 3;
                            p[0] = (uint8_t)((c & 31) << 3);
                            p[1] = (uint8_t)(((c >> 5) & 31) << 3);
                            p[2] = (uint8_t)(((c >> 10) & 31) << 3);
                        }
                }
    }
    free(pos);
    int r = png_write_rgb(out, img, W, H);
    free(img);
    return r;
}
