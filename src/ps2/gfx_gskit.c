/* gfx.h backend for the PlayStation 2 using gsKit. */
#include <gsKit.h>
#include <dmaKit.h>

#include "ps2_platform.h"
#include "../core/gfx.h"
#include "../core/draw.h"
#include "../core/platform.h"

static GSGLOBAL *s_gs;
static float s_sx = 1.0f, s_sy = 1.0f;
static int s_blend = -1;

/* GS alpha: 0x80 means fully opaque. */
static inline u64 gs_col(Color c)
{
    return GS_SETREG_RGBAQ(COL_R(c), COL_G(c), COL_B(c), (COL_A(c) + 1) >> 1, 0);
}

int gfx_ps2_init(void)
{
    s_gs = gsKit_init_global_custom(2 * 1024 * 1024, GS_RENDER_QUEUE_PER_POOLSIZE);
    if (!s_gs) return -1;
    s_gs->PSM = GS_PSM_CT24;
    s_gs->PSMZ = GS_PSMZ_16S;
    s_gs->ZBuffering = GS_SETTING_OFF;
    s_gs->DoubleBuffering = GS_SETTING_ON;
    s_gs->PrimAlphaEnable = GS_SETTING_ON;
    s_gs->PrimAAEnable = GS_SETTING_OFF;
    s_gs->Dithering = GS_SETTING_OFF;

    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(s_gs);
    gsKit_mode_switch(s_gs, GS_ONESHOT);
    gsKit_set_test(s_gs, GS_ZTEST_OFF);
    gsKit_set_primalpha(s_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

    s_sx = (float)s_gs->Width / SCREEN_W;
    s_sy = (float)s_gs->Height / SCREEN_H;
    /* text, icons and outlines on whole pixels of the picture: in PAL its
     * 512 lines show the virtual screen's 448, a virtual pixel is 8/7 of a
     * line tall (draw.h) */
    draw_set_pixel_grid(s_sx);
    draw_set_pixel_grid_y(s_sy);
    return 0;
}

int gfx_ps2_is_pal(void)
{
    return s_gs && s_gs->Mode == GS_MODE_PAL;
}

float gfx_ps2_refresh_hz(void)
{
    /* interlaced field rates */
    return gfx_ps2_is_pal() ? 50.0f : 60000.0f / 1001.0f;
}

void gfx_ps2_begin(void)
{
    s_blend = -1;
    gfx_blend(BLEND_ALPHA);
    gsKit_clear(s_gs, GS_SETREG_RGBAQ(0, 0, 0, 0x80, 0));
}

void gfx_ps2_submit(void)
{
    /* starts the DMA; the GS draws while the EE goes on */
    gsKit_queue_exec(s_gs);
}

/*
 * The flicker filter (the options' FLICKER FILTER, off unless chosen): the
 * display's two read circuits show the frame one line apart, blended, so
 * each line of the interlaced picture mixes two of the frame's, and an
 * edge or a line one pixel tall no longer shows in one field and not the
 * other. Set up as ps2sdk's libgraph sets up its own (graph_set_mode with
 * the flicker filter, graph_set_screen, graph_set_framebuffer_filtered,
 * graph_enable_output): circuit 1 reads the frame from its first line,
 * circuit 2 from its second and one line less of it (nothing past the
 * frame's end), and the merge circuit blends them, circuit 1 at a fixed
 * alpha of 0x70. It costs no drawing: the merge circuit does it as it
 * reads the frame out. Off, the registers are as gsKit_init_screen left
 * them (circuit 2 alone, the frame's full height) and the flip writes what
 * it always did.
 */
static int s_filter, s_filter_shown;

void plat_flicker_filter(int on)
{
    s_filter = on != 0; /* from the next flip */
}

static void set_merge(int filter)
{
    int dx = s_gs->StartX + s_gs->StartXOffset, dy = s_gs->StartY + s_gs->StartYOffset;
    if (filter) {
        GS_SET_DISPLAY2(dx, dy, s_gs->MagH, s_gs->MagV, s_gs->DW - 1, s_gs->DH - 2);
        GS_SET_PMODE(1, 1, 1, 0, 0, 0x70);
    } else {
        GS_SET_PMODE(0, 1, 0, 1, 0, 0x80);
        GS_SET_DISPLAY2(dx, dy, s_gs->MagH, s_gs->MagV, s_gs->DW - 1, s_gs->DH - 1);
    }
}

void gfx_ps2_flip(void)
{
    /* gsKit_sync_flip() without its busy-wait for the vblank: the caller has
     * already slept until one started */
    gsKit_finish();
    unsigned fbp = s_gs->ScreenBuffer[s_gs->ActiveBuffer & 1] / 8192, fbw = s_gs->Width / 64;
    int change = s_filter != s_filter_shown;
    s_filter_shown = s_filter;
    /* circuit 1 off before circuit 2 moves back; on once both read the frame */
    if (change && !s_filter_shown) set_merge(0);
    if (s_filter_shown) GS_SET_DISPFB1(fbp, fbw, s_gs->PSM, 0, 0);
    GS_SET_DISPFB2(fbp, fbw, s_gs->PSM, 0, s_filter_shown);
    if (change && s_filter_shown) set_merge(1);
    s_gs->ActiveBuffer ^= 1;
    gsKit_setactive(s_gs);
}

void gfx_blend(int mode)
{
    if (mode == s_blend) return;
    s_blend = mode;
    if (mode == BLEND_ADD) /* Cs * As + Cd */
        gsKit_set_primalpha(s_gs, GS_SETREG_ALPHA(0, 2, 0, 1, 0), 0);
    else /* (Cs - Cd) * As + Cd */
        gsKit_set_primalpha(s_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

void gfx_tri(float x0, float y0, Color c0, float x1, float y1, Color c1, float x2, float y2, Color c2)
{
    x0 *= s_sx; x1 *= s_sx; x2 *= s_sx;
    y0 *= s_sy; y1 *= s_sy; y2 *= s_sy;
    if (c0 == c1 && c1 == c2)
        gsKit_prim_triangle_3d(s_gs, x0, y0, 0, x1, y1, 0, x2, y2, 0, gs_col(c0));
    else
        gsKit_prim_triangle_gouraud_3d(s_gs, x0, y0, 0, x1, y1, 0, x2, y2, 0, gs_col(c0), gs_col(c1), gs_col(c2));
}

void gfx_quad(const float *xy, const Color *c)
{
    /* perimeter order 0,1,2,3 -> strip order 0,1,3,2 */
    float x0 = xy[0] * s_sx, y0 = xy[1] * s_sy, x1 = xy[2] * s_sx, y1 = xy[3] * s_sy;
    float x2 = xy[4] * s_sx, y2 = xy[5] * s_sy, x3 = xy[6] * s_sx, y3 = xy[7] * s_sy;
    if (c[0] == c[1] && c[1] == c[2] && c[2] == c[3])
        gsKit_prim_quad_3d(s_gs, x0, y0, 0, x1, y1, 0, x3, y3, 0, x2, y2, 0, gs_col(c[0]));
    else
        gsKit_prim_quad_gouraud_3d(s_gs, x0, y0, 0, x1, y1, 0, x3, y3, 0, x2, y2, 0, gs_col(c[0]), gs_col(c[1]),
                                   gs_col(c[3]), gs_col(c[2]));
}

void gfx_rect(float x0, float y0, float x1, float y1, Color c)
{
    gsKit_prim_sprite(s_gs, x0 * s_sx, y0 * s_sy, x1 * s_sx, y1 * s_sy, 0, gs_col(c));
}

void gfx_rect_v(float x0, float y0, float x1, float y1, Color top, Color bottom)
{
    if (top == bottom) {
        gfx_rect(x0, y0, x1, y1, top);
        return;
    }
    x0 *= s_sx; x1 *= s_sx;
    y0 *= s_sy; y1 *= s_sy;
    gsKit_prim_quad_gouraud_3d(s_gs, x0, y0, 0, x1, y0, 0, x0, y1, 0, x1, y1, 0, gs_col(top), gs_col(top),
                               gs_col(bottom), gs_col(bottom));
}

void gfx_rect_h(float x0, float y0, float x1, float y1, Color left, Color right)
{
    if (left == right) {
        gfx_rect(x0, y0, x1, y1, left);
        return;
    }
    x0 *= s_sx; x1 *= s_sx;
    y0 *= s_sy; y1 *= s_sy;
    gsKit_prim_quad_gouraud_3d(s_gs, x0, y0, 0, x1, y0, 0, x0, y1, 0, x1, y1, 0, gs_col(left), gs_col(right),
                               gs_col(left), gs_col(right));
}
