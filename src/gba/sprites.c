/*
 * Sprites of a run (sprites.h): what render_level, play_render and fx_draw
 * draw on the PC, from the same state, with the sprites gba_tool drew with
 * those functions (gba_art_obj.c). What turns (the player, saws, the
 * orbs' dashes) is drawn turned at build time, a frame for each step
 * (art.h); scaling (the orbs' beat, spinning coins, rings) is the sprites'
 * affine matrices.
 */
#include <string.h>

#include "sprites.h"
#include "video.h"
#include "world.h"
#include "gen.h"
#include "fx.h"
#include "fx_gba.h"
#include "frame.h"
#include "hud.h"
#include "gba_draw.h"

static const uint16_t SHAPE[12] = {ATTR0_SQUARE, ATTR0_SQUARE, ATTR0_SQUARE, ATTR0_SQUARE,
                                   ATTR0_WIDE,   ATTR0_WIDE,   ATTR0_WIDE,   ATTR0_WIDE,
                                   ATTR0_TALL,   ATTR0_TALL,   ATTR0_TALL,   ATTR0_TALL};

/* the double-buffered tiles of the rings drawn this frame (OBJ tiles),
 * after the static ones */
#define RING_TILE (OBJ_STATIC_TILE + OBJ_STATIC_COUNT)
#define RING_MAX 4

#define TURN (65536.0f / (2.0f * PI))

static int floordiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static int s_prio = 1;

void spr_prio(int prio)
{
    s_prio = prio;
}

void spr(int x, int y, int size, int tile, int pal, uint16_t a0, uint16_t a1)
{
    if (x <= -64 || x >= SCR_W || y <= -64 || y >= SCR_H) return;
    video_obj((uint16_t)(ATTR0_Y(y) | SHAPE[size] | a0), (uint16_t)(ATTR1_X(x) | ATTR1_SIZE(size & 3) | a1),
              (uint16_t)(ATTR2_TILE(tile) | ATTR2_PRIO(s_prio) | ATTR2_PAL(pal)));
}

void spr_aff(int cx, int cy, int w, int h, int size, int tile, int pal, int aff, int dbl, uint16_t a0)
{
    if (aff < 0) return;
    if (dbl) spr(cx - w, cy - h, size, tile, pal, (uint16_t)(ATTR0_DOUBLE | a0), (uint16_t)ATTR1_AFF(aff));
    else spr(cx - w / 2, cy - h / 2, size, tile, pal, (uint16_t)(ATTR0_AFFINE | a0), (uint16_t)ATTR1_AFF(aff));
}

uint16_t spr_angle(float rad)
{
    /* to the nearest of the sine table's 1024 steps (video_sin): a quarter
     * turn (a cube on the ground) or nought (a ship flying level) must be
     * exactly that, or the picture is resampled a step off square, its
     * edges a pixel out of line, and truncating 16383.99 gives 16383 */
    return (uint16_t)((int32_t)floorf(rad * (TURN / 64.0f) + 0.5f) * 64);
}


/* ------------------------------------------------------------------ */
/* Glows                                                               */
/* ------------------------------------------------------------------ */

/* The PC's draw_glow (and the portals' and pads' like it): a light around
 * the player, the orbs, pads, portals and coins, added to what is behind
 * them. Here they are see-through sprites (the hardware adds them), of
 * colours GLOW_FIRST.. of the object's bank (gba_art_obj.c), put last so
 * that every other sprite is drawn over them: sprites_glows(). */
#define GLOW_MAX 24
typedef struct {
    int16_t x, y;
    uint16_t a0, a1, a2;
} Glow;
static Glow s_glows[GLOW_MAX];
static int s_nglows;
static float s_pulse; /* the beat's (beat_pulse), once a frame */
static int s_glow; /* see-through sprites add their light (not under the pause menu) */

static void glow(int x, int y, int size, int tile, int pal, uint16_t a0, uint16_t a1)
{
    Glow *g;
    if (!s_glow || s_nglows >= GLOW_MAX || x <= -64 || x >= SCR_W || y <= -64 || y >= SCR_H) return;
    g = &s_glows[s_nglows++];
    g->x = (int16_t)x;
    g->y = (int16_t)y;
    g->a0 = (uint16_t)(SHAPE[size] | ATTR0_BLEND | a0);
    g->a1 = (uint16_t)(ATTR1_SIZE(size & 3) | a1);
    g->a2 = (uint16_t)(ATTR2_TILE(tile) | ATTR2_PRIO(s_prio) | ATTR2_PAL(pal));
}

/* the round glow centred on (cx, cy), its radius 16 pixels scaled by
 * kx/256 across and ky/256 down (aff: the matrix for it, made once a frame
 * for all that share it) */
static void glow_disc(int cx, int cy, int pal, int *aff, int kx, int ky)
{
    if (!s_glow) return;
    if (kx == 256 && ky == 256) {
        glow(cx - 16, cy - 16, SQ32, OT_GLOW, pal, 0, 0);
        return;
    }
    if (*aff < 0) *aff = video_aff(0, kx, ky);
    if (*aff >= 0) glow(cx - 32, cy - 32, SQ32, OT_GLOW, pal, ATTR0_DOUBLE, (uint16_t)ATTR1_AFF(*aff));
}

void sprites_glows(void)
{
    int i;
    for (i = 0; i < s_nglows; i++) {
        const Glow *g = &s_glows[i];
        video_obj((uint16_t)(ATTR0_Y(g->y) | g->a0), (uint16_t)(ATTR1_X(g->x) | g->a1), g->a2);
    }
    s_nglows = 0;
}

/* ------------------------------------------------------------------ */
/* The player                                                          */
/* ------------------------------------------------------------------ */

static int s_icon = -1, s_col1 = -1, s_col2 = -1;
/* the frame each vehicle's place in VRAM holds (by MODE_*), -1 none yet */
static int s_vframe[MODE_COUNT];
/* and the saws' (big, small) */
static int s_saw_frame[2] = {-1, -1};
static int s_orb_frame = -1;

/* the ring picture's words that have any of it (the others stay 0 in
 * the rings' tiles) */
static uint8_t s_ring_words[16 * 8];
static int s_nring_words;

void sprites_init(void)
{
    int j;
    dma3_copy32(OBJ_TILES + OBJ_STATIC_TILE * 8, g_obj_tiles, OBJ_STATIC_COUNT * 8);
    dma3_fill32(OBJ_TILES + RING_TILE * 8, 0, RING_MAX * 2 * 16 * 8);
    for (j = 0; j < 16 * 8; j++)
        if (g_fx_ring[j]) s_ring_words[s_nring_words++] = (uint8_t)j;
    sprites_palettes();
}

void sprites_palettes(void)
{
    memcpy(g_pal_obj, g_obj_pals, sizeof(g_pal_obj));
    s_col1 = s_col2 = -1;
}

static uint16_t c15(Color c)
{
    return world_rgb15(c);
}

void sprites_garage(int icon, int col1, int col2)
{
    if (icon != s_icon) {
        int m;
        s_icon = icon;
        video_queue(OBJ_TILES + OBJ_PLAYER_TILE * 8, g_player_tiles + icon * PLAYER_TILES * 8, PLAYER_TILES * 8);
        video_queue(OBJ_TILES + (OBJ_PLAYER_TILE + PT_CUBE_STILL) * 8, g_player_tiles + (icon * PLAYER_TILES + PT_CUBE) * 8,
                    4 * 8);
        for (m = 0; m < MODE_COUNT; m++) s_vframe[m] = -1;
    }
    if (col1 != s_col1 || col2 != s_col2) {
        /* the parts icons.c draws: outline k, colour 1, its highlight (1.25),
         * colour 2, and the UFO's glass dome over them (0.35 white-blue) */
        Color k = RGB(8, 8, 12), c1 = g_player_colors[col1 % PLAYER_COLOR_COUNT];
        Color c2 = g_player_colors[col2 % PLAYER_COLOR_COUNT], hi = col_scale(c1, 1.25f);
        Color dome = RGB(200, 240, 255);
        uint16_t *p = g_pal_obj + OBJ_PAL_PLAYER * 16;
        s_col1 = col1;
        s_col2 = col2;
        p[PC_K] = c15(k);
        p[PC_C1] = c15(c1);
        p[PC_C1HI] = c15(hi);
        p[PC_C2] = c15(c2);
        p[PC_DOME] = c15(RGB(120, 150, 175));
        p[PC_K_DOME] = c15(col_lerp(k, dome, 0.35f));
        p[PC_C1_DOME] = c15(col_lerp(c1, dome, 0.35f));
        p[PC_C1HI_DOME] = c15(col_lerp(hi, dome, 0.35f));
        p[PC_C2_DOME] = c15(col_lerp(c2, dome, 0.35f));
    }
}

int spr_frame_turn(float angle, float period, int n)
{
    int f = (int)floorf(angle * ((float)n / period) + 0.5f) % n;
    return f < 0 ? f + n : f;
}

int spr_frame_range(float angle, float lo, float hi, int n)
{
    return clampi((int)floorf((angle - lo) * ((float)(n - 1) / (hi - lo)) + 0.5f), 0, n - 1);
}

void spr_vehicle(int mode, int x, int y, float angle, int flip)
{
    /* each vehicle's place in VRAM, its frames' first tile and size */
    static const uint8_t place[MODE_COUNT] = {PT_CUBE, PT_SHIP, PT_BALL, PT_UFO, PT_WAVE};
    static const uint16_t first[MODE_COUNT] = {VFT_CUBE, VFT_SHIP, VFT_BALL, VFT_UFO, VFT_WAVE};
    static const uint8_t size[MODE_COUNT] = {16, 32, 16, 32, 16};
    int f, n;
    mode = clampi(mode, 0, MODE_COUNT - 1);
    /* upside down: the frame of the opposite angle, mirrored (a mirror
     * turns the other way) */
    if (flip) angle = -angle;
    switch (mode) {
    case MODE_SHIP: f = spr_frame_range(angle, -PI / 2, PI / 2, VF_SHIP); break;
    case MODE_BALL: f = spr_frame_turn(angle, 2.0f * PI, VF_BALL); break;
    case MODE_UFO: f = spr_frame_range(angle, -VF_UFO_TILT, VF_UFO_TILT, VF_UFO); break;
    case MODE_WAVE: f = spr_frame_range(angle, -PI / 2, PI / 2, VF_WAVE); break;
    default: f = spr_frame_turn(angle, 2.0f * PI, VF_CUBE); break;
    }
    n = size[mode] * size[mode] / 64;
    if (f != s_vframe[mode] && s_icon >= 0 &&
        video_queue(OBJ_TILES + (OBJ_PLAYER_TILE + place[mode]) * 8,
                    g_player_frames + ((uint32_t)s_icon * VF_TILES + first[mode] + (uint32_t)f * n) * 8, (uint32_t)n * 8))
        s_vframe[mode] = f;
    spr(x - size[mode] / 2, y - size[mode] / 2, size[mode] == 32 ? SQ32 : SQ16, OBJ_PLAYER_TILE + place[mode],
        OBJ_PAL_PLAYER, 0, flip ? ATTR1_VFLIP : 0);
}

/* the saws' frame of the moment (all of one size turn together) */
static void saw_frame(int small, float angle)
{
    int f = spr_frame_turn(angle, PI / 6.0f, SAW_FRAMES), n = small ? 4 : 16;
    if (f != s_saw_frame[small] &&
        video_queue(OBJ_TILES + (small ? OT_SAW_SMALL : OT_SAW_BIG) * 8,
                    g_saw_frames + ((small ? SAW_FRAMES * 16 : 0) + f * n) * 8, (uint32_t)n * 8))
        s_saw_frame[small] = f;
}

/* the orbs' frame of the moment (their dashes turn: render_orb's time * 2) */
static void orb_frame(float angle)
{
    int f = spr_frame_turn(angle, PI * 0.5f, ORB_FRAMES);
    if (f != s_orb_frame && video_queue(OBJ_TILES + OT_ORB * 8, g_orb_frames + f * 4 * 8, 4 * 8)) s_orb_frame = f;
}

/* icons.c's vehicles: rotated by the run's rot (cube, ball) or tilt (the
 * rest), the ship and the UFO upside down with gravity flipped */
static void draw_player(const PlayState *ps)
{
    const Player *p = &ps->p;
    int x = (int)floorf(sim_x(p) * BLOCK_PIX + 0.5f) - g_cam_px;
    int y = (int)floorf(-sim_y(p) * BLOCK_PIX + 0.5f) - g_cam_py;
    int spin = p->mode == MODE_CUBE || p->mode == MODE_BALL;
    if (s_glow) {
        /* play_draw's glow: colour 1, 0.22 + 0.15 x the beat strong, over
         * 1.3 blocks (the disc's 16 pixels) */
        Color c = g_player_colors[g_game.save.col1 % PLAYER_COLOR_COUNT];
        int a = (int)((0.22f + 0.15f * s_pulse) * 256.0f), k;
        uint16_t *pal = g_pal_obj + OBJ_PAL_PLAYER * 16 + GLOW_FIRST;
        for (k = 0; k < GLOW_LEVELS; k++) {
            int m = a * (2 * k + 1) / (2 * GLOW_LEVELS);
            pal[k] = (uint16_t)(((int)COL_R(c) * m + 1024) >> 11 | (((int)COL_G(c) * m + 1024) >> 11) << 5 |
                                (((int)COL_B(c) * m + 1024) >> 11) << 10);
        }
        glow(x - 16, y - 16, SQ32, OT_GLOW, OBJ_PAL_PLAYER, 0, 0);
    }
    spr_vehicle(p->mode, x, y, spin ? ps->rot : ps->vis_angle, p->grav < 0 && (p->mode == MODE_SHIP || p->mode == MODE_UFO));
}

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */

/* the particles' palette, filled as they are drawn */
static uint16_t s_fxc[16];
static int s_nfx, s_nring, s_frame;

/* a particle's colour, k/4 of it (added light fades in quarters), as the
 * GBA's 15 bits (rounded as world_rgb15 rounds) */
static uint16_t fx15(Color c, int k)
{
    int r = ((int)COL_R(c) * k / 4 + 4) >> 3, g = ((int)COL_G(c) * k / 4 + 4) >> 3, b = ((int)COL_B(c) * k / 4 + 4) >> 3;
    return (uint16_t)((r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10);
}

/* the particles' palette index for a colour (the nearest once all 15 are taken) */
IWRAM_CODE static int fx_color(uint16_t v)
{
    int i, best = 1, bd = 1 << 30;
    for (i = 1; i <= s_nfx; i++)
        if (s_fxc[i] == v) return i;
    if (s_nfx < 15) {
        s_fxc[++s_nfx] = v;
        g_pal_obj[OBJ_PAL_FX * 16 + s_nfx] = v;
        return s_nfx;
    }
    for (i = 1; i <= 15; i++) {
        int dr = (s_fxc[i] & 31) - (v & 31), dg = ((s_fxc[i] >> 5) & 31) - ((v >> 5) & 31),
            db = ((s_fxc[i] >> 10) & 31) - ((v >> 10) & 31);
        int d = dr * dr + dg * dg + db * db;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void fx_begin(void)
{
    s_nfx = 0;
    s_nring = 0;
    s_frame++;
}

/* one particle (its shape, added light or not, colour) at screen (x, y):
 * its size in pixels (8.8 fixed point), how far through its life it is
 * (0..256) */
IWRAM_CODE static void fx_one(int shape, int add, Color c, int x, int y, int size8, int t8)
{
    int idx, k = 4;
    uint16_t blend = 0;
    if (add && s_glow) {
        /* added light: darker is fainter, (1 - t^2) in quarters, rounded up */
        int a = 256 - (t8 * t8 >> 8);
        k = (a * 4 + 253) >> 8;
        blend = ATTR0_BLEND;
    }
    idx = fx_color(fx15(c, k));
    if (shape == FX_RING) {
        /* the ring picture's radius is 15 pixels; its colour is copied in */
        int n, j, aff;
        volatile uint32_t *dst;
        if (s_nring >= RING_MAX) return;
        n = RING_TILE + ((s_frame & 1) * RING_MAX + s_nring) * 16;
        dst = OBJ_TILES + n * 8;
        for (j = 0; j < s_nring_words; j++) dst[s_ring_words[j]] = g_fx_ring[s_ring_words[j]] * (uint32_t)idx;
        s_nring++;
        aff = video_aff(0, size8 / 15 + 1, size8 / 15 + 1);
        spr_aff(x, y, 32, 32, SQ32, n, OBJ_PAL_FX, aff, 1, blend);
        return;
    }
    {
        int s = (size8 + 128) >> 8;
        if (s < 1) s = 1;
        if (s > 8) s = 8;
        spr(x - 4, y - 4, SQ8, OT_FX + (idx - 1) * 16 + (shape == FX_CIRCLE ? 8 : 0) + s - 1, OBJ_PAL_FX, blend, 0);
    }
}

/* (fx_gba.c moves them in fixed point, in pixels: no floats here) */
IWRAM_CODE void sprites_fx(int space)
{
    int n, i;
    const Particle *ps = fx_particles(&n);
    int ox = space == FX_WORLD ? g_cam_px : 0, oy = space == FX_WORLD ? g_cam_py : 0;
    for (i = 0; i < n; i++) {
        const Particle *q = &ps[i];
        const FxFix *f = &g_fx[i];
        int x, y, t8;
        if (!q->active || q->space != space) continue;
        if (q->active == 1) f = fx_fix(i); /* (spawned since the tick's fx_update) */
        x = (f->x >> 16) - ox;
        y = (f->y >> 16) - oy;
        if (x < -40 || x > SCR_W + 40 || y < -40 || y > SCR_H + 40) continue;
        t8 = fx_t8(f);
        fx_one(q->shape, q->add, q->c, x, y, f->s0 + ((f->s1 - f->s0) * t8 >> 8), t8);
    }
}

/* the wave's trail: dots along the last positions, fading towards its end */
static void draw_trail(const PlayState *ps)
{
    int i, n = ps->trail_n;
    Color c2 = g_player_colors[g_game.save.col2 % PLAYER_COLOR_COUNT];
    /* (the sizes and fades in integers, a0 = (n - i) / n: in floats this
     * took a busy Wave Rider frame 10 scanlines) */
    for (i = 1; i < n; i += 2) {
        int r = (9 * 256 * (n - i) / n + 2 * 256) * BLOCK_PIX / (int)BLOCK_PX;
        fx_one(FX_CIRCLE, 1, c2, (int)(ps->trail_x[i] * BLOCK_PIX) - g_cam_px,
               (int)(-ps->trail_y[i] * BLOCK_PIX) - g_cam_py, r, 256 * i / n);
    }
}

/* ------------------------------------------------------------------ */
/* The level's objects                                                 */
/* ------------------------------------------------------------------ */

static void draw_objects(const PlayState *ps, int back, uint8_t saved_coins)
{
    const Level *L = ps->L;
    const Game *g = &g_game;
    float time = g->t, pulse = s_pulse;
    int c0 = clampi(floordiv(g_cam_px, BLOCK_PIX) - 2, 0, L->width);
    int c1 = clampi(floordiv(g_cam_px + SCR_W, BLOCK_PIX) + 3, 0, L->width);
    int i, orb_aff = -1, glow_aff[3] = {-1, -1, -1};
    /* the glows' sizes: render_orb's 2.6 x 0.3 blocks (beating), the
     * portals' 0.96 x 1.81, render_coin's 2.4 x 0.42, as the disc's 16 */
    int orb_k = (int)(BLOCK_PIX * 0.78f * 16.0f * (1.0f + 0.12f * pulse));
    for (i = L->col_start[c0]; i < L->col_start[c1]; i++) {
        const LevelObj *o = &L->objs[i];
        int t = o->type;
        int x = o->cx * BLOCK_PIX + BLOCK_PIX / 2 - g_cam_px, y = -o->cy * BLOCK_PIX - BLOCK_PIX / 2 - g_cam_py;
        int is_back = (t >= OBJ_PORTAL_CUBE && t <= OBJ_SPEED_3) || (t >= OBJ_PAD_YELLOW && t <= OBJ_PAD_BLUE);
        if (is_back != back || y < -48 || y > SCR_H + 48) continue;
        switch (t) {
        case OBJ_ORB_YELLOW: case OBJ_ORB_PINK: case OBJ_ORB_BLUE: case OBJ_ORB_GREEN:
            /* a used orb fades on the PC: here it shows every other frame */
            if (sim_used(&ps->p, o->id) && (g_frames.vblanks & 1)) break;
            if (orb_aff < 0) {
                /* (turned in their frames; the beat swells them) */
                int s = (int)((1.0f + 0.12f * pulse) * 256.0f);
                orb_frame(time * 2.0f);
                orb_aff = video_aff(0, s, s);
            }
            spr_aff(x, y, 16, 16, SQ16, OT_ORB, OBJ_PAL_ORB + t - OBJ_ORB_YELLOW, orb_aff, 0, 0);
            glow_disc(x, y, OBJ_PAL_ORB + t - OBJ_ORB_YELLOW, &glow_aff[0], orb_k, orb_k);
            break;
        case OBJ_PAD_YELLOW: case OBJ_PAD_PINK: case OBJ_PAD_BLUE:
            /* (render_pad's glow: a column from the pad, away from the surface) */
            if (o->flags & OF_CEILING) {
                spr(x - 8, y - 8, W16x8, OT_PAD, OBJ_PAL_ORB + t - OBJ_PAD_YELLOW, 0, ATTR1_VFLIP);
                glow(x - 8, y - 4, SQ16, OT_GLOW_PAD, OBJ_PAL_ORB + t - OBJ_PAD_YELLOW, 0, ATTR1_VFLIP);
            } else {
                spr(x - 8, y, W16x8, OT_PAD, OBJ_PAL_ORB + t - OBJ_PAD_YELLOW, 0, 0);
                glow(x - 8, y - 12, SQ16, OT_GLOW_PAD, OBJ_PAL_ORB + t - OBJ_PAD_YELLOW, 0, 0);
            }
            break;
        case OBJ_PORTAL_CUBE: case OBJ_PORTAL_SHIP: case OBJ_PORTAL_BALL: case OBJ_PORTAL_UFO:
        case OBJ_PORTAL_WAVE: case OBJ_PORTAL_GRAV_FLIP: case OBJ_PORTAL_GRAV_NORMAL: {
            /* the highlight goes round at 3 radians a second */
            int f = (int)(time * (3.0f * PORTAL_FRAMES / (2.0f * PI))) % PORTAL_FRAMES;
            int pal = OBJ_PAL_PORTAL + t - OBJ_PORTAL_CUBE;
            spr(x - 8, y - 8, SQ16, OT_PORTAL_SYM + (t - OBJ_PORTAL_CUBE) * 4, pal, 0, 0);
            spr(x - 8, y - 24, T16x32, OT_PORTAL + f * PORTAL_FRAME_TILES, pal, 0, 0);
            spr(x - 8, y + 8, SQ16, OT_PORTAL + f * PORTAL_FRAME_TILES + 8, pal, 0, 0);
            glow_disc(x, y, pal, &glow_aff[1], 184, 348);
            break;
        }
        case OBJ_SPEED_0: case OBJ_SPEED_1: case OBJ_SPEED_2: case OBJ_SPEED_3:
            spr(x - 8, y - 8, SQ16, OT_SPEED + (t - OBJ_SPEED_0) * 4, OBJ_PAL_SPEED, 0, 0);
            break;
        case OBJ_COIN: {
            int ghost = (saved_coins >> ((o->flags >> 4) & 3)) & 1;
            float spin = time * 2.5f + o->cx;
            int s;
            if (sim_used(&ps->p, o->id)) break;
            s = (int)((0.15f + 0.85f * fabsf(tcosf(spin))) * 256.0f);
            spr_aff(x, y, 16, 16, SQ16, ghost ? OT_COIN_GHOST : OT_COIN, OBJ_PAL_SPEED, video_aff(0, s, 256), 0, 0);
            if (!ghost) glow_disc(x, y, OBJ_PAL_SPEED, &glow_aff[2], 194, 194);
            break;
        }
        case OBJ_SAW_BIG:
            saw_frame(0, time * 5.0f);
            spr(x - 16, y - 16, SQ32, OT_SAW_BIG, OBJ_PAL_MISC, 0, 0);
            glow(x - 16, y - 16, SQ32, OT_SAW_GLOW_BIG, OBJ_PAL_MISC, 0, 0);
            break;
        case OBJ_SAW_SMALL:
            saw_frame(1, -time * 7.0f);
            spr(x - 8, y - 8, SQ16, OT_SAW_SMALL, OBJ_PAL_MISC, 0, 0);
            glow(x - 8, y - 8, SQ16, OT_SAW_GLOW_SMALL, OBJ_PAL_MISC, 0, 0);
            break;
        default:
            break;
        }
    }
}

/* the finish line: a white line with a glow that fades out to either side */
static void draw_gate(const PlayState *ps)
{
    int ex = ps->L->width * BLOCK_PIX - g_cam_px, y;
    if (ex < -40 || ex > SCR_W + 40) return;
    for (y = 0; y < SCR_H; y += 64) {
        spr(ex - 32, y, T32x64, OT_GATE, OBJ_PAL_MISC, s_glow ? ATTR0_BLEND : 0, 0);
        spr(ex, y, T32x64, OT_GATE, OBJ_PAL_MISC, s_glow ? ATTR0_BLEND : 0, ATTR1_HFLIP);
    }
}

typedef char ring_tiles_fit[RING_TILE + RING_MAX * 2 * 16 <= OBJ_TEXT_TILE ? 1 : -1];

void sprites_run(const PlayState *ps, int show_player)
{
    const Game *g = &g_game;
    uint8_t saved = ps == &g->play && ps->level_idx < SAVE_MAX_LEVELS ? g->save.progress.coins[ps->level_idx] : 0;
    int i;
    /* (no see-through sprites under the pause menu, from the frame it is
     * shown: its panel darkens what is behind it by the hardware's effect,
     * which sprites that add their light are left out of. A fade is done
     * in the palettes, theirs too: they darken with the rest.) */
    s_glow = !(ps == &g->play && hud_pause_shown());
    s_nglows = 0;
    s_pulse = g_frame_pulse;
    fx_begin();
    /* the saws' outline is the level's, lit on the beat; their glow the
     * blocks' (0.4 of it at its strongest: about what it is beyond their
     * teeth) */
    g_pal_obj[OBJ_PAL_MISC * 16 + MISC_EDGE] = g_pal_bg[WC_EDGE];
    g_pal_obj[OBJ_PAL_MISC * 16 + MISC_GLOW] = world_rgb15(col_scale(world_halo(), 0.4f));
    sprites_fx(FX_WORLD);
    if (show_player) {
        if (ps->p.mode == MODE_WAVE) draw_trail(ps);
        draw_player(ps);
    }
    if (ps->practice)
        for (i = 0; i < ps->ncp; i++)
            spr((int)(sim_x(&ps->cp[i].p) * BLOCK_PIX) - g_cam_px - 8,
                (int)(-sim_y(&ps->cp[i].p) * BLOCK_PIX) - g_cam_py - 8, SQ16, OT_CHECKPOINT, OBJ_PAL_MISC, 0, 0);
    if (!ps->L) return;
    draw_objects(ps, 0, saved);
    draw_objects(ps, 1, saved);
    draw_gate(ps);
}
