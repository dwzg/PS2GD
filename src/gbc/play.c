/*
 * Playing a level.
 *
 * The screen: row 0 is the progress bar, level rows 14..0 are background
 * rows 1..15 (a block is a tile), rows 16-17 the ground. The background map
 * is 32 tiles wide and scrolls; a new column of the level is drawn into it
 * just before it comes into view. The camera keeps the player 45 pixels in
 * from the left (5.65 blocks, as on the other versions) and never moves up
 * or down: a whole level is 10 rows high.
 */
#include <string.h>

#include "gbc.h"

#define PLAYER_SX 45 /* the player's x on screen */
#define GROUND_SY 128 /* screen y of the ground's surface */
#define MAX_CP 8
#define TRAIL 10

enum { PH_RUN = 0, PH_DEAD, PH_RESPAWN, PH_COMPLETE };

/* OAM slots */
#define OAM_PLAYER 0
#define OAM_PART 2
#define OAM_TRAIL 10
#define OAM_CHECK 20
#define OAM_RING 24
#define NPART 8
#define NCHECK 4

typedef struct {
    GsPlayer p;
    uint8_t trig, pal_from, pal_to, pal_t;
    uint16_t rot;
} Snap;

/* for the emulator test: the phase, and frames that took longer than a
 * frame while running */
uint8_t g_phase;
uint16_t g_dropped;
GsPlayer g_snap; /* the player after the last tick (frames end mid-tick) */
static uint8_t s_vbl_start;
extern volatile uint8_t g_vbl_count;

static const GbLevel *L;
static uint8_t s_level, s_practice, s_quit;
static uint8_t s_t;
static uint16_t s_attempts, s_jumps_total;
static uint32_t s_ticks_total;
static int16_t s_cam, s_col;
static uint8_t s_trig, s_pal_from, s_pal_to, s_pal_t;
static uint8_t s_flash, s_fade, s_pal_dirty, s_beat;
static uint16_t s_rot;
static int8_t s_ship_f;
static Snap s_cp[MAX_CP];
static uint8_t s_ncp;
static uint8_t s_bar_fill[12], s_pc = 0xff;
static uint8_t s_paused;
static uint8_t s_new_best, s_popup_t;
static int16_t s_part_x[NPART], s_part_y[NPART]; /* 1/16 px, screen */
static int8_t s_part_vx[NPART], s_part_vy[NPART];
static uint8_t s_part_t;
static int16_t s_trail_x[TRAIL]; /* world px */
static uint8_t s_trail_y[TRAIL], s_trail_n, s_trail_i;
static int16_t s_ring_x;
static uint8_t s_ring_y, s_ring_t;
static Snap s_start;

/* smoothstep over the 48 frames (0.8 s) of a palette change */
static const uint8_t SMOOTH[49] = {0,   0,   1,   3,   5,   8,   11,  15,  19,  24,  29,  34,  40,  46,  53,  59,  66,
                                   74,  81,  89,  96,  104, 112, 120, 128, 136, 144, 152, 160, 167, 175, 182, 190, 197,
                                   203, 210, 216, 222, 227, 232, 237, 241, 245, 248, 251, 253, 255, 255, 255};

/* a ball turns a quarter in 0.45 blocks: per tick at each speed (1/256 of a quarter) */
static const uint8_t BALL_SPIN[4] = {50, 63, 79, 95};
/* ship tilt steps: |vy| against speed * 1.6 * tan(5, 15, 25 degrees) */
static const uint16_t SHIP_TILT[4][3] = {
    {321, 983, 1711}, {401, 1229, 2139}, {502, 1536, 2674}, {602, 1843, 3208}};

/* death burst directions (1/16 px a frame) */
static const int8_t PART_VX[NPART] = {40, 28, 0, -28, -40, -28, 0, 28};
static const int8_t PART_VY[NPART] = {0, -28, -40, -28, 0, 28, 40, 28};

/* 16.16 blocks to pixels (8 a block), without a 13-step shift loop */
static int16_t px_of(uint32_t x)
{
    return (int16_t)(((uint16_t)(x >> 16) << 3) | ((uint8_t)(x >> 8) >> 5));
}
static int16_t py_of(int32_t y) { return GROUND_SY - px_of((uint32_t)y); }
static int16_t col_of(int16_t px) { return (int16_t)((px + 256) >> 3) - 32; } /* floor, px >= -256 */

/* --- the background --- */

static void build_column(int16_t c, uint8_t *t, uint8_t *a)
{
    static const uint8_t COIN_BIT[4] = {1, 2, 4, 8};
    uint8_t r = COL_ROWS, tile;
    /* filled from the bottom (level row 0) up */
    t += COL_ROWS;
    a += COL_ROWS;
    if (c >= 0 && (uint16_t)c < L->width) {
        const uint8_t *cells = L->cells + ((uint16_t)c << 4);
        do {
            tile = *cells++;
            if ((uint8_t)(tile - T_COIN) < 4 && (gs_p.coins & COIN_BIT[tile - T_COIN])) tile = T_EMPTY;
            *--t = tile;
            *--a = gfx_bg_attr[tile];
        } while (--r);
    } else {
        do {
            *--t = T_EMPTY;
            *--a = gfx_bg_attr[T_EMPTY];
        } while (--r);
    }
}

/* Draw the columns coming into view, at most max of them. */
static void stream(uint8_t max)
{
    uint8_t t[COL_ROWS], a[COL_ROWS];
    /* (a column ahead of the screen's last: drawing can wait a frame) */
    int16_t want = col_of(s_cam) + 22;
    while (s_col <= want && max && col_queue_free()) {
        build_column(s_col, t, a);
        col_queue((uint8_t)(s_col & 31), t, a);
        s_col++;
        max--;
    }
}

/* column where the level's percentage reaches k: ceil(k * width / 100) */
static uint16_t s_pc_col[101];
static uint8_t s_pc_now;

static void hud_thresholds(void)
{
    /* (k * width + 99) / 100, a step at a time: quotient and remainder */
    uint16_t q = L->width / 100, r = L->width % 100, Q = 0, R = 99;
    uint8_t k;
    for (k = 0; k <= 100; k++) {
        s_pc_col[k] = Q;
        Q += q;
        R += r;
        if (R >= 100) {
            Q++;
            R -= 100;
        }
    }
    s_pc_now = 0;
}

static void hud_update(void)
{
    uint16_t col = (uint16_t)(gs_p.x >> 16);
    uint8_t fill, i;
    if (col < s_pc_col[s_pc_now]) s_pc_now = 0; /* back at the start or a checkpoint */
    while (s_pc_now < 100 && col >= s_pc_col[s_pc_now + 1]) s_pc_now++;
    if (s_pc_now == s_pc) return;
    s_pc = s_pc_now;
    fill = (uint8_t)(((uint16_t)s_pc * 123) >> 7); /* 96 pixels for 100% */
    for (i = 0; i < 12; i++) {
        uint8_t f = fill > i * 8 ? (fill - i * 8 > 8 ? 8 : fill - i * 8) : 0;
        if (f != s_bar_fill[i]) {
            cell_queue(3 + i, 0, UT_BAR + f, PAL_HUD | 0x08);
            s_bar_fill[i] = f;
        }
    }
    {
        char buf[6];
        uint8_t n;
        fmt_uint(buf, s_pc);
        n = (uint8_t)strlen(buf);
        for (i = 0; i < 3; i++) cell_queue(16 + i, 0, i < 3 - n ? UT_BLANK : UT_FONT + buf[i - (3 - n)] - 32, PAL_HUD | 0x08);
        cell_queue(19, 0, UT_FONT + '%' - 32, PAL_HUD | 0x08);
    }
}

/* --- palettes --- */

static void palettes(void)
{
    while (s_trig < L->ntrig && (gs_p.x >> 16) >= L->trig[s_trig].x) {
        s_pal_from = s_pal_to;
        s_pal_to = L->trig[s_trig].pal;
        s_pal_t = 0;
        s_trig++;
    }
    /* a palette change moves on every other frame (the flash fades on
     * the frames between): blending is the expensive part */
    if (s_pal_t < 48 && !(g_frame & 1)) {
        s_pal_t += 2;
        s_pal_dirty = 1;
    }
    if (s_beat && g_phase == PH_RUN) {
        s_flash = 9;
        s_pal_dirty = 1;
    } else if (s_flash && (g_frame & 1)) {
        s_flash--;
        s_pal_dirty = 1;
    }
    s_beat = 0;
    if (s_pal_dirty) {
        pal_level(s_pal_from, s_pal_to, SMOOTH[s_pal_t], s_flash, s_fade);
        s_pal_dirty = 0;
    }
}

/* --- sprites --- */

static void put16(uint8_t oam, uint8_t tile, int16_t sx, int16_t sy, uint8_t prop)
{
    uint8_t left = (prop & S_FLIPX) ? 2 : 0;
    if (sx < -8 || sx > 168 || sy < -8 || sy > 152) {
        hide_sprite(oam);
        hide_sprite(oam + 1);
        return;
    }
    set_sprite_tile(oam, tile + left);
    set_sprite_tile(oam + 1, tile + (2 - left));
    set_sprite_prop(oam, prop);
    set_sprite_prop(oam + 1, prop);
    move_sprite(oam, (uint8_t)sx, (uint8_t)(sy + 8));
    move_sprite(oam + 1, (uint8_t)(sx + 8), (uint8_t)(sy + 8));
}

static void draw_player(void)
{
    int16_t sx = px_of(gs_p.x) - s_cam, sy = py_of(gs_p.y);
    uint8_t tile, prop = OPAL_PLAYER | 0x08, f;
    int8_t g = gs_p.grav;
    int16_t vy = gs_p.vy;
    if (g_phase == PH_DEAD || (g_phase == PH_RESPAWN && s_t < 8)) {
        hide_sprite(OAM_PLAYER);
        hide_sprite(OAM_PLAYER + 1);
        return;
    }
    switch (gs_p.mode) {
    case GM_SHIP: {
        int16_t v = g > 0 ? vy : -vy, av = v < 0 ? -v : v;
        const uint16_t *th = SHIP_TILT[gs_p.speed_idx];
        int8_t k = av >= th[2] ? 3 : av >= th[1] ? 2 : av >= th[0] ? 1 : 0;
        int8_t target = 3 + (v > 0 ? -k : k);
        if (s_ship_f < target) s_ship_f++;
        else if (s_ship_f > target) s_ship_f--;
        tile = ST_SHIP + 4 * s_ship_f;
        if (g < 0) prop |= S_FLIPY;
        break;
    }
    case GM_BALL:
        f = (uint8_t)(((s_rot & 255) + 32) >> 6) & 3;
        tile = ST_BALL + 4 * f;
        break;
    case GM_UFO: {
        int16_t v = g > 0 ? vy : -vy;
        f = v > 1365 ? 0 : v < -1365 ? 2 : 1;
        tile = ST_UFO + 4 * f;
        if (g < 0) prop |= S_FLIPY;
        break;
    }
    case GM_WAVE:
        f = vy > 0 ? 0 : vy < 0 ? 2 : 1;
        tile = ST_WAVE + 4 * f;
        break;
    default:
        f = (uint8_t)(((uint16_t)(s_rot & 255) * 6 + 128) >> 8);
        if (f >= CUBE_FRAMES) f = 0;
        tile = ST_CUBE + 4 * f;
        break;
    }
    put16(OAM_PLAYER, tile, sx, sy, prop);
}

/* the cube spins in the air and settles on a side when it lands */
static void spin(void)
{
    int8_t g = gs_p.grav;
    if (gs_p.mode == GM_CUBE) {
        if (gs_p.grounded) {
            uint8_t r = (uint8_t)s_rot;
            if (r) {
                if (r < 128) s_rot -= r < 49 ? r : 49;
                else s_rot += (uint8_t)(256 - r) < 49 ? (uint8_t)(256 - r) : 49;
            }
        } else {
            s_rot += g > 0 ? 20 : -20;
        }
    } else if (gs_p.mode == GM_BALL) {
        s_rot += g > 0 ? BALL_SPIN[gs_p.speed_idx] : -BALL_SPIN[gs_p.speed_idx];
    }
}

/* The wave's trail: where it was on the last frames, as dots. */
static uint8_t s_trail_shown;

static void draw_trail(void)
{
    uint8_t i, k, n = 0;
    if (gs_p.mode == GM_WAVE && g_phase == PH_RUN) {
        s_trail_x[s_trail_i] = px_of(gs_p.x);
        s_trail_y[s_trail_i] = (uint8_t)(py_of(gs_p.y));
        if (++s_trail_i == TRAIL) s_trail_i = 0;
        if (s_trail_n < TRAIL) s_trail_n++;
        /* newest first, skipping the one under the player */
        k = s_trail_i;
        for (i = 1; i < s_trail_n; i++) {
            k = k ? k - 1 : TRAIL - 1;
            if (i == 1) continue;
            set_sprite_tile(OAM_TRAIL + n, ST_DOT);
            set_sprite_prop(OAM_TRAIL + n, OPAL_FX | 0x08);
            move_sprite(OAM_TRAIL + n, (uint8_t)(s_trail_x[k] - s_cam + 7), (uint8_t)(s_trail_y[k] + 15));
            n++;
        }
    } else if (g_phase == PH_RUN) {
        s_trail_n = 0;
    } else {
        return; /* frozen while dead */
    }
    for (i = n; i < s_trail_shown; i++) hide_sprite(OAM_TRAIL + i);
    s_trail_shown = n;
}

static void draw_checkpoints(void)
{
    uint8_t i, n = 0;
    for (i = s_ncp; i > 0 && n < NCHECK; i--) {
        const GsPlayer *p = &s_cp[i - 1].p;
        int16_t sx = px_of(p->x) - s_cam, sy = py_of(p->y);
        if (sx < -8 || sx > 168) continue;
        set_sprite_tile(OAM_CHECK + n, ST_CHECK);
        set_sprite_prop(OAM_CHECK + n, OPAL_CHECK | 0x08);
        move_sprite(OAM_CHECK + n, (uint8_t)(sx + 5), (uint8_t)(sy + 13));
        n++;
    }
    for (; n < NCHECK; n++) hide_sprite(OAM_CHECK + n);
}

static void draw_effects(void)
{
    uint8_t i;
    if (s_part_t) {
        s_part_t--;
        for (i = 0; i < NPART; i++) {
            s_part_x[i] += s_part_vx[i];
            s_part_y[i] += s_part_vy[i];
            if (s_part_t) {
                set_sprite_tile(OAM_PART + i, (s_part_t < 12 || (i & 1)) ? ST_PART_SM : ST_PART);
                set_sprite_prop(OAM_PART + i, OPAL_FX | 0x08);
                move_sprite(OAM_PART + i, (uint8_t)((s_part_x[i] >> 4) + 8), (uint8_t)((s_part_y[i] >> 4) + 16));
            } else {
                hide_sprite(OAM_PART + i);
            }
        }
    }
    if (s_ring_t) {
        s_ring_t--;
        put16(OAM_RING, s_ring_t > 4 ? ST_RING : ST_RING + 4, s_ring_x - s_cam, s_ring_y, OPAL_FX | 0x08);
        if (!s_ring_t) {
            hide_sprite(OAM_RING);
            hide_sprite(OAM_RING + 1);
        }
    }
}

static void burst(int16_t sx, int16_t sy, uint8_t frames)
{
    uint8_t i;
    for (i = 0; i < NPART; i++) {
        s_part_x[i] = (sx - 1) << 4;
        s_part_y[i] = (sy - 1) << 4;
        s_part_vx[i] = PART_VX[i];
        s_part_vy[i] = PART_VY[i];
    }
    s_part_t = frames;
}

static void hide_all_sprites(void)
{
    uint8_t i;
    for (i = 0; i < 40; i++) hide_sprite(i);
    s_part_t = s_ring_t = 0;
    s_trail_n = 0;
    s_trail_shown = 0;
}

/* --- attempts --- */

static void take_snap(Snap *s)
{
    s->p = gs_p;
    s->trig = s_trig;
    s->pal_from = s_pal_from;
    s->pal_to = s_pal_to;
    s->pal_t = s_pal_t;
    s->rot = s_rot;
}

/* Start an attempt (from a checkpoint in practice): the screen is dark,
 * the columns are drawn over the next few frames. */
static void restore(const Snap *s)
{
    gs_p = s->p;
    g_snap = gs_p;
    s_trig = s->trig;
    s_pal_from = s->pal_from;
    s_pal_to = s->pal_to;
    s_pal_t = s->pal_t;
    s_rot = s->rot;
    s_ship_f = 3;
    s_cam = px_of(gs_p.x) - PLAYER_SX;
    s_col = col_of(s_cam);
    g_scx = (uint8_t)s_cam;
    s_attempts++;
    s_pc = 0xff;
    hide_all_sprites();
}

static uint8_t level_pc(void)
{
    hud_update();
    return s_pc_now;
}

static void on_death(void)
{
    uint8_t pc = level_pc();
    int16_t sx = px_of(gs_p.x) - s_cam, sy = py_of(gs_p.y);
    sfx_play(SFX_DEATH);
    burst(sx, sy, 30);
    g_phase = PH_DEAD;
    s_t = 0;
    s_ticks_total += gs_p.ticks;
    s_jumps_total += gs_p.jumps;
    if (g_save.attempts[s_level] < 65535) g_save.attempts[s_level]++;
    s_new_best = 0;
    if (s_practice) {
        if (pc > g_save.best_practice[s_level]) g_save.best_practice[s_level] = pc;
    } else if (pc > g_save.best[s_level]) {
        g_save.best[s_level] = pc;
        s_new_best = pc;
    }
    save_write();
    if (s_new_best) {
        ui_new_best(s_new_best);
        s_popup_t = 50;
    }
    if (!s_practice) music_stop();
}

static void on_complete(void)
{
    sfx_play(SFX_COMPLETE);
    g_phase = PH_COMPLETE;
    s_t = 0;
    s_ticks_total += gs_p.ticks;
    s_jumps_total += gs_p.jumps;
    if (s_practice) {
        g_save.best_practice[s_level] = 100;
    } else {
        g_save.best[s_level] = 100;
        g_save.coins[s_level] |= gs_p.coins;
    }
    save_write();
    burst(PLAYER_SX, 60, 40);
}

/* --- the frame --- */

static void run_tick(void)
{
    uint8_t held = (g_keys & (J_A | J_UP)) != 0, pressed = (g_pressed & (J_A | J_UP)) != 0;
    uint16_t ev;
    if (s_practice) {
        if ((g_pressed & J_B) && !gs_p.dead) {
            if (s_ncp == MAX_CP) {
                memmove(&s_cp[0], &s_cp[1], sizeof(Snap) * (MAX_CP - 1));
                s_ncp--;
            }
            take_snap(&s_cp[s_ncp++]);
            sfx_play(SFX_CHECKPOINT);
        }
        if ((g_pressed & J_SELECT) && s_ncp) {
            s_ncp--;
            sfx_play(SFX_BACK);
        }
    }
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        gs_step(held, pressed);
        PERF_END(PERF_SIM, t);
    }
    g_snap = gs_p;
    ev = gs_p.events;
    spin();
    if (ev & (GE_ORB | GE_PAD)) {
        /* a ring around the object's cell centre */
        s_ring_x = (int16_t)(gs_p.ev_cell >> 4) * 8 + 4;
        s_ring_y = (uint8_t)(GROUND_SY - (gs_p.ev_cell & 15) * 8 - 4);
        s_ring_t = 8;
    }
    if (ev & GE_COIN) {
        int16_t c = (int16_t)(gs_p.ev_cell >> 4);
        cell_queue((uint8_t)(c & 31), (uint8_t)(15 - (gs_p.ev_cell & 15)), T_EMPTY, gfx_bg_attr[T_EMPTY]);
        sfx_play(SFX_COIN);
    }
    if (gs_p.dead) on_death();
    else if (gs_p.done) on_complete();
}

/* Scanlines since this frame's vertical blank began (255: the next one
 * has begun already). */
static uint8_t frame_lines(void)
{
    uint8_t ly = LY_REG;
    if (g_vbl_count != s_vbl_start) return 255;
    return ly >= 144 ? (uint8_t)(ly - 144) : (uint8_t)(ly + 10);
}

static void play_frame(void)
{
    s_beat |= music_beat; /* for palettes(), which may run a frame later */
    if (s_paused) {
        if (g_pressed & (J_A | J_SELECT | J_B)) {
            s_paused = 0;
            HIDE_WIN;
            SHOW_SPRITES;
        }
        if (g_pressed & J_A) {
            music_pause(0);
        } else if (g_pressed & J_SELECT) {
            s_ncp = 0;
            s_attempts = 0;
            s_ticks_total = 0;
            s_jumps_total = 0;
            g_phase = PH_RESPAWN;
            s_t = 0;
            music_pause(0);
        } else if (g_pressed & J_B) {
            if (g_phase == PH_RUN && gs_p.ticks > 30) {
                gs_p.dead = 1;
                on_death();
            }
            s_quit = 1;
        }
        return;
    }
    if ((g_pressed & J_START) && g_phase == PH_RUN) {
        s_paused = 1;
        music_pause(1);
        sfx_play(SFX_SELECT);
        HIDE_SPRITES; /* they would cover the menu */
        ui_pause(s_practice);
        return;
    }

    switch (g_phase) {
    case PH_RUN:
        run_tick();
        break;
    case PH_DEAD:
        s_t++;
        if (s_popup_t && !--s_popup_t) HIDE_WIN;
        if (s_t >= (s_practice ? 24 : 45)) {
            g_phase = PH_RESPAWN;
            s_t = 0;
        }
        break;
    case PH_RESPAWN:
        /* fade out (8 frames), draw the start (6), fade in (8) */
        s_t++;
        if (s_t <= 8) {
            s_fade = (uint8_t)(8 - s_t);
            s_pal_dirty = 1;
            if (s_t == 8) {
                HIDE_WIN;
                s_popup_t = 0;
                restore(s_practice && s_ncp ? &s_cp[s_ncp - 1] : &s_start);
            }
        } else if (s_t == 15) {
            /* the attempt is written at the start of the level */
            if (!(s_practice && s_ncp)) ui_attempt(s_attempts);
        } else if (s_t > 15) {
            s_fade = (uint8_t)(s_t - 15);
            s_pal_dirty = 1;
            if (s_fade >= 8) {
                s_fade = 8;
                g_phase = PH_RUN;
                if (!s_practice) music_play(SONG_FIRST_LEVEL_GB + L->song);
            }
        }
        break;
    case PH_COMPLETE: {
        int16_t stop = (int16_t)(L->width * 8) - 100;
        s_t++;
        gs_p.x += (uint32_t)gs_p.speed * 4;
        if (s_cam < stop) {
            int16_t d = (stop - s_cam) >> 3;
            s_cam += d > 2 ? 2 : (d < 1 ? 1 : d);
        }
        if (s_t == 70)
            ui_results(s_practice, s_attempts, s_jumps_total, (uint16_t)(s_ticks_total / 60),
                       s_practice ? 0 : L->ncoins, gs_p.coins);
        if (s_t > 70 && (g_pressed & (J_A | J_START))) s_quit = 1;
        if ((s_t & 15) == 0 && s_t < 64) burst((int16_t)(40 + (s_t << 1)), (int16_t)(30 + (s_t & 31)), 24);
        break;
    }
    }

    if (g_phase == PH_RUN) s_cam = px_of(gs_p.x) - PLAYER_SX;
    g_scx = (uint8_t)s_cam;
    if ((g_frame & 3) == 0) saw_frame((uint8_t)((g_frame >> 2) & 3));
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        draw_player();
        draw_trail();
        draw_effects();
        if (s_practice) draw_checkpoints();
        PERF_END(PERF_SPRITES, t);
    }
    /* Then what can wait a frame if this one is short of time (a frame
     * that runs late slows the game and its music down): the columns
     * coming into view, the progress bar and the palettes, each only if
     * its longest run (about 14, 20 and 31 scanlines) still fits. */
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        if (g_phase == PH_RESPAWN) stream(4);
        else if (frame_lines() < 154 - 20) stream(2);
        PERF_END(PERF_STREAM, t);
        PERF_BEGIN(t);
        if ((g_phase == PH_RUN || g_phase == PH_COMPLETE) && frame_lines() < 154 - 26) hud_update();
        PERF_END(PERF_HUD, t);
        PERF_BEGIN(t);
        if (frame_lines() < 154 - 37) palettes();
        PERF_END(PERF_PAL, t);
    }
}

void play_level(uint8_t level, uint8_t practice)
{
    g_screen = SCR_PLAY;
    s_level = level;
    s_practice = practice;
    L = &gbc_levels[level];
    SWITCH_ROM_MBC5(L->bank);
    gs_cells = L->cells;
    gs_width = L->width;
    gs_height = L->height;
    gs_ring_reset();

    video_off();
    hide_all_sprites();
    HIDE_WIN;
    s_quit = 0;
    s_paused = 0;
    s_ncp = 0;
    s_attempts = 0;
    s_ticks_total = 0;
    s_jumps_total = 0;
    s_popup_t = 0;

    gs_reset(L->speed);
    s_trig = 0;
    s_pal_from = s_pal_to = L->pal;
    s_pal_t = 48;
    s_rot = 0;
    take_snap(&s_start);
    restore(&s_start);
    /* draw the screen while the display is off */
    {
        uint8_t t[COL_ROWS], a[COL_ROWS], i;
        for (i = 0; i < 23; i++) {
            build_column(s_col, t, a);
            col_queue((uint8_t)(s_col & 31), t, a);
            s_col++;
            if (!col_queue_free()) video_vblank();
        }
        video_vblank();
    }
    ui_ground(32);
    ui_hud_init();
    hud_thresholds();
    s_pc = 0xff;
    memset(s_bar_fill, 0, sizeof(s_bar_fill));
    ui_attempt(s_attempts);
    s_fade = 8;
    s_flash = 0;
    pal_level(s_pal_from, s_pal_to, 255, 0, 8);
    g_hud_split = 1;
    g_scx = (uint8_t)s_cam;
    video_on();

    g_phase = PH_RUN;
    g_dropped = 0;
    music_play(practice ? SONG_PRACTICE_GB : SONG_FIRST_LEVEL_GB + L->song);
    s_vbl_start = g_vbl_count;
    while (!s_quit) {
#ifdef PD_PERF
        uint16_t t, tm;
#endif
        frame_wait();
        /* the last frame's work ran past the next vblank */
        if ((uint8_t)(g_vbl_count - s_vbl_start) > 1 && g_phase == PH_RUN && !s_paused) g_dropped++;
        s_vbl_start = g_vbl_count;
        PERF_BEGIN(t);
        /* the music first: it must not wait */
        PERF_BEGIN(tm);
        music_tick();
        PERF_END(PERF_MUSIC, tm);
        play_frame();
        PERF_END(PERF_FRAME, t);
    }
    music_stop();
    HIDE_WIN;
    hide_all_sprites();
    g_hud_split = 0;
    g_scx = 0;
}
