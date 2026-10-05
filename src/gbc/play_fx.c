/*
 * A level's sprites, effects, progress bar and palettes, and what a death
 * and a finish do (play.c has the frame; play.h what both share). In the
 * menus' ROM bank: none of it reads the level's.
 */
#pragma bank 15
#include <string.h>

#include "gbc.h"
#include "play.h"

static int16_t s_part_x[NPART], s_part_y[NPART]; /* 1/16 px, screen */
static int8_t s_part_vx[NPART], s_part_vy[NPART];
static uint8_t s_part_t, s_part_n, s_part_death;
/* a death: its burst starts the frame after (the physics ran in this one) */
static uint8_t s_death_wait;
static int16_t s_death_x, s_death_y;
/* and the screen shakes and flashes white: frames left */
static uint8_t s_shake_t, s_white_t;
int8_t s_shake_dx;
static int16_t s_trail_x[TRAIL]; /* world px */
static uint8_t s_trail_y[TRAIL], s_trail_n, s_trail_i;
static int16_t s_ring_x;
static uint8_t s_ring_y, s_ring_t;
static uint8_t s_bar_fill[12], s_pc = 0xff;

/* smoothstep over the 48 frames (0.8 s) of a palette change */
static const uint8_t SMOOTH[49] = {0,   0,   1,   3,   5,   8,   11,  15,  19,  24,  29,  34,  40,  46,  53,  59,  66,
                                   74,  81,  89,  96,  104, 112, 120, 128, 136, 144, 152, 160, 167, 175, 182, 190, 197,
                                   203, 210, 216, 222, 227, 232, 237, 241, 245, 248, 251, 253, 255, 255, 255};

/* ship tilt steps: |vy| against speed * 1.6 * tan(5, 15, 25 degrees) */
static const uint16_t SHIP_TILT[4][3] = {
    {321, 983, 1711}, {401, 1229, 2139}, {502, 1536, 2674}, {602, 1843, 3208}};

/* the fireworks' directions (1/16 px a frame) */
static const int8_t PART_VX[NPART] = {40, 28, 0, -28, -40, -28, 0, 28};
static const int8_t PART_VY[NPART] = {0, -28, -40, -28, 0, 28, 40, 28};

/* A death's burst, as the other versions': 22 particles in the player's
 * two colours (sprite palettes 3 and 4) and white sparks, every way at
 * their own speeds, slowing down, for 0.35 to 0.6 s. As s_dp starts
 * (below): x, y 0.5 (the death's pixel goes in at the start), velocities
 * in 1/256 px a frame (the table's in 1/24 px). */
#define DP(vx, vy) {0x80, 0, 0x80, 0, (uint8_t)((vx) * 24), (uint8_t)(((vx) * 24) >> 8), (uint8_t)((vy) * 24), \
                    (uint8_t)(((vy) * 24) >> 8)}
static const uint8_t DEATH_START[NPART_MAX][8] = {
    DP(-12, -21), DP(22, -26), DP(16, 1),  DP(-19, -2), DP(-6, -30), DP(9, -14),  DP(-11, 9),  DP(-15, -11),
    DP(-14, -15), DP(-4, 14),  DP(-31, 1), DP(-14, 5),  DP(11, 11),  DP(-19, 22), DP(-6, 17),  DP(29, 2),
    DP(2, 34),    DP(17, 15),  DP(24, -17), DP(22, 20), DP(0, -18),  DP(31, -10),
};
/* ... and for each, its frames, the frame it turns small (and lighter;
 * white sparks are small throughout), its sprite (byte in OAM) and palette */
#define DI(frames, pal, k) {frames, (pal) == OPAL_WHITE ? 0 : (frames) - 10, \
                            ((k) < NPART ? OAM_PART + (k) : OAM_PART2 + (k) - NPART) * 4, (pal) | 0x08}
static const uint8_t DEATH_INFO[NPART_MAX][4] = {
    DI(27, OPAL_COL1, 0),  DI(22, OPAL_WHITE, 1),  DI(36, OPAL_COL2, 2),  DI(27, OPAL_COL1, 3),
    DI(25, OPAL_WHITE, 4), DI(24, OPAL_COL2, 5),   DI(34, OPAL_COL1, 6),  DI(21, OPAL_WHITE, 7),
    DI(33, OPAL_COL2, 8),  DI(33, OPAL_COL1, 9),   DI(26, OPAL_WHITE, 10), DI(35, OPAL_COL2, 11),
    DI(30, OPAL_COL1, 12), DI(26, OPAL_WHITE, 13), DI(31, OPAL_COL2, 14), DI(36, OPAL_COL1, 15),
    DI(29, OPAL_WHITE, 16), DI(31, OPAL_COL2, 17), DI(27, OPAL_COL1, 18), DI(27, OPAL_WHITE, 19),
    DI(28, OPAL_COL1, 20), DI(24, OPAL_WHITE, 21),
};
/* the shake, about the other versions' (a few pixels at first, 0.35 s):
 * steps of SCX (-1..1) and SCY (0..2) times the shake's size */
static const int8_t SHAKE_X[8] = {1, -1, 0, 1, -1, 0, -1, 1};
static const uint8_t SHAKE_Y[8] = {2, 0, 1, 0, 2, 1, 0, 2};

/* where the level's percentage reaches k (progress.h), and what it is now */
static uint32_t s_pc_x[101];
static uint8_t s_pc_now;

void hud_thresholds(void) BANKED
{
    progress_steps(s_pc_x, L->width);
    s_pc_now = 0;
    s_pc = 0xff;
    memset(s_bar_fill, 0, sizeof(s_bar_fill));
}

/* The bar and the percentage, as far as this frame's cell queue takes
 * them: the rest the next frame (a restart after 70% changes 9 or more of
 * the bar's cells, and the queue has room for 8). */
void hud_update(void) BANKED
{
    uint32_t x = gs_p.x;
    uint8_t fill, i;
    if (x < s_pc_x[s_pc_now]) s_pc_now = 0; /* back at the start or a checkpoint */
    while (s_pc_now < 100 && x >= s_pc_x[s_pc_now + 1]) s_pc_now++;
    if (s_pc_now == s_pc) return;
    fill = (uint8_t)(((uint16_t)s_pc_now * 123) >> 7); /* 96 pixels for 100% */
    for (i = 0; i < 12; i++) {
        uint8_t f = fill > i * 8 ? (fill - i * 8 > 8 ? 8 : fill - i * 8) : 0;
        if (f != s_bar_fill[i]) {
            if (!cell_queue_free()) return;
            cell_queue(3 + i, 0, UT_BAR + f, PAL_HUD | 0x08);
            s_bar_fill[i] = f;
        }
    }
    if (cell_queue_free() < 4) return;
    {
        char buf[6];
        uint8_t n;
        fmt_uint(buf, s_pc_now);
        n = (uint8_t)strlen(buf);
        for (i = 0; i < 3; i++) cell_queue(16 + i, 0, i < 3 - n ? UT_BLANK : UT_FONT + buf[i - (3 - n)] - 32, PAL_HUD | 0x08);
        cell_queue(19, 0, UT_FONT + '%' - 32, PAL_HUD | 0x08);
    }
    s_pc = s_pc_now;
}

void palettes(uint8_t left) BANKED
{
    /* a palette change (blending or fading) takes up to 44 scanlines,
     * the beat's flash alone 15 (play.c): otherwise it waits a frame */
    if (left < 50 && (s_pal_dirty || s_pal_t < 48 || s_white_t || (s_demo && s_demo_t >= 359) ||
                      (s_trig < L->ntrig && (gs_p.x >> 16) >= L->trig[s_trig].x)))
        return;
    /* a death's white flash: a quarter of the way to white, gone in a
     * quarter of a second (the other versions' white overlay) */
    if (s_white_t) {
        s_white_t--;
        g_pal_white = s_white_t >> 1;
        s_pal_dirty = 1;
    }
    /* the title goes through the level palettes, 6 s each (as the other
     * versions' does) */
    if (s_demo && ++s_demo_t >= 360) {
        s_demo_t = 0;
        s_pal_from = s_pal_to;
        s_pal_to = s_pal_to + 1 == GBC_PALETTE_COUNT ? 0 : s_pal_to + 1;
        s_pal_t = 0;
    }
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
    /* The block edges and the ground line light up on the beat: white on
     * a bar's first, three quarters of the way on the others (the other
     * versions' pulse, which also glows), then fade by 11/16 every other
     * frame. */
    if (s_beat && g_phase == PH_RUN) {
        s_flash = s_beat_down ? 16 : 12;
        s_pal_dirty = 1;
    } else if (s_flash && (g_frame & 1)) {
        s_flash = (uint8_t)(((uint8_t)((s_flash << 3) + (s_flash << 1) + s_flash)) >> 4);
        s_pal_dirty = 1;
    }
    s_beat = 0;
    if (s_pal_dirty) {
        g_pal_split = 1;
        g_pal_pending = 0;
        pal_level(s_pal_from, s_pal_to, SMOOTH[s_pal_t], s_flash, s_fade);
        g_pal_split = 0;
        s_pal_dirty = g_pal_pending;
    }
}

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

void draw_player(void) BANKED
{
    int16_t sx = px_of(gs_p.x) - s_cam, sy = py_of(gs_p.y);
    uint8_t tile, prop = OPAL_PLAYER | 0x08, f;
    int8_t g = gs_p.grav;
    int16_t vy = gs_p.vy;
    /* (on the frame it died, until its burst starts) */
    if ((g_phase == PH_DEAD && s_death_wait < 2) || (g_phase == PH_RESPAWN && s_t < 8)) {
        hide_sprite(OAM_PLAYER);
        hide_sprite(OAM_PLAYER + 1);
        return;
    }
    switch (gs_p.mode) {
    case MODE_SHIP: {
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
    case MODE_BALL:
        f = (uint8_t)(((s_rot & 255) + 32) >> 6) & 3;
        tile = ST_BALL + 4 * f;
        break;
    case MODE_UFO: {
        int16_t v = g > 0 ? vy : -vy;
        f = v > 1365 ? 0 : v < -1365 ? 2 : 1;
        tile = ST_UFO + 4 * f;
        if (g < 0) prop |= S_FLIPY;
        break;
    }
    case MODE_WAVE:
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

/* The wave's trail: where it was on the last frames, as dots. */
static uint8_t s_trail_shown;

void draw_trail(void) BANKED
{
    uint8_t i, k, n = 0;
    if (gs_p.mode == MODE_WAVE && (g_phase == PH_RUN || g_phase == PH_COMPLETE)) {
        s_trail_x[s_trail_i] = px_of(gs_p.x);
        s_trail_y[s_trail_i] = (uint8_t)(py_of(gs_p.y));
        if (++s_trail_i == TRAIL) s_trail_i = 0;
        if (s_trail_n < TRAIL) s_trail_n++;
        /* newest first, skipping the one under the player */
        k = s_trail_i;
        for (i = 1; i < s_trail_n; i++) {
            k = k ? k - 1 : TRAIL - 1;
            if (i == 1 || s_trail_x[k] - s_cam > 160) continue; /* (speeding off past the finish) */
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

void draw_checkpoints(void) BANKED
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

/* A death's particles: x, y (8.8 screen pixels) and their speeds */
static uint8_t s_dp[NPART_MAX][8];
static uint8_t s_dp_n, s_dp_ox, s_dp_oy, s_dp_left, s_dp_px;
static const uint8_t *s_dp_at;

static void death_start(void)
{
    uint8_t i, *p = &s_dp[0][1];
    uint8_t x = (uint8_t)(s_death_x - 1), y = (uint8_t)(s_death_y - 1);
    memcpy(s_dp, DEATH_START, sizeof(s_dp));
    for (i = 0; i < NPART_MAX; i++, p += 8) {
        p[0] = x;
        p[2] = y;
    }
    s_part_t = 0;
    s_part_death = 1;
    fx_ring(s_death_x + s_cam, (uint8_t)s_death_y);
    s_shake_t = 20;
    s_white_t = 16;
}

/* Frame s_part_t of a death's particles, into the sprites: each slows
 * down (by an eighth every other frame), and turns small and light before
 * it goes. In assembly: 22 sprites in SDCC's C took 60 scanlines. */
static void death_parts(void) __naked
{
    __asm
    ld a, (_s_shake_dx) ; (the sprites shake with the screen)
    ld b, a
    ld a, #8
    sub a, b
    ld (_s_dp_ox), a
    ld a, (_g_scy)
    ld b, a
    ld a, #16
    sub a, b
    ld (_s_dp_oy), a
    xor a, a
    ld (_s_dp_left), a
    ld a, #22 ; NPART_MAX
    ld (_s_dp_n), a
    ld hl, #_DEATH_INFO
    ld a, l
    ld (_s_dp_at), a
    ld a, h
    ld (_s_dp_at + 1), a
    ld de, #_s_dp
1$:
    ld hl, #_s_dp_at
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, (_s_part_t)
    ld b, a
    ld a, (hl) ; its frames
    cp a, b
    jr z, 8$
    jr c, 9$
    ld a, #1
    ld (_s_dp_left), a
    bit 0, b
    jr z, 3$
    ld h, d
    ld l, e
    inc hl
    inc hl
    inc hl
    inc hl
    call death_drag ; vx
    call death_drag ; vy
3$:
    push de
    ld h, d
    ld l, e
    ld bc, #4
    add hl, bc
    ld a, (hl+)
    ld c, a
    ld a, (hl+)
    ld b, a
    ld a, (de) ; x += vx
    add a, c
    ld (de), a
    inc de
    ld a, (de)
    adc a, b
    ld (de), a
    ld (_s_dp_px), a
    inc de
    ld a, (hl+)
    ld c, a
    ld b, (hl)
    ld a, (de) ; y += vy
    add a, c
    ld (de), a
    inc de
    ld a, (de)
    adc a, b
    ld (de), a
    ld c, a
    ld hl, #_s_dp_at
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    inc hl
    ld a, (_s_part_t)
    cp a, (hl)
    ld b, #220 ; ST_PART
    jr c, 4$
    ld b, #222 ; ST_PART_SM
4$:
    inc hl
    ld a, (hl+)
    ld e, a
    ld a, (hl)
    ld h, #>_shadow_OAM
    ld l, e
    ld e, a
    ld a, (_s_dp_oy)
    add a, c
    ld (hl+), a
    ld a, (_s_dp_ox)
    ld c, a
    ld a, (_s_dp_px)
    add a, c
    ld (hl+), a
    ld a, b
    ld (hl+), a
    ld (hl), e
    pop de
    jr 9$
8$:
    inc hl ; its last frame: hidden
    inc hl
    ld a, (hl)
    ld l, a
    ld h, #>_shadow_OAM
    ld (hl), #0
9$:
    ld hl, #8
    add hl, de
    ld d, h
    ld e, l
    ld hl, #_s_dp_at
    ld a, (hl)
    add a, #4
    ld (hl+), a
    jr nc, 10$
    inc (hl)
10$:
    ld a, (_s_dp_n)
    dec a
    ld (_s_dp_n), a
    jp nz, 1$
    ret
death_drag: ; (hl) -= (hl) >> 3, 16 bits; hl += 2
    ld a, (hl+)
    ld c, a
    ld a, (hl-)
    ld b, a
    sra b
    rr c
    sra b
    rr c
    sra b
    rr c
    ld a, (hl)
    sub a, c
    ld (hl+), a
    ld a, (hl)
    sbc a, b
    ld (hl+), a
    ret
    __endasm;
}

static void draw_death(void)
{
    s_part_t++;
    death_parts();
    if (!s_dp_left) s_part_death = 0;
}

void draw_effects(void) BANKED
{
    uint8_t i;
    /* (not after a restart from the pause menu) */
    if (s_death_wait && !--s_death_wait && g_phase == PH_DEAD) death_start();
    if (s_shake_t) {
        uint8_t k = s_shake_t & 7, a = s_shake_t > 12 ? 2 : 1;
        s_shake_t--;
        s_shake_dx = s_shake_t ? (int8_t)(SHAKE_X[k] * a) : 0;
        g_scy = s_shake_t ? (uint8_t)((SHAKE_Y[k] * a) >> 1) : 0;
    }
    if (s_part_death) {
        draw_death();
    } else if (s_part_t) {
        s_part_t--;
        for (i = 0; i < s_part_n; i++) {
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
        put16(OAM_RING, s_ring_t > 4 ? ST_RING : ST_RING + 4, s_ring_x - s_cam - s_shake_dx, s_ring_y - g_scy,
              OPAL_FX | 0x08);
        if (!s_ring_t) {
            hide_sprite(OAM_RING);
            hide_sprite(OAM_RING + 1);
        }
    }
}

void burst(int16_t sx, int16_t sy, uint8_t frames) BANKED
{
    uint8_t i;
    for (i = 0; i < NPART; i++) {
        s_part_x[i] = (sx - 1) << 4;
        s_part_y[i] = (sy - 1) << 4;
        s_part_vx[i] = PART_VX[i];
        s_part_vy[i] = PART_VY[i];
    }
    s_part_n = NPART;
    s_part_t = frames;
}

void hide_all_sprites(void) BANKED
{
    uint8_t i;
    for (i = 0; i < 40; i++) hide_sprite(i);
    s_part_t = s_ring_t = s_part_death = s_death_wait = 0;
    s_trail_n = 0;
    s_trail_shown = 0;
    s_shake_t = s_white_t = 0;
    s_shake_dx = 0;
    g_scy = 0;
    if (g_pal_white) {
        g_pal_white = 0;
        s_pal_dirty = 1;
    }
}

static uint8_t level_pc(void)
{
    hud_update();
    return s_pc_now;
}

/* The run died on this tick: the sound now; the burst, the shake and the
 * flash from the next frame, and what the attempt counts for (death_count)
 * from the one after (this frame ran the physics, and together they took
 * longer than a frame). */
void on_death(void) BANKED
{
    sfx_play(SFX_DEATH);
    s_death_x = px_of(gs_p.x) - s_cam;
    s_death_y = py_of(gs_p.y);
    s_death_wait = 2;
    g_phase = PH_DEAD;
    s_t = 0;
    if (!s_practice) music_stop();
}

/* The attempt's progress, saved, and a new best shown. */
void death_count(void) BANKED
{
    uint8_t pc = level_pc();
    s_ticks_total += gs_p.ticks;
    s_jumps_total += gs_p.jumps;
    s_new_best = progress_death(&g_save.progress, L->id, s_practice, pc, gs_p.jumps);
    save_write();
    /* over the level, centred on the screen (the camera stays put until
     * the respawn draws the level anew) */
    if (s_new_best) ui_new_best(s_new_best, (uint8_t)col_of(s_cam + 36));
}

void on_complete(void) BANKED
{
    uint8_t first;
    sfx_play(SFX_COMPLETE);
    g_phase = PH_COMPLETE;
    s_t = 0;
    s_ticks_total += gs_p.ticks;
    s_jumps_total += gs_p.jumps;
    progress_complete(&g_save.progress, L->id, s_practice, gs_p.coins, gs_p.jumps, &first);
    save_write();
    burst(PLAYER_SX, 60, 40);
}

/* A tick past the finish line, as sim_coast (src/core/sim.c): the player
 * levels out within the bounds that stop it and, after SIM_EXIT_WAIT
 * ticks, speeds off the screen. */
void finish_exit(void) BANKED
{
    uint16_t hw, hh;
    uint8_t n = SIM_SUBSTEPS;
    int32_t lo, hi;
    if (px_of(gs_p.x) - s_cam > 176) return; /* gone (and kept from coming round again) */
    if (s_t > SIM_EXIT_WAIT) n = s_t - SIM_EXIT_WAIT >= SIM_EXIT_MAX - SIM_SUBSTEPS ? SIM_EXIT_MAX : SIM_SUBSTEPS + (uint8_t)(s_t - SIM_EXIT_WAIT);
    gs_p.x += (uint32_t)gs_p.speed * n;
    gs_p.y += (int32_t)gs_p.vy << 2; /* SIM_SUBSTEPS */
    gs_p.vy /= 2;
    gs_hitbox(&hw, &hh);
    if (gs_p.mode == MODE_CUBE) {
        lo = hh;
        hi = 0x7fffffffL;
    } else {
        lo = ((int32_t)gs_p.floor_y << 16) + hh;
        hi = ((int32_t)gs_p.ceil_y << 16) - hh;
    }
    if (gs_p.y < lo) gs_p.y = lo;
    if (gs_p.y > hi) gs_p.y = hi;
}

/* a ring around an orb or pad that fired (world x, screen y) */
void fx_ring(int16_t x, uint8_t y) BANKED
{
    s_ring_x = x;
    s_ring_y = y;
    s_ring_t = 8;
}

/* the run moved by dx pixels (the title's loop) */
void fx_shift(int16_t dx) BANKED
{
    uint8_t i;
    s_ring_x += dx;
    for (i = 0; i < TRAIL; i++) s_trail_x[i] += dx;
}

/* an attempt starts: no sprites, the progress bar drawn anew */
void fx_restart(void) BANKED
{
    s_pc = 0xff;
    hide_all_sprites();
}
