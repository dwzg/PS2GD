/*
 * gfx.h backend for the PSP: the GE draws the virtual screen (SCREEN_W x
 * SCREEN_H, 790x448) onto the 480x272 LCD through an orthographic
 * projection, so the scaling costs the CPU nothing. It scales both ways by
 * the pixel grid (PIXEL_GRID, 272/448), which text, icons and lines are
 * placed on.
 *
 * Primitives are batched as coloured triangles into a vertex buffer and
 * drawn whenever the blend mode changes. The display list runs as it is
 * built (GU_DIRECT), so each batch is written back from the data cache
 * before the GE is told to draw it.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <psputils.h>

#include "psp_platform.h"
#include "../core/gfx.h"

#define FB_BYTES (PSP_BUF_W * PSP_SCR_H * 4)
/* A frame needs about 20000 vertices at most (the level select screen);
 * one draw call takes up to 65535. */
#define MAX_VERTS 60000

typedef struct {
    uint32_t color;
    float x, y, z;
} Vertex;
#define VTYPE (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)

static unsigned int s_list[64 * 1024] __attribute__((aligned(64)));
static Vertex s_verts[MAX_VERTS] __attribute__((aligned(64)));
static int s_n;     /* vertices in this frame */
static int s_drawn; /* of which already handed to the GE */
static int s_blend = -1;
static unsigned s_dropped;

/* 0xAARRGGBB -> the GE's 0xAABBGGRR */
static inline uint32_t ge_col(Color c)
{
    return (c & 0xFF00FF00u) | ((c >> 16) & 0xFFu) | ((c & 0xFFu) << 16);
}

static void flush(void)
{
    int n = s_n - s_drawn;
    if (n <= 0) return;
    const Vertex *v = &s_verts[s_drawn];
    sceKernelDcacheWritebackRange(v, (unsigned)n * sizeof(Vertex));
    sceGuDrawArray(GU_TRIANGLES, VTYPE, n, NULL, v);
    s_drawn = s_n;
}

/* Room for n more vertices? Primitives that don't fit are left out. */
static inline int reserve(int n)
{
    if (s_n + n <= MAX_VERTS) return 1;
    s_dropped++;
    return 0;
}

static inline void put(float x, float y, Color c)
{
    Vertex *v = &s_verts[s_n++];
    v->color = ge_col(c);
    v->x = x;
    v->y = y;
    v->z = 0.0f;
}

void gfx_psp_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, s_list);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, PSP_BUF_W);
    sceGuDispBuffer(PSP_SCR_W, PSP_SCR_H, (void *)FB_BYTES, PSP_BUF_W);
    sceGuDepthBuffer((void *)(2 * FB_BYTES), PSP_BUF_W);
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

    /* virtual screen, y down -> clip space, PIXEL_GRID device pixels per
     * virtual one */
    ScePspFMatrix4 proj = {{2.0f * PIXEL_GRID / PSP_SCR_W, 0.0f, 0.0f, 0.0f},
                           {0.0f, -2.0f * PIXEL_GRID / PSP_SCR_H, 0.0f, 0.0f},
                           {0.0f, 0.0f, -1.0f, 0.0f},
                           {-1.0f, 1.0f, 0.0f, 1.0f}};
    ScePspFMatrix4 id = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
    sceGuSetMatrix(GU_PROJECTION, &proj);
    sceGuSetMatrix(GU_VIEW, &id);
    sceGuSetMatrix(GU_MODEL, &id);

    sceGuClearColor(0xFF000000u);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void gfx_psp_begin(void)
{
    s_n = s_drawn = 0;
    sceGuStart(GU_DIRECT, s_list);
    sceGuClearColor(0xFF000000u);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    s_blend = -1;
    gfx_blend(BLEND_ALPHA);
}

void gfx_psp_submit(void)
{
    flush();
    sceGuFinish();
}

void gfx_psp_sync(void)
{
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WAIT);
}

void gfx_psp_flip(void)
{
    /* takes effect at once: the caller is in the vblank */
    sceGuSwapBuffers();
}

unsigned gfx_psp_dropped(void)
{
    return s_dropped;
}

void gfx_blend(int mode)
{
    if (mode == s_blend) return;
    flush();
    s_blend = mode;
    if (mode == BLEND_ADD) /* Cs * As + Cd */
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFFu);
    else /* Cs * As + Cd * (1 - As) */
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
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
