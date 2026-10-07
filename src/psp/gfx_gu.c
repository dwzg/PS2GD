/*
 * gfx.h backend for the PSP: the GE draws the virtual screen (SCREEN_W x
 * SCREEN_H, 790x448) onto the 480x272 LCD through an orthographic
 * projection, so the scaling costs the CPU nothing. It scales both ways by
 * the pixel grid (PIXEL_GRID, 272/448), which text, icons and lines are
 * placed on.
 *
 * Primitives are batched as coloured triangles into a vertex buffer and
 * drawn whenever the blend mode changes (or a glow comes, see below). The
 * display list runs as it is built (GU_DIRECT), so each batch is written
 * back from the data cache before the GE is told to draw it. A frame is
 * cleared only when its first primitive does not cover the screen: every
 * screen starts with an opaque background over all of it, and clearing
 * first would have the GE fill every pixel twice.
 *
 * Smoothing (anti-aliasing): when there is time for it, each frame is drawn
 * twice, a quarter of a pixel down-right of where it belongs into the frame
 * buffer and a quarter of a pixel up-left into a second buffer (in the VRAM
 * a depth buffer would take, which the game has no use for), and the second
 * is blended half over the first. Edges between pixels come out with a
 * half-tone step; edges on whole pixels (text, icons, strokes on the pixel
 * grid: draw.h) are drawn the same both times and stay sharp, as the GE
 * samples each pixel 7/16 of a pixel in from its top left corner. The
 * second drawing replays the first one's batches (the vertices are already
 * in memory), so it costs the CPU almost nothing, but it doubles the GE's
 * work. Glows are drawn from a small radial texture then too, smooth
 * instead of faceted fans.
 *
 * The GE time it takes is guarded frame by frame (aa_fits): the second
 * drawing is only started when it is sure to end before the vblank. It
 * draws the same triangles as the first, so it takes no longer than the
 * time from the frame's start until the GE finished the first (the GE
 * cannot have worked on that before the frame started); the blend's own
 * time is measured. A frame that would not fit shows the first drawing
 * alone, and the smoothing then stays off until the frames have been light
 * enough for it for two seconds.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <psputils.h>

#include "psp_platform.h"
#include "../core/gfx.h"

#define FB_BYTES (PSP_BUF_W * PSP_SCR_H * 4)
/* VRAM: the two frame buffers, then the second drawing's buffer */
#define AA_BUF (2 * FB_BYTES)
/* A frame needs about 20000 vertices at most (the level select screen);
 * one draw call takes up to 65535. */
#define MAX_VERTS 60000
#define MAX_GLOWS 256
#define MAX_BATCHES 2048

/* the smoothing's guard, in microseconds: what must be left of a frame
 * after the second drawing (the vblank wait, threads that run in between),
 * what the blend is taken to cost before it has been timed, and the further
 * room frames need for the smoothing to come back on */
#define AA_MARGIN_US 1500
#define AA_BLEND_GUESS_US 3000
#define AA_HYST_US 1000
/* frames the smoothing stays off once a frame had no time for it (and at
 * boot, while the timings settle) */
#define AA_COOL_FRAMES 120
/* the two drawings' offsets, in device pixels: the PSP samples each pixel
 * 7/16 in from its corner, so whatever has its edges on whole pixels
 * covers the same pixels at either (anything within -9/16..7/16 would) */
#define AA_JITTER 0.25f

typedef struct {
    uint32_t color;
    float x, y, z;
} Vertex;
#define VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} TVertex;
#define TVTYPE (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

/* the blend: a full-screen copy of the second buffer, in slices 32 texels
 * wide (the texture cache's width) */
typedef struct {
    unsigned short u, v;
    short x, y, z;
} BlitVertex;
#define BLIT_VTYPE (GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define BLIT_SLICE 32

/* what a batch draws: triangles from s_verts, or glows from s_tverts */
enum { KIND_TRI, KIND_GLOW };
typedef struct {
    const void *v;
    unsigned short count;
    unsigned char kind, blend;
} Batch;

#define GLOW_TEX 64
static uint32_t s_glow_tex[GLOW_TEX * GLOW_TEX] __attribute__((aligned(16)));

static unsigned int s_list[64 * 1024] __attribute__((aligned(64)));
static Vertex s_verts[MAX_VERTS] __attribute__((aligned(64)));
static TVertex s_tverts[MAX_GLOWS * 6] __attribute__((aligned(64)));
static Batch s_batches[MAX_BATCHES];
static int s_n;      /* vertices in this frame */
static int s_drawn;  /* of which already handed to the GE */
static int s_tn, s_tdrawn; /* the same for glows */
static int s_nbatch, s_batch_overflow;
static int s_kind;
static int s_blend = -1;
static int s_cleared; /* the frame began with a clear: -1 not known yet */
static unsigned s_dropped;
static void *s_back; /* the frame buffer being drawn (relative to VRAM) */

/* the guard (times from sceKernelGetSystemTimeLow) */
static int s_aa;          /* this frame is drawn twice, if it fits */
static int s_cool = AA_COOL_FRAMES;
static unsigned s_deadline; /* when this frame must be done: its vblank */
static unsigned s_t_begin;
static unsigned s_blend_us = AA_BLEND_GUESS_US;
static GfxPspStats s_stats;

/* 0xAARRGGBB -> the GE's 0xAABBGGRR */
static inline uint32_t ge_col(Color c)
{
    return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}

static void set_projection(float dx, float dy)
{
    /* virtual screen, y down -> clip space, PIXEL_GRID device pixels per
     * virtual one, moved by (dx, dy) device pixels */
    ScePspFMatrix4 proj = {{2.0f * PIXEL_GRID / PSP_SCR_W, 0.0f, 0.0f, 0.0f},
                           {0.0f, -2.0f * PIXEL_GRID / PSP_SCR_H, 0.0f, 0.0f},
                           {0.0f, 0.0f, -1.0f, 0.0f},
                           {-1.0f + 2.0f * dx / PSP_SCR_W, 1.0f - 2.0f * dy / PSP_SCR_H, 0.0f, 1.0f}};
    sceGuSetMatrix(GU_PROJECTION, &proj);
}

static void set_blend(int mode)
{
    if (mode == BLEND_ADD) /* Cs * As + Cd */
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFFu);
    else /* Cs * As + Cd * (1 - As) */
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
}

/* The glow texture's state (it is switched on per batch). */
static void set_glow_texture(void)
{
    sceGuTexMode(GU_PSM_8888, 0, 0, 1);
    sceGuTexImage(0, GLOW_TEX, GLOW_TEX, GLOW_TEX, s_glow_tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

static void set_kind(int kind)
{
    if (kind == KIND_GLOW) sceGuEnable(GU_TEXTURE_2D);
    else sceGuDisable(GU_TEXTURE_2D);
}

/* Does the frame's first primitive cover the whole screen without letting
 * anything through (the background every screen starts with)? Then the
 * frame needs no clear. */
static int covers_screen(void)
{
    if (s_kind != KIND_TRI || s_blend != BLEND_ALPHA || s_n < 6) return 0;
    const Vertex *v = s_verts;
    for (int i = 0; i < 6; i++)
        if ((v[i].color >> 24) != 0xFF) return 0;
    /* a rectangle as gfx_rect puts it: (x0,y0) (x1,y0) (x1,y1) (x0,y0) (x1,y1) (x0,y1) */
    if (v[3].x != v[0].x || v[3].y != v[0].y || v[4].x != v[2].x || v[4].y != v[2].y || v[1].y != v[0].y ||
        v[1].x != v[2].x || v[5].x != v[0].x || v[5].y != v[2].y)
        return 0;
    float x0 = v[0].x < v[1].x ? v[0].x : v[1].x, x1 = v[0].x < v[1].x ? v[1].x : v[0].x;
    float y0 = v[0].y < v[2].y ? v[0].y : v[2].y, y1 = v[0].y < v[2].y ? v[2].y : v[0].y;
    return x0 <= 0.0f && y0 <= 0.0f && x1 >= (float)SCREEN_W && y1 >= (float)SCREEN_H;
}

static void clear(void)
{
    sceGuClearColor(0xFF000000u);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

static void decide_clear(void)
{
    s_cleared = !covers_screen();
    if (s_cleared) {
        clear();
        s_stats.clears++;
    }
}

static void flush(void)
{
    int n;
    const void *v;
    if (s_kind == KIND_TRI) {
        n = s_n - s_drawn;
        v = &s_verts[s_drawn];
        s_drawn = s_n;
    } else {
        n = s_tn - s_tdrawn;
        v = &s_tverts[s_tdrawn];
        s_tdrawn = s_tn;
    }
    if (n <= 0) return;
    if (s_cleared < 0) decide_clear();
    sceKernelDcacheWritebackRange(v, (unsigned)n * (s_kind == KIND_TRI ? sizeof(Vertex) : sizeof(TVertex)));
    sceGuDrawArray(GU_TRIANGLES, s_kind == KIND_TRI ? VTYPE : TVTYPE, n, NULL, v);
    if (s_nbatch < MAX_BATCHES) {
        Batch *b = &s_batches[s_nbatch++];
        b->v = v;
        b->count = (unsigned short)n;
        b->kind = (unsigned char)s_kind;
        b->blend = (unsigned char)s_blend;
    } else {
        s_batch_overflow = 1;
    }
}

/* Room for n more vertices? Primitives that don't fit are left out. */
static inline int reserve(int n)
{
    if (s_kind != KIND_TRI) {
        flush();
        s_kind = KIND_TRI;
        set_kind(KIND_TRI);
    }
    if (s_n + n <= MAX_VERTS) return 1;
    s_dropped++;
    return 0;
}

static inline void put(float x, float y, Color c)
{
    Vertex *v = &s_verts[s_n++];
    v->color = ge_col(c);
    /* The screen's right edge is 0.4 pixels into the LCD's last column:
     * whatever reaches it is drawn on past the LCD's edge, so that column
     * is covered by the up-left drawing too. */
    if (x > (float)SCREEN_W - 0.01f && x < (float)SCREEN_W + 0.01f) x = (float)SCREEN_W + 2.0f;
    v->x = x;
    v->y = y;
    v->z = 0.0f;
}

/* The glow texture: alpha falling off from the centre to nothing 31
 * texels out (the outermost texels stay clear, so it fades out before the
 * edge of its square), white; swizzled, as the GE reads it fastest. */
static void make_glow_texture(void)
{
    static uint32_t lin[GLOW_TEX * GLOW_TEX];
    const float c = GLOW_TEX / 2, reach = GLOW_TEX / 2 - 1;
    for (int y = 0; y < GLOW_TEX; y++)
        for (int x = 0; x < GLOW_TEX; x++) {
            float dx = x + 0.5f - c, dy = y + 0.5f - c;
            float a = 1.0f - sqrtf(dx * dx + dy * dy) / reach;
            int ai = a <= 0.0f ? 0 : (int)(a * 255.0f + 0.5f);
            lin[y * GLOW_TEX + x] = ((uint32_t)ai << 24) | 0x00FFFFFFu;
        }
    /* swizzled: blocks of 16 bytes by 8 rows, row by row */
    const int row_words = GLOW_TEX, block_words = 4;
    uint32_t *out = s_glow_tex;
    for (int by = 0; by < GLOW_TEX / 8; by++)
        for (int bx = 0; bx < row_words / block_words; bx++)
            for (int r = 0; r < 8; r++)
                for (int w = 0; w < block_words; w++) *out++ = lin[(by * 8 + r) * row_words + bx * block_words + w];
    sceKernelDcacheWritebackRange(s_glow_tex, sizeof(s_glow_tex));
}

void gfx_psp_init(void)
{
    make_glow_texture();
    sceGuInit();
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, PSP_BUF_W);
    s_back = (void *)0;
    sceGuDispBuffer(PSP_SCR_W, PSP_SCR_H, (void *)FB_BYTES, PSP_BUF_W);
    /* no depth buffer: depth tests and writes are off (its VRAM holds the
     * smoothing's second drawing) */
    /* the screen sits in the middle of the GE's 4096x4096 drawing space,
     * so geometry reaching past its edges is clipped, not wrapped */
    sceGuOffset(2048 - PSP_SCR_W / 2, 2048 - PSP_SCR_H / 2);
    sceGuViewport(2048, 2048, PSP_SCR_W, PSP_SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, PSP_SCR_W, PSP_SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE); /* no depth writes */
    sceGuDisable(GU_CULL_FACE); /* quads come in either winding */
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_DITHER);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);

    set_projection(0.0f, 0.0f);
    ScePspFMatrix4 id = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
    sceGuSetMatrix(GU_VIEW, &id);
    sceGuSetMatrix(GU_MODEL, &id);

    clear();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void gfx_psp_begin(unsigned deadline)
{
    s_deadline = deadline;
    s_t_begin = sceKernelGetSystemTimeLow();
    s_aa = s_cool == 0;
    s_n = s_drawn = s_tn = s_tdrawn = 0;
    s_nbatch = s_batch_overflow = 0;
    s_cleared = -1; /* when the first primitive comes */
    sceGuStart(GU_DIRECT, s_list);
    set_projection(s_aa ? AA_JITTER : 0.0f, s_aa ? AA_JITTER : 0.0f);
    set_glow_texture();
    s_kind = KIND_TRI;
    set_kind(KIND_TRI);
    s_blend = -1;
    gfx_blend(BLEND_ALPHA);
}

void gfx_psp_submit(void)
{
    flush();
    if (s_cleared < 0) decide_clear(); /* nothing drawn */
    sceGuFinish();
}

/* Is there time to draw the frame again and blend it in, by the deadline? */
static int aa_fits(unsigned now, unsigned extra)
{
    unsigned ge_bound = now - s_t_begin; /* the GE time of the first drawing, at most */
    unsigned left = s_deadline - now;
    if ((int)left <= 0) return 0; /* (late already) */
    return ge_bound + s_blend_us + AA_MARGIN_US + extra <= left;
}

static void draw_again(void)
{
    /* the second drawing, up-left, into the second buffer */
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBufferList(GU_PSM_8888, (void *)AA_BUF, PSP_BUF_W);
    set_projection(-AA_JITTER, -AA_JITTER);
    if (s_cleared) clear();
    set_glow_texture();
    int kind = -1, blend = -1;
    for (int i = 0; i < s_nbatch; i++) {
        const Batch *b = &s_batches[i];
        if (b->kind != kind) set_kind(kind = b->kind);
        if (b->blend != blend) set_blend(blend = b->blend);
        sceGuDrawArray(GU_TRIANGLES, b->kind == KIND_TRI ? VTYPE : TVTYPE, b->count, NULL, b->v);
    }
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
}

static void blend_in(void)
{
    /* the second buffer half over the first: Cs * 127/255 + Cd * 129/255
     * (with the GE's rounding, two equal colours come out as they were or
     * off by one; 128 and 128 would be off by one for every odd one) */
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBufferList(GU_PSM_8888, s_back, PSP_BUF_W);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 512, PSP_BUF_W, (char *)sceGeEdramGetAddr() + AA_BUF);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFlush();
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0x7F7F7Fu, 0x818181u);
    const int n = PSP_SCR_W / BLIT_SLICE;
    BlitVertex *v = sceGuGetMemory(2 * n * sizeof(BlitVertex));
    for (int i = 0; i < n; i++) {
        short x0 = (short)(i * BLIT_SLICE), x1 = (short)(x0 + BLIT_SLICE);
        v[2 * i] = (BlitVertex){(unsigned short)x0, 0, x0, 0, 0};
        v[2 * i + 1] = (BlitVertex){(unsigned short)x1, PSP_SCR_H, x1, PSP_SCR_H, 0};
    }
    sceGuDrawArray(GU_SPRITES, BLIT_VTYPE, 2 * n, NULL, v);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
}

void gfx_psp_sync(void)
{
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
    unsigned t_a = sceKernelGetSystemTimeLow();
    if (!s_aa) {
        /* off: back on after AA_COOL_FRAMES frames that would have had
         * the time, with some to spare */
        if (!aa_fits(t_a, AA_HYST_US)) s_cool = AA_COOL_FRAMES;
        else if (s_cool > 0) s_cool--;
        return;
    }
    if (s_batch_overflow || !aa_fits(t_a, 0)) {
        /* no time (or too many batches to replay): this frame shows the
         * first drawing alone */
        s_cool = AA_COOL_FRAMES;
        s_stats.skipped++;
        return;
    }
    draw_again();
    unsigned t_b = sceKernelGetSystemTimeLow();
    blend_in();
    unsigned t_c = sceKernelGetSystemTimeLow();
    unsigned blend_us = t_c - t_b;
    /* the blend's time: the highest lately, slowly forgetting */
    s_blend_us = blend_us > s_blend_us ? blend_us : (s_blend_us * 15 + blend_us) / 16;
    s_stats.frames++;
    s_stats.again_us += t_b - t_a;
    if (t_b - t_a > s_stats.again_max_us) s_stats.again_max_us = t_b - t_a;
    s_stats.blend_us += blend_us;
    if (blend_us > s_stats.blend_max_us) s_stats.blend_max_us = blend_us;
    if ((unsigned)s_nbatch > s_stats.batches_max) s_stats.batches_max = (unsigned)s_nbatch;
    if ((int)(s_deadline - t_c) < AA_MARGIN_US / 2) {
        /* closer to the vblank than the guard allows for (it should not
         * be): off for a good while */
        s_cool = 10 * AA_COOL_FRAMES;
        s_stats.close++;
    }
}

void gfx_psp_flip(void)
{
    /* takes effect at once: the caller is in the vblank */
    s_back = sceGuSwapBuffers();
}

unsigned gfx_psp_dropped(void)
{
    return s_dropped;
}

void gfx_psp_stats(GfxPspStats *out)
{
    *out = s_stats;
    GfxPspStats zero = {0};
    s_stats = zero;
}

void gfx_blend(int mode)
{
    if (mode == s_blend) return;
    flush();
    s_blend = mode;
    set_blend(mode);
}

int gfx_glow(float cx, float cy, float r, Color c)
{
    if (!s_aa || s_tn + 6 > MAX_GLOWS * 6) return 0;
    if (s_kind != KIND_GLOW) {
        flush();
        s_kind = KIND_GLOW;
        set_kind(KIND_GLOW);
    }
    /* the texture fades out 31 texels from its centre: r out */
    float h = r * (GLOW_TEX / 2) / (GLOW_TEX / 2 - 1);
    float x0 = cx - h, y0 = cy - h, x1 = cx + h, y1 = cy + h;
    uint32_t col = ge_col(c);
    const float q[6][4] = {{0, 0, x0, y0}, {1, 0, x1, y0}, {1, 1, x1, y1},
                           {0, 0, x0, y0}, {1, 1, x1, y1}, {0, 1, x0, y1}};
    for (int i = 0; i < 6; i++) {
        TVertex *v = &s_tverts[s_tn++];
        v->u = q[i][0];
        v->v = q[i][1];
        v->color = col;
        v->x = q[i][2];
        v->y = q[i][3];
        v->z = 0.0f;
    }
    return 1;
}

void gfx_tri(float x0, float y0, Color c0, float x1, float y1, Color c1, float x2, float y2, Color c2)
{
    if (!reserve(3)) return;
    put(x0, y0, c0);
    put(x1, y1, c1);
    put(x2, y2, c2);
}

void gfx_quad(const float *xy, const Color *c)
{
    if (!reserve(6)) return;
    put(xy[0], xy[1], c[0]);
    put(xy[2], xy[3], c[1]);
    put(xy[4], xy[5], c[2]);
    put(xy[0], xy[1], c[0]);
    put(xy[4], xy[5], c[2]);
    put(xy[6], xy[7], c[3]);
}

static void quad4(float x0, float y0, float x1, float y1, Color tl, Color tr, Color br, Color bl)
{
    float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    Color c[4] = {tl, tr, br, bl};
    gfx_quad(xy, c);
}

void gfx_rect(float x0, float y0, float x1, float y1, Color c)
{
    quad4(x0, y0, x1, y1, c, c, c, c);
}

void gfx_rect_v(float x0, float y0, float x1, float y1, Color top, Color bottom)
{
    quad4(x0, y0, x1, y1, top, top, bottom, bottom);
}

void gfx_rect_h(float x0, float y0, float x1, float y1, Color left, Color right)
{
    quad4(x0, y0, x1, y1, left, right, right, left);
}
