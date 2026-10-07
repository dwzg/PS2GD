/*
 * gfx.h backend for the Nintendo DS: the 3D engine draws the virtual
 * screen (SCREEN_W x SCREEN_H, 597x448) on the top screen, 256x192,
 * scaled by the pixel grid (PIXEL_GRID, 192/448) both ways.
 *
 * The 3D engine is no painter: it draws the opaque polygons of a frame
 * first and then the see-through ones, and keeps a depth buffer. So every
 * primitive is given a depth of its own, each nearer than the one before,
 * and the see-through ones are drawn in the order they came
 * (GL_TRANS_MANUALSORT): whatever is drawn later is in front, as the core
 * expects. Up to 2048 polygons a frame (the engine's limit; a frame of a
 * level has 400 to 1100 primitives, a quad being one polygon).
 *
 * Its colours are 5 bits a channel. Its alpha is one per polygon (5 bits),
 * not per vertex: a primitive whose corners differ in alpha (a glow fading
 * out) is drawn with a texture, a ramp of 32 alphas, white, the alphas
 * given as its texture coordinates and the colours as the vertices'
 * (modulated), so they blend across it as the core's corners ask.
 *
 * A see-through pixel is not drawn over another see-through pixel of the
 * same polygon ID: the triangles of one shape (a circle's fan) whose edges
 * the rasterizer draws twice blend once there. A shape is a run of
 * primitives of the same colours: each run gets the next ID.
 *
 * The engine has no added blending (BLEND_ADD, the glows and sparks): those
 * are blended as see-through, which is close over the dark backgrounds
 * they mostly show on. A glow (gfx_glow, GFX_GLOW) is one square of a
 * radial texture rather than a fan of triangles, as on the PSP.
 *
 * Text on the pixel grid comes as whole glyphs (gfx_glyph_dev): each is a
 * square of a texture of the font, 2 bits a texel with colour 0 see-
 * through, which keeps opaque text in the opaque polygons.
 *
 * Their outlines too (gfx_glyph_outline_dev): for each size of the font's
 * pixels and of the outline the game draws, a texture of every glyph's
 * outline ring (the pixels within the outline's width of the glyph's,
 * not its own), made when first drawn. The rings of glyphs side by side
 * overlap, but a see-through polygon is not drawn over one of the same
 * polygon ID, and they share one: the outline comes out even.
 *
 * VRAM A holds the textures: the alpha ramp at 0, the glow at 256 bytes,
 * the font at 2 KB, the outlines from 16 KB; VRAM E their palette, white.
 * A texture made after start-up is copied in at the next vertical blank
 * (gfx_nds_vblank), before the 3D engine starts drawing the frame that
 * uses it, while VRAM A is the CPU's.
 */
#include <nds.h>
#include <stdlib.h>
/* libnds' name for the 2D engines' blending, which gfx.h names its own */
#undef BLEND_ALPHA

#include "nds_platform.h"
#include "../core/draw.h"
#include "../core/font.h"
#include "../core/gfx.h"

/* vertices are given in 1/16 device pixel: the projection maps 256 * 16
 * to the screen's width, a power of two (exact in the engine's fixed
 * point) */
#define SUB 16
#define MAX_POLYS 2040
/* each primitive is this much (in the vertex z's 1/4096) nearer than the
 * last: with the projection's depth range, -1..1, 4096 primitives fit */
#define Z_STEP 2
#define Z_FIRST (-4000)

static int s_z;
static int s_polys, s_dropped, s_max_polys;
static int s_blend;
static uint32_t s_attr = 0xFFFFFFFFu; /* the polygon attributes set last */
static uint32_t s_tex = 0xFFFFFFFFu;  /* the texture parameters set last */
static uint32_t s_run_key;            /* the colours of the run of primitives the ID is for */
static int s_id = 1;
static uint32_t s_ramp_param, s_glow_param, s_font_param;

/* the outlines' textures (gfx_glyph_outline_dev) */
#define OUTLINE_SETS 8
#define OUTLINE_VRAM 16384
typedef struct {
    uint8_t px, py, ox, oy;
    int cw, ch;          /* a cell, in texels (a glyph and its outline) */
    uint32_t param;
} OutlineSet;
static OutlineSet s_outline[OUTLINE_SETS];
static int s_outlines, s_outline_full;
static uint32_t s_vram_next = OUTLINE_VRAM;

static void make_font(uint16_t *tex);
static const OutlineSet *outline_set(int px, int py, int ox, int oy);

/* textures waiting for the vertical blank */
#define UPLOADS 4
typedef struct {
    uint32_t offset, bytes;
    uint16_t *data;
} Upload;
static Upload s_upload[UPLOADS];
static int s_uploads;

/* ------------------------------------------------------------------ */

/*
 * A virtual coordinate in the vertices' units (1/16 of a device pixel),
 * rounded to the nearest: x * PIXEL_GRID * 16 from the float's bits, with
 * one integer multiply, rather than libgcc's software multiply, add and
 * conversion (a hundred instructions; there are four to eight of these a
 * primitive).
 */
#define K_FIX ((uint32_t)(192.0 / SCREEN_H * SUB * 16777216.0 + 0.5)) /* K in 8.24 */

static inline int16_t vx(float x)
{
    uint32_t u;
    memcpy(&u, &x, 4);
    int e = (int)((u >> 23) & 255);
    /* |x| = m * 2^(e - 150); |x| * K * 256 = m * K_FIX * 2^(e - 150 - 24 + 8) */
    int sh = 166 - e;
    if (sh >= 64) return 0;  /* under 1/256 of a unit */
    if (sh < 24) return (int16_t)((u >> 31) ? -32768 : 32767); /* far off the screen */
    uint64_t p = (uint64_t)((u & 0x7FFFFFu) | 0x800000u) * K_FIX;
    int32_t v = (int32_t)(p >> sh); /* |x| * K in 1/256 units (under 2^24 here) */
    if (v >= (32767 << 8)) v = 32767 << 8;
    if (u >> 31) v = -v;
    return (int16_t)((v + 128) >> 8); /* rounded: half up */
}

static inline uint16_t col15(Color c)
{
    return (uint16_t)((COL_R(c) >> 3) | ((COL_G(c) >> 3) << 5) | ((COL_B(c) >> 3) << 10));
}

static inline void vertex(int16_t x, int16_t y)
{
    GFX_VERTEX16 = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
    GFX_VERTEX16 = (uint32_t)(uint16_t)(int16_t)s_z;
}

/*
 * The state of the next primitive: its polygon attributes (alpha and ID)
 * and texture, set where they changed. Returns 0 if it isn't to be drawn
 * (all of it transparent, or no polygons left this frame).
 * a_min/a_max: the least and the most alpha of its corners; key: its
 * colours, which tell one shape from the next.
 */
static int prim_begin(unsigned a_min, unsigned a_max, uint32_t key)
{
    if (a_max < 8) return 0; /* under the 5-bit alpha's first step */
    if (s_polys >= MAX_POLYS) {
        s_dropped++;
        return 0;
    }
    s_polys++;
    int ramp = (a_max - a_min) >= 8;
    uint32_t alpha = ramp ? 31 : (a_max >> 3);
    uint32_t id = 0;
    if (alpha < 31 || ramp) {
        if (key != s_run_key) {
            s_run_key = key;
            s_id = s_id >= 63 ? 1 : s_id + 1;
        }
        id = (uint32_t)s_id;
    } else {
        s_run_key = 0;
    }
    uint32_t attr = POLY_ALPHA(alpha) | POLY_ID(id) | POLY_CULL_NONE | POLY_MODULATION;
    uint32_t tex = ramp ? s_ramp_param : 0;
    if (tex != s_tex) {
        GFX_TEX_FORMAT = tex;
        s_tex = tex;
    }
    if (attr != s_attr) {
        GFX_POLY_FORMAT = attr;
        s_attr = attr;
    }
    s_z += Z_STEP;
    return 1 + ramp;
}

/* texture coordinate of an alpha on the ramp (its texels are 0..31, at
 * u = texel * 16 + 8 in 1/16 texel) */
static inline void ramp_coord(Color c)
{
    uint32_t u = (COL_A(c) * 31u + 127u) / 255u * 16u + 8u;
    GFX_TEX_COORD = u;
}

/* ------------------------------------------------------------------ */

void gfx_nds_init(void)
{
    glInit();
    glEnable(GL_BLEND | GL_TEXTURE_2D);
    glDisable(GL_ANTIALIAS);
    glClearColor(0, 0, 0, 31);
    glClearPolyID(63);
    glClearDepth(GL_MAX_DEPTH);
    glViewport(0, 0, 255, 191);

    /* the alpha ramp: 32x8 texels of a 3-bit index (white) and a 5-bit
     * alpha, at the start of VRAM A, its palette at the start of VRAM E;
     * written while the banks are the CPU's, then given to the engine */
    vramSetBankA(VRAM_A_LCD);
    vramSetBankE(VRAM_E_LCD);
    uint16_t *tex = (uint16_t *)VRAM_A;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 32; x += 2) tex[(y * 32 + x) / 2] = (uint16_t)((x << 3) | ((x + 1) << 11));
    for (int i = 0; i < 8; i++) VRAM_E[i] = 0x7FFF;
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankE(VRAM_E_TEX_PALETTE);
    /* offset 0, 32 (8 << 2) by 8 (8 << 0) texels, format 6 (A5I3), the
     * coordinates as given */
    s_ramp_param = (2u << 20) | (0u << 23) | (6u << 26) | (1u << 30);
    /* the glow: 32x32 texels after it (256 bytes in), the alpha falling
     * from the middle to nothing at the edge of the circle in it, as
     * draw_glow's fan does */
    vramSetBankA(VRAM_A_LCD);
    uint8_t row[32];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float dx = (float)x + 0.5f - 16.0f, dy = (float)y + 0.5f - 16.0f;
            float d = sqrtf(dx * dx + dy * dy) / 16.0f;
            int a = d >= 1.0f ? 0 : (int)(31.0f * (1.0f - d) + 0.5f);
            row[x] = (uint8_t)(a << 3);
        }
        for (int x = 0; x < 32; x += 2) tex[(256 + y * 32 + x) / 2] = (uint16_t)(row[x] | (row[x + 1] << 8));
    }
    make_font(tex + 2048 / 2);
    vramSetBankA(VRAM_A_TEXTURE);
    s_glow_param = (256u / 8) | (2u << 20) | (2u << 23) | (6u << 26) | (1u << 30);
    GFX_PAL_FORMAT = 0;
}

/* The outlines of the text the game draws (its fancy text's font scales and
 * outline widths, game_draw.c, play_draw.c), made at start-up: made when
 * first drawn, a set takes most of a frame. Any other is still made then. */
void gfx_nds_prepare_outlines(void)
{
    static const float USED[][2] = {{9, 4}, {2, 2}, {3, 2}, {3, 3}, {4, 3}, {5, 3}};
    for (unsigned i = 0; i < sizeof(USED) / sizeof(USED[0]); i++) {
        int px = (int)(grid_w(USED[i][0]) * PIXEL_GRID + 0.5f), o = (int)(grid_w(USED[i][1]) * PIXEL_GRID + 0.5f);
        outline_set(px, px, o, o);
    }
}

int gfx_nds_dropped(void)
{
    return s_dropped;
}

int gfx_nds_max_polys(void)
{
    int m = s_max_polys;
    s_max_polys = 0;
    return m;
}

void gfx_nds_begin(void)
{
    /* one unit of the vertices is 1/16 of a pixel: the projection takes
     * the screen's 4096 across (1.0 in the engine's 20.12 fixed point) to
     * -1..1, and its 3072 down to 1..-1, and depth -1..1 to 1..-1. Down,
     * 2 / 0.75 has no exact 20.12: it is rounded up (glOrthof32 rounds it
     * down), so that the positions come out a hair below their pixel's
     * edge rather than above it, where the engine's rounding down would
     * take a whole row off the bottom of what is drawn. */
    MATRIX_CONTROL = GL_PROJECTION;
    static const int32_t proj[16] = {
        8192, 0, 0, 0,
        0, -10923, 0, 0,
        0, 0, -4096, 0,
        -4096, 4096, 0, 4096,
    };
    for (int i = 0; i < 16; i++) MATRIX_LOAD4x4 = proj[i];
    MATRIX_CONTROL = GL_MODELVIEW;
    MATRIX_IDENTITY = 0;
    s_z = Z_FIRST;
    s_polys = 0;
    s_attr = s_tex = 0xFFFFFFFFu;
    s_run_key = 0;
    s_blend = BLEND_ALPHA;
}

void gfx_nds_end(void)
{
    if (s_polys > s_max_polys) s_max_polys = s_polys;
    /* see-through polygons in the order they were drawn; depth by z */
    glFlush(GL_TRANS_MANUALSORT);
}

/* ------------------------------------------------------------------ */

void gfx_blend(int mode)
{
    s_blend = mode;
}

static inline unsigned amin3(unsigned a, unsigned b, unsigned c)
{
    unsigned m = a < b ? a : b;
    return m < c ? m : c;
}

static inline unsigned amax3(unsigned a, unsigned b, unsigned c)
{
    unsigned m = a > b ? a : b;
    return m > c ? m : c;
}

void gfx_tri(float x0, float y0, Color c0, float x1, float y1, Color c1, float x2, float y2, Color c2)
{
    unsigned a0 = COL_A(c0), a1 = COL_A(c1), a2 = COL_A(c2);
    int r = prim_begin(amin3(a0, a1, a2), amax3(a0, a1, a2), c0 ^ (c1 * 3u) ^ (c2 * 7u));
    if (!r) return;
    GFX_BEGIN = GL_TRIANGLES;
    GFX_COLOR = col15(c0);
    if (r == 2) ramp_coord(c0);
    vertex(vx(x0), vx(y0));
    GFX_COLOR = col15(c1);
    if (r == 2) ramp_coord(c1);
    vertex(vx(x1), vx(y1));
    GFX_COLOR = col15(c2);
    if (r == 2) ramp_coord(c2);
    vertex(vx(x2), vx(y2));
}

static void quad(const int16_t *v, const Color *c)
{
    unsigned a0 = COL_A(c[0]), a1 = COL_A(c[1]), a2 = COL_A(c[2]), a3 = COL_A(c[3]);
    unsigned lo = amin3(a0, a1, a2), hi = amax3(a0, a1, a2);
    if (a3 < lo) lo = a3;
    if (a3 > hi) hi = a3;
    int r = prim_begin(lo, hi, c[0] ^ (c[1] * 3u) ^ (c[2] * 7u) ^ (c[3] * 13u));
    if (!r) return;
    GFX_BEGIN = GL_QUADS;
    for (int i = 0; i < 4; i++) {
        GFX_COLOR = col15(c[i]);
        if (r == 2) ramp_coord(c[i]);
        vertex(v[2 * i], v[2 * i + 1]);
    }
}

void gfx_quad(const float *xy, const Color *c)
{
    int16_t v[8];
    for (int i = 0; i < 8; i++) v[i] = vx(xy[i]);
    quad(v, c);
}

static void rect4(float x0, float y0, float x1, float y1, Color tl, Color tr, Color br, Color bl)
{
    int16_t a = vx(x0), b = vx(y0), c = vx(x1), d = vx(y1);
    if (a == c || b == d) return; /* under a sixteenth of a pixel */
    int16_t v[8] = {a, b, c, b, c, d, a, d};
    Color col[4] = {tl, tr, br, bl};
    quad(v, col);
}

int gfx_glow(float cx, float cy, float r, Color c)
{
    unsigned a = COL_A(c);
    if (a < 8) return 1;
    if (s_polys >= MAX_POLYS) {
        s_dropped++;
        return 1;
    }
    s_polys++;
    if (s_tex != s_glow_param) {
        GFX_TEX_FORMAT = s_glow_param;
        s_tex = s_glow_param;
    }
    if (c != s_run_key) {
        s_run_key = c;
        s_id = s_id >= 63 ? 1 : s_id + 1;
    }
    uint32_t attr = POLY_ALPHA(a >> 3) | POLY_ID((uint32_t)s_id) | POLY_CULL_NONE | POLY_MODULATION;
    if (attr != s_attr) {
        GFX_POLY_FORMAT = attr;
        s_attr = attr;
    }
    s_z += Z_STEP;
    int16_t x0 = vx(cx - r), y0 = vx(cy - r), x1 = vx(cx + r), y1 = vx(cy + r);
    GFX_BEGIN = GL_QUADS;
    GFX_COLOR = col15(c);
    GFX_TEX_COORD = 0;
    vertex(x0, y0);
    GFX_TEX_COORD = 512;
    vertex(x1, y0);
    GFX_TEX_COORD = (512u << 16) | 512u;
    vertex(x1, y1);
    GFX_TEX_COORD = 512u << 16;
    vertex(x0, y1);
    return 1;
}

/* The font's 128 glyphs in 8x8 cells, 16 across, 8 down (128x64 texels, 2
 * bits each, set pixels colour 1), into tex (VRAM A at 2 KB). */
static void make_font(uint16_t *tex)
{
    font_init();
    for (int i = 0; i < 128 * 64 / 8; i++) tex[i] = 0;
    for (int ch = 0; ch < 128; ch++) {
        uint8_t rows[7];
        if (!font_glyph((char)ch, rows)) continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++) {
                if (!(rows[r] & (1u << (4 - c)))) continue;
                int u = (ch % 16) * 8 + c, v = (ch / 16) * 8 + r, t = v * 128 + u;
                tex[t / 8] |= (uint16_t)(1u << ((t % 8) * 2));
            }
    }
    /* 2 KB in, 128 (8 << 4) by 64 (8 << 3) texels, format 2 (4 colours),
     * colour 0 see-through */
    s_font_param = (2048u / 8) | (4u << 20) | (3u << 23) | (2u << 26) | (1u << 29) | (1u << 30);
}

static int log2_size(int n)
{
    int k = 0;
    while ((8 << k) < n) k++;
    return k;
}

/*
 * The outline set for glyph pixels of px by py and an outline of ox, oy
 * (device pixels): made if it isn't there, its texture queued for the
 * vertical blank. NULL when there is no room (or more sets than kept).
 */
static const OutlineSet *outline_set(int px, int py, int ox, int oy)
{
    for (int i = 0; i < s_outlines; i++)
        if (s_outline[i].px == px && s_outline[i].py == py && s_outline[i].ox == ox && s_outline[i].oy == oy)
            return &s_outline[i];
    if (s_outline_full || s_outlines == OUTLINE_SETS || s_uploads == UPLOADS || px > 8 || py > 8 || ox > 8 || oy > 8)
        return NULL;
    int cw = 5 * px + 2 * ox, ch = 7 * py + 2 * oy;
    int ks = log2_size(16 * cw), kt = log2_size(8 * ch);
    int w = 8 << ks, h = 8 << kt;
    uint32_t bytes = (uint32_t)(w * h / 4);
    if (ks > 7 || kt > 7 || s_vram_next + bytes > 128 * 1024) {
        s_outline_full = 1;
        return NULL;
    }
    uint16_t *tex = calloc(bytes / 2, 2);
    if (!tex) return NULL;
    /* each glyph's rows as bits at device size, grown by ox across (OR
     * shifted copies), then by oy down (OR of rows), less its own */
    for (int g = 0; g < 128; g++) {
        uint8_t rows[7];
        if (!font_glyph((char)g, rows)) continue;
        uint64_t own[7 * 8 + 16], grown[7 * 8 + 16];
        for (int v = 0; v < ch; v++) own[v] = grown[v] = 0;
        for (int y = 0; y < 7 * py; y++) {
            uint64_t m = 0;
            for (int x = 0; x < 5 * px; x++)
                if ((rows[y / py] >> (4 - x / px)) & 1) m |= 1ull << (x + ox);
            own[y + oy] = m;
            uint64_t gx = 0;
            for (int d = -ox; d <= ox; d++) gx |= d < 0 ? m >> -d : m << d;
            for (int v = y; v <= y + 2 * oy; v++) grown[v] |= gx;
        }
        int u0 = (g % 16) * cw, v0 = (g / 16) * ch;
        for (int v = 0; v < ch; v++) {
            uint64_t m = grown[v] & ~own[v];
            for (int u = 0; m; u++, m >>= 1)
                if (m & 1) {
                    int t = (v0 + v) * w + u0 + u;
                    tex[t / 8] |= (uint16_t)(1u << ((t % 8) * 2));
                }
        }
    }
    OutlineSet *o = &s_outline[s_outlines++];
    o->px = (uint8_t)px;
    o->py = (uint8_t)py;
    o->ox = (uint8_t)ox;
    o->oy = (uint8_t)oy;
    o->cw = cw;
    o->ch = ch;
    o->param = (s_vram_next / 8) | ((uint32_t)ks << 20) | ((uint32_t)kt << 23) | (2u << 26) | (1u << 29) | (1u << 30);
    s_upload[s_uploads++] = (Upload){s_vram_next, bytes, tex};
    s_vram_next += bytes;
    return o;
}

void gfx_nds_vblank(void)
{
    if (!s_uploads) return;
    vramSetBankA(VRAM_A_LCD);
    for (int i = 0; i < s_uploads; i++) {
        memcpy((uint8_t *)VRAM_A + s_upload[i].offset, s_upload[i].data, s_upload[i].bytes);
        free(s_upload[i].data);
    }
    s_uploads = 0;
    vramSetBankA(VRAM_A_TEXTURE);
}

/* The state of a textured square of one colour per edge (a glyph, an
 * outline): 0 if it isn't drawn. */
static int textured_begin(uint32_t param, Color top, Color bottom)
{
    unsigned at = COL_A(top), ab = COL_A(bottom);
    unsigned a = at > ab ? at : ab;
    if (a < 8) return 0;
    if (s_polys >= MAX_POLYS) {
        s_dropped++;
        return 0;
    }
    s_polys++;
    uint32_t alpha = a >> 3, id = 0;
    if (alpha < 31) {
        uint32_t key = top ^ (bottom * 3u) ^ param;
        if (key != s_run_key) {
            s_run_key = key;
            s_id = s_id >= 63 ? 1 : s_id + 1;
        }
        id = (uint32_t)s_id;
    } else {
        s_run_key = 0;
    }
    uint32_t attr = POLY_ALPHA(alpha) | POLY_ID(id) | POLY_CULL_NONE | POLY_MODULATION;
    if (s_tex != param) {
        GFX_TEX_FORMAT = param;
        s_tex = param;
    }
    if (attr != s_attr) {
        GFX_POLY_FORMAT = attr;
        s_attr = attr;
    }
    s_z += Z_STEP;
    return 1;
}

/* the square (x0, y0)-(x1, y1) in pixels, texels (u0, v0)-(u1, v1) in
 * 1/16 */
static void textured_quad(int x0, int y0, int x1, int y1, uint32_t u0, uint32_t v0, uint32_t u1, uint32_t v1,
                          Color top, Color bottom)
{
    uint16_t ct = col15(top), cb = col15(bottom);
    GFX_BEGIN = GL_QUADS;
    GFX_COLOR = ct;
    GFX_TEX_COORD = (v0 << 16) | u0;
    vertex((int16_t)(x0 * SUB), (int16_t)(y0 * SUB));
    GFX_TEX_COORD = (v0 << 16) | u1;
    vertex((int16_t)(x1 * SUB), (int16_t)(y0 * SUB));
    GFX_COLOR = cb;
    GFX_TEX_COORD = (v1 << 16) | u1;
    vertex((int16_t)(x1 * SUB), (int16_t)(y1 * SUB));
    GFX_TEX_COORD = (v1 << 16) | u0;
    vertex((int16_t)(x0 * SUB), (int16_t)(y1 * SUB));
}

int gfx_glyph_outline_dev(int x, int y, int px, int py, int ox, int oy, unsigned char ch, Color c)
{
    const OutlineSet *o = outline_set(px, py, ox, oy);
    if (!o) return 0;
    if (!textured_begin(o->param, c, c)) return 1;
    /* the cell's texels: one texel a pixel, a sixteenth in */
    uint32_t u0 = (uint32_t)((ch % 16) * o->cw * 16 + 1), v0 = (uint32_t)((ch / 16) * o->ch * 16 + 1);
    textured_quad(x - ox, y - oy, x - ox + o->cw, y - oy + o->ch, u0, v0, u0 + (uint32_t)o->cw * 16,
                  v0 + (uint32_t)o->ch * 16, c, c);
    return 1;
}

void gfx_glyph_dev(int x, int y, int px, int py, unsigned char ch, Color top, Color bottom)
{
    if (!textured_begin(s_font_param, top, bottom)) return;
    /* the cell's texels, in 1/16, a sixteenth in from its corner */
    uint32_t u0 = (uint32_t)((ch % 16) * 8 * 16 + 1), v0 = (uint32_t)((ch / 16) * 8 * 16 + 1);
    textured_quad(x, y, x + 5 * px, y + 7 * py, u0, v0, u0 + 5 * 16, v0 + 7 * 16, top, bottom);
}

void gfx_rect_dev(int x0, int y0, int x1, int y1, Color top, Color bottom)
{
    if (x0 >= x1 || y0 >= y1) return;
    int16_t a = (int16_t)(x0 * SUB), b = (int16_t)(y0 * SUB), c = (int16_t)(x1 * SUB), d = (int16_t)(y1 * SUB);
    int16_t v[8] = {a, b, c, b, c, d, a, d};
    Color col[4] = {top, top, bottom, bottom};
    quad(v, col);
}

void gfx_rect(float x0, float y0, float x1, float y1, Color c)
{
    rect4(x0, y0, x1, y1, c, c, c, c);
}

void gfx_rect_v(float x0, float y0, float x1, float y1, Color top, Color bottom)
{
    rect4(x0, y0, x1, y1, top, top, bottom, bottom);
}

void gfx_rect_h(float x0, float y0, float x1, float y1, Color left, Color right)
{
    rect4(x0, y0, x1, y1, left, right, right, left);
}
