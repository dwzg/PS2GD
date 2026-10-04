/*
 * The player's physics for the Game Boy Color; see gbsim.h. The functions
 * follow src/core/sim.c, the reference, one for one, so a change there must
 * be mirrored here (gbc_tool difftest checks that both give the same
 * results).
 *
 * Written for the Game Boy's CPU: it has no multiply,
 * 8-bit registers, and SDCC turns a 32-bit comparison into some 85
 * instructions. So the player and the boxes being tested are globals, and
 * the box tests compare a position with "cell + constant" split into whole
 * blocks and 1/65536 (me_x, BOX): 16-bit work, exactly the result of the
 * 32-bit comparison.
 */
#ifdef __SDCC
#pragma bank 14
#endif
#include "gbsim.h"

GsPlayer gs_p;

#define P gs_p
#define ONE SIM_ONE

/* The whole blocks and 1/65536 of the player's x and y (16.16), read and
 * written as 16-bit halves: SDCC's 32-bit shifts and compares are slow. */
#ifdef __SDCC
#define X_LO() (((uint16_t *)&P.x)[0])
#define X_HI() (((uint16_t *)&P.x)[1])
#define Y_LO() (((uint16_t *)&P.y)[0])
#define Y_HI() (((int16_t *)&P.y)[1])
#define SET_X(hi, lo) (((uint16_t *)&P.x)[0] = (lo), ((uint16_t *)&P.x)[1] = (hi))
#define SET_Y(hi, lo) (((uint16_t *)&P.y)[0] = (lo), ((int16_t *)&P.y)[1] = (hi))
#else
#define X_LO() ((uint16_t)P.x)
#define X_HI() ((uint16_t)(P.x >> 16))
#define Y_LO() ((uint16_t)P.y)
#define Y_HI() ((int16_t)((uint32_t)P.y >> 16))
#define SET_X(hi, lo) (P.x = (uint32_t)(uint16_t)(hi) << 16 | (uint16_t)(lo))
#define SET_Y(hi, lo) (P.y = (int32_t)((uint32_t)(uint16_t)(hi) << 16 | (uint16_t)(lo)))
#endif

static const uint16_t SPEED_HI[4] = SIM_SPEED_HI;
static const uint16_t SPEED_LO[4] = SIM_SPEED_LO;
static const int16_t ORB_YELLOW_V[4] = SIM_ORB_YELLOW_V;
static const int16_t ORB_PINK_V[4] = SIM_ORB_PINK_V;
static const int16_t PAD_YELLOW_V[4] = SIM_PAD_YELLOW_V;
static const int16_t PAD_PINK_V[4] = SIM_PAD_PINK_V;

/* 1 << n */
static const uint16_t BIT[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};

/* --- comparisons --- */

/* The player's box being tested (outer or inner): its edges in whole
 * blocks and 1/65536, worked out from 16-bit halves of x and y. */
static int16_t mx0i, mx1i, my0i, my1i;
static uint16_t mx0f, mx1f, my0f, my1f;
/* half extents of the player's outer hitbox this sub-step */
static uint16_t s_hw, s_hh;

/* The half extents me_x / me_y work with, and the cell at_cell tests. */
static uint16_t s_cw, s_ch;
static int16_t s_cx, s_cy;

/* The cell being tested: the whole-block distances of the box's edges from
 * it (the cells tested are next to the box, so they fit 8 bits). */
static int8_t dx0, dx1, dy0, dy1;

#ifdef __SDCC
/* SM83 versions of the three helpers called most (SDCC's code for them is
 * three times as long); the C below is the reference, and the emulator
 * test plays the levels against it. Arguments and results in globals. */

/* mx0 = x - s_cw, mx1 = x + s_cw */
static void me_x(void) __naked
{
    __asm
    ld hl, #_s_cw
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld hl, #_gs_p
    ld a, (hl+)
    ld c, a
    ld a, (hl+)
    ld b, a
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, c
    sub a, e
    ld (_mx0f), a
    ld a, b
    sbc a, d
    ld (_mx0f + 1), a
    ld a, l
    sbc a, #0
    ld (_mx0i), a
    ld a, h
    sbc a, #0
    ld (_mx0i + 1), a
    ld a, c
    add a, e
    ld (_mx1f), a
    ld a, b
    adc a, d
    ld (_mx1f + 1), a
    ld a, l
    adc a, #0
    ld (_mx1i), a
    ld a, h
    adc a, #0
    ld (_mx1i + 1), a
    ret
    __endasm;
}

/* my0 = y - s_ch, my1 = y + s_ch (y at gs_p + 6) */
static void me_y(void) __naked
{
    __asm
    ld hl, #_s_ch
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld hl, #(_gs_p + 6)
    ld a, (hl+)
    ld c, a
    ld a, (hl+)
    ld b, a
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, c
    sub a, e
    ld (_my0f), a
    ld a, b
    sbc a, d
    ld (_my0f + 1), a
    ld a, l
    sbc a, #0
    ld (_my0i), a
    ld a, h
    sbc a, #0
    ld (_my0i + 1), a
    ld a, c
    add a, e
    ld (_my1f), a
    ld a, b
    adc a, d
    ld (_my1f + 1), a
    ld a, l
    adc a, #0
    ld (_my1i), a
    ld a, h
    adc a, #0
    ld (_my1i + 1), a
    ret
    __endasm;
}

/* dx0 = mx0i - s_cx ... (the low bytes are enough: the results fit 8 bits) */
static void at_cell(void) __naked
{
    __asm
    ld a, (_s_cx)
    ld c, a
    ld a, (_mx0i)
    sub a, c
    ld (_dx0), a
    ld a, (_mx1i)
    sub a, c
    ld (_dx1), a
    ld a, (_s_cy)
    ld c, a
    ld a, (_my0i)
    sub a, c
    ld (_dy0), a
    ld a, (_my1i)
    sub a, c
    ld (_dy1), a
    ret
    __endasm;
}
#else
static void me_x(void)
{
    uint16_t lo = (uint16_t)P.x, hi = (uint16_t)(P.x >> 16);
    mx0f = lo - s_cw;
    mx0i = (int16_t)(hi - (lo < s_cw));
    mx1f = lo + s_cw;
    mx1i = (int16_t)(hi + (mx1f < lo));
}

static void me_y(void)
{
    uint16_t lo = (uint16_t)P.y, hi = (uint16_t)((uint32_t)P.y >> 16);
    my0f = lo - s_ch;
    my0i = (int16_t)(hi - (lo < s_ch));
    my1f = lo + s_ch;
    my1i = (int16_t)(hi + (my1f < lo));
}

static void at_cell(void)
{
    dx0 = (int8_t)(mx0i - s_cx);
    dx1 = (int8_t)(mx1i - s_cx);
    dy0 = (int8_t)(my0i - s_cy);
    dy1 = (int8_t)(my1i - s_cy);
}
#endif

/* A box edge at v (16.16, relative to the cell) as whole blocks and 1/65536 */
#define KI(v) ((int8_t)((v) >= 0 ? (v) / ONE : -((-(v) + ONE - 1) / ONE)))
#define KF(v) ((uint16_t)((v) - (long)KI(v) * ONE))
/* edge d + f/65536 < (or >) the constant k */
#define LT(d, f, k) ((d) < KI(k) || ((d) == KI(k) && (f) < KF(k)))
#define GT(d, f, k) ((d) > KI(k) || ((d) == KI(k) && (f) > KF(k)))
/* Does the box overlap the box (x0, y0)-(x1, y1) of the cell at_cell set?
 * As the float version's a.x0 < b.x1 - eps && a.x1 > b.x0 + eps && the
 * same in y, with the constants worked out by the compiler. */
#define BOX(x0, y0, x1, y1)                                                                              \
    (LT(dx0, mx0f, (x1) - SIM_OVERLAP_EPS) && GT(dx1, mx1f, (x0) + SIM_OVERLAP_EPS) &&                     \
     LT(dy0, my0f, (y1) - SIM_OVERLAP_EPS) && GT(dy1, my1f, (y0) + SIM_OVERLAP_EPS))
/* ... one of sim_rules.h's boxes */
#define OBJ_BOX(box) BOX(box)

static uint8_t box_solid(uint8_t kind)
{
    switch (kind) {
    case OBJ_BLOCK: return BOX(0L, 0L, ONE, ONE);
    case OBJ_SLAB_LO: return BOX(0L, 0L, ONE, ONE / 2);
    default: return BOX(0L, ONE / 2, ONE, ONE);
    }
}

/* Does the box touch the circle of radius r (< 0.72) around the cell's centre? */
static uint8_t circle_hit(int16_t cx, int16_t cy, uint16_t r)
{
    int32_t x0 = (int32_t)((uint32_t)(uint16_t)mx0i << 16 | mx0f), x1 = (int32_t)((uint32_t)(uint16_t)mx1i << 16 | mx1f);
    int32_t y0 = (int32_t)((uint32_t)(uint16_t)my0i << 16 | my0f), y1 = (int32_t)((uint32_t)(uint16_t)my1i << 16 | my1f);
    int32_t ccx = ((int32_t)cx << 16) + ONE / 2, ccy = ((int32_t)cy << 16) + ONE / 2;
    int32_t px = ccx < x0 ? x0 : (ccx > x1 ? x1 : ccx);
    int32_t py = ccy < y0 ? y0 : (ccy > y1 ? y1 : ccy);
    int32_t dx = px - ccx, dy = py - ccy;
    uint16_t ax, ay, ar;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx >= r || dy >= r) return 0;
    /* in 1/256 blocks, where each square fits 16 bits */
    ax = (uint16_t)dx >> 8;
    ay = (uint16_t)dy >> 8;
    ar = r >> 8;
    return (uint32_t)(uint16_t)(ax * ax) + (uint16_t)(ay * ay) < (uint32_t)(uint16_t)(ar * ar);
}

/* --- the level --- */

/* rows lo..15 and 0..hi of a column, as masks */
static const uint16_t ROWS_FROM[16] = {0xffff, 0xfffe, 0xfffc, 0xfff8, 0xfff0, 0xffe0, 0xffc0, 0xff80,
                                       0xff00, 0xfe00, 0xfc00, 0xf800, 0xf000, 0xe000, 0xc000, 0x8000};
static const uint16_t ROWS_UPTO[16] = {0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
                                       0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff};

/* The rows lo..hi clamped to the level, in r_lo..r_hi, and as a mask; 0
 * if none. */
static uint8_t r_lo, r_hi;
#ifdef __SDCC
static uint16_t rows_of(int16_t lo, int16_t hi) __naked
{
    lo;
    hi;
    __asm
    bit 7, b ; hi < 0: none
    jr nz, 9$
    bit 7, d
    jr z, 1$
    xor a, a ; lo < 0: from row 0
    jr 2$
1$:
    ld a, d
    or a, a
    jr nz, 9$
    ld a, e
    cp a, #16
    jr nc, 9$ ; lo > 15: none
2$:
    ld (_r_lo), a
    ld e, a
    ld a, b
    or a, a
    jr nz, 3$
    ld a, c
    cp a, #16
    jr c, 4$
3$:
    ld a, #15
4$:
    ld (_r_hi), a
    cp a, e
    jr c, 9$
    add a, a
    add a, #<_ROWS_UPTO
    ld l, a
    ld a, #0
    adc a, #>_ROWS_UPTO
    ld h, a
    ld a, (hl+)
    ld c, a
    ld b, (hl)
    ld a, e
    add a, a
    add a, #<_ROWS_FROM
    ld l, a
    ld a, #0
    adc a, #>_ROWS_FROM
    ld h, a
    ld a, (hl+)
    and a, c
    ld c, a
    ld a, (hl)
    and a, b
    ld b, a
    ret
9$:
    ld bc, #0
    ret
    __endasm;
}
#else
static uint16_t rows_of(int16_t lo, int16_t hi)
{
    if (hi < 0) return 0;
    if (lo < 0) r_lo = 0;
    else if ((uint16_t)lo > GS_ROWS - 1) return 0;
    else r_lo = (uint8_t)lo;
    r_hi = (uint16_t)hi > GS_ROWS - 1 ? GS_ROWS - 1 : (uint8_t)hi;
    if (r_lo > r_hi) return 0;
    return ROWS_FROM[r_lo] & ROWS_UPTO[r_hi];
}
#endif

/* Is the cell above / below row cy of ring column ci a whole block? */
#define BLOCK_ABOVE(ci, cy) ((cy) < GS_ROWS - 1 && (gs_ring_block[ci] & BIT[(cy) + 1]))
#define BLOCK_BELOW(ci, cy) ((cy) > 0 && (gs_ring_block[ci] & BIT[(cy) - 1]))

/* Hitbox half extents (16.16) by mode: cube, ship, ball, UFO, wave. The
 * outer box touches solids and objects, the inner one kills in a solid. */
static const uint16_t HIT_W[MODE_COUNT] = SIM_HIT_W;
static const uint16_t HIT_H[MODE_COUNT] = SIM_HIT_H;
static const uint16_t INNER_W[MODE_COUNT] = SIM_INNER_W;
static const uint16_t INNER_H[MODE_COUNT] = SIM_INNER_H;

void gs_hitbox(uint16_t *hw, uint16_t *hh) GS_BANKED
{
    *hw = HIT_W[P.mode];
    *hh = HIT_H[P.mode];
}

static uint8_t is_used(uint16_t key)
{
    uint8_t i;
    key++;
    for (i = 0; i < GS_USED_N; i++)
        if (P.used[i] == key) return 1;
    return 0;
}

static void set_used(uint16_t key)
{
    P.used[P.used_pos] = key + 1;
    P.used_pos = (uint8_t)((P.used_pos + 1) & (GS_USED_N - 1));
}

void gs_reset(uint8_t start_speed) GS_BANKED
{
    uint8_t *b = (uint8_t *)&P;
    uint16_t i;
    for (i = 0; i < sizeof(P); i++) b[i] = 0;
    P.y = ONE / 2;
    P.grav = 1;
    P.mode = MODE_CUBE;
    P.speed_idx = start_speed > 3 ? 3 : start_speed;
    P.speed = SPEED_HI[P.speed_idx];
    P.grounded = 1;
    P.floor_y = 0;
    P.ceil_y = SIM_CORRIDOR;
}

/* --- moving forwards --- */

/* x += the speed's step, exactly (x and xfrac are x in 1/2^32 blocks) */
static void advance_x(void)
{
    uint16_t f = P.xfrac, lo = X_LO(), nlo;
    P.xfrac = f + SPEED_LO[P.speed_idx];
    nlo = lo + P.speed + (P.xfrac < f);
    SET_X(X_HI() + (nlo < lo), nlo);
}

static void enter_mode(uint8_t mode, int16_t cy)
{
    if (mode != MODE_CUBE) {
        /* corridor centred on the portal: floor(cy + 0.5 - 5 + 0.5) */
        int16_t fl = cy - SIM_CORRIDOR / 2 + 1;
        if (fl < 0) fl = 0;
        P.floor_y = (int8_t)fl;
        P.ceil_y = (int8_t)(fl + SIM_CORRIDOR);
    }
    if (P.mode != mode) {
        P.vy = P.vy / 2;
        P.mode = mode;
    }
    P.grounded = 0;
}

/* --- solids --- */

/* A solid / an object somewhere the player can reach this tick (else the
 * sub-steps' cell tests would find nothing, so they are skipped). */
static uint8_t s_near_solid, s_near_obj;

/* resolve_solids found solids in the cells around the box (else, unless
 * y moved since, the inner box has none either: it is inside the outer) */
static uint8_t s_solids_near;
static int32_t s_solids_y;

/* y before this sub-step, and the box's low and high edges then (whole
 * blocks and 1/65536), worked out by prev_edges() when a cell needs them */
static int32_t s_prev_y;
static uint8_t s_prev_ok;
static int16_t pli, phi;
static uint16_t plf, phf;

static void prev_edges(void)
{
    uint16_t lo = (uint16_t)s_prev_y, hi = (uint16_t)((uint32_t)s_prev_y >> 16);
    plf = lo - s_hh;
    pli = (int16_t)(hi - (lo < s_hh));
    phf = lo + s_hh;
    phi = (int16_t)(hi + (phf < lo));
    s_prev_ok = 1;
}

/* how far the box may be in a solid and still step onto it (0.25, a
 * wave's 0.06); whether it can bump its head (not as a cube); my0..my1
 * are out of date (the box was moved onto or under a solid) */
static uint16_t s_step_up;
static uint8_t s_can_ceil, s_y_moved;

/* Can the box land on the solid (top: of its cell, 1/65536)? Its low edge
 * was above the top before this sub-step, or is within a step of it (all
 * these points are inside the cell, whole part 0). */
static uint8_t low_ok(uint8_t cy, uint16_t top)
{
    if (dy0 > 0 || (dy0 == 0 && my0f >= (uint16_t)(top - s_step_up))) return 1;
    if (!s_prev_ok) prev_edges();
    return pli > (int16_t)cy || (pli == (int16_t)cy && plf >= (uint16_t)(top - SIM_SNAP_EPS));
}

/* ... hang under it: the same for the high edge and the solid's bottom */
static uint8_t high_ok(uint8_t cy, uint16_t bottom)
{
    if (dy1 < 0 || (dy1 == 0 && my1f <= (uint16_t)(bottom + s_step_up))) return 1;
    if (!s_prev_ok) prev_edges();
    return phi < (int16_t)cy || (phi == (int16_t)cy && phf <= (uint16_t)(bottom + SIM_SNAP_EPS));
}

/* y = cy + top (a whole block's: 1) + hh, on the solid */
static void land_on(uint8_t cy, uint16_t top)
{
    uint16_t lo = top + s_hh;
    SET_Y((int16_t)cy + (top ? 0 : 1) + (lo < s_hh), lo);
    s_y_moved = 1;
}

/* y = cy + bottom - hh, under it */
static void hang_under(uint8_t cy, uint16_t bottom)
{
    SET_Y((int16_t)cy - (bottom < s_hh), (uint16_t)(bottom - s_hh));
    s_y_moved = 1;
}

/* The solid cell (s_cx, cy) of ring column ci against the outer box. */
static void solid_cell(uint8_t ci, uint8_t cy)
{
    uint8_t t = GTI_KIND(gs_tile_info[gs_ring[(ci << 4) | cy]]);
    uint16_t top, bottom; /* the solid's top and bottom in its cell, 1/65536 (a whole block's top: 0) */
    if (s_y_moved) {
        me_y();
        s_y_moved = 0;
    }
    s_cy = cy;
    at_cell();
    if (!box_solid(t)) return;
    top = t == OBJ_SLAB_LO ? 32768 : 0;
    bottom = t == OBJ_SLAB_HI ? 32768 : 0;
    if (P.grav > 0) {
        if (P.vy <= 0 && (t != OBJ_BLOCK || !BLOCK_ABOVE(ci, cy)) && low_ok(cy, top)) {
            land_on(cy, top);
            P.vy = 0;
            P.grounded = 1;
        } else if (s_can_ceil && P.vy >= 0 && (t != OBJ_BLOCK || !BLOCK_BELOW(ci, cy)) && high_ok(cy, bottom)) {
            hang_under(cy, bottom);
            P.vy = 0;
        }
    } else {
        if (P.vy >= 0 && (t != OBJ_BLOCK || !BLOCK_BELOW(ci, cy)) && high_ok(cy, bottom)) {
            hang_under(cy, bottom);
            P.vy = 0;
            P.grounded = 1;
        } else if (s_can_ceil && P.vy <= 0 && (t != OBJ_BLOCK || !BLOCK_ABOVE(ci, cy)) && low_ok(cy, top)) {
            land_on(cy, top);
            P.vy = 0;
        }
    }
}

/* Land on / bump into solid cells overlapping the outer box: column by
 * column, each bottom up, the cells gs_ring_solid has in the box's rows. */
static void resolve_solids(void)
{
    s_solids_near = 0;
    if (s_near_solid) {
        uint16_t rows;
        s_cw = s_hw;
        s_ch = s_hh;
        me_x();
        me_y();
        rows = rows_of(my0i, my1i);
        if (rows) {
            uint8_t cy0 = r_lo, n = (uint8_t)(mx1i - mx0i) + 1;
            s_prev_ok = 0;
            s_y_moved = 0;
            s_can_ceil = P.mode != MODE_CUBE;
            s_step_up = P.mode == MODE_WAVE ? SIM_STEP_UP_WAVE : SIM_STEP_UP;
            s_cx = mx0i;
            do {
                if ((uint16_t)s_cx < gs_width) {
                    uint8_t ci = (uint8_t)s_cx & (GS_RING - 1), cy;
                    uint16_t m = gs_ring_solid[ci] & rows;
                    if (m) {
                        uint16_t bit = BIT[cy0];
                        s_solids_near = 1;
                        /* (m has no bits outside the rows) */
                        for (cy = cy0;; cy++, bit <<= 1)
                            if (m & bit) {
                                solid_cell(ci, cy);
                                m &= ~bit;
                                if (!m) break;
                            }
                    }
                }
                s_cx++;
            } while (--n);
        }
    }
    s_solids_y = P.y;

    /* World floor / corridor bounds never kill, they just stop you. */
    if (P.mode == MODE_CUBE) {
        int16_t hi = Y_HI();
        if (hi < 0 || (hi == 0 && Y_LO() < s_hh)) { /* y < hh */
            SET_Y(0, s_hh);
            if (P.vy < 0) P.vy = 0;
            if (P.grav > 0) P.grounded = 1;
        }
    } else {
        int16_t hi = Y_HI();
        uint16_t lo = Y_LO(), top;
        if (hi - (lo < s_hh) < P.floor_y) { /* y - hh < floor */
            hi = P.floor_y;
            lo = s_hh;
            SET_Y(hi, lo);
            if (P.vy < 0) P.vy = 0;
            if (P.grav > 0) P.grounded = 1;
        }
        top = lo + s_hh;
        hi += top < lo;
        if (hi > P.ceil_y || (hi == P.ceil_y && top)) { /* y + hh > ceiling */
            SET_Y(P.ceil_y - 1, (uint16_t)(0 - s_hh));
            if (P.vy > 0) P.vy = 0;
            if (P.grav < 0) P.grounded = 1;
        }
    }
}

static uint8_t inner_hits_solid(void)
{
    uint16_t rows;
    uint8_t n;
    /* the inner box is inside the outer one */
    if (!s_near_solid || (!s_solids_near && P.y == s_solids_y)) return 0;
    s_cw = INNER_W[P.mode];
    s_ch = INNER_H[P.mode];
    me_x();
    me_y();
    rows = rows_of(my0i, my1i);
    if (!rows) return 0;
    s_cx = mx0i;
    n = (uint8_t)(mx1i - mx0i) + 1;
    do {
        if ((uint16_t)s_cx < gs_width) {
            uint8_t ci = (uint8_t)s_cx & (GS_RING - 1), cy;
            uint16_t m = gs_ring_solid[ci] & rows;
            if (m) {
                uint16_t bit = BIT[r_lo];
                for (cy = r_lo;; cy++, bit <<= 1)
                    if (m & bit) {
                        s_cy = cy;
                        at_cell();
                        if (box_solid(GTI_KIND(gs_tile_info[gs_ring[(ci << 4) | cy]]))) return 1;
                        m &= ~bit;
                        if (!m) break;
                    }
            }
        }
        s_cx++;
    } while (--n);
    return 0;
}

/* --- objects --- */

static void apply_orb(uint8_t k)
{
    if (P.mode == MODE_WAVE) {
        if (k == OBJ_ORB_BLUE || k == OBJ_ORB_GREEN) {
            P.grav = (int8_t)-P.grav;
            P.events |= EV_GRAVITY;
        }
        return;
    }
    switch (k) {
    case OBJ_ORB_YELLOW: P.vy = P.grav > 0 ? ORB_YELLOW_V[P.mode] : -ORB_YELLOW_V[P.mode]; break;
    case OBJ_ORB_PINK: P.vy = P.grav > 0 ? ORB_PINK_V[P.mode] : -ORB_PINK_V[P.mode]; break;
    case OBJ_ORB_BLUE:
        P.grav = (int8_t)-P.grav;
        P.vy = P.grav > 0 ? -SIM_ORB_BLUE_V : SIM_ORB_BLUE_V;
        P.events |= EV_GRAVITY;
        break;
    case OBJ_ORB_GREEN:
        P.grav = (int8_t)-P.grav;
        P.vy = P.grav > 0 ? ORB_YELLOW_V[P.mode] : -ORB_YELLOW_V[P.mode];
        P.events |= EV_GRAVITY;
        break;
    }
    P.grounded = 0;
}

static void apply_pad(uint8_t k)
{
    if (P.mode == MODE_WAVE) {
        if (k == OBJ_PAD_BLUE) {
            P.grav = (int8_t)-P.grav;
            P.events |= EV_GRAVITY;
        }
        return;
    }
    switch (k) {
    case OBJ_PAD_YELLOW: P.vy = P.grav > 0 ? PAD_YELLOW_V[P.mode] : -PAD_YELLOW_V[P.mode]; break;
    case OBJ_PAD_PINK: P.vy = P.grav > 0 ? PAD_PINK_V[P.mode] : -PAD_PINK_V[P.mode]; break;
    case OBJ_PAD_BLUE:
        P.grav = (int8_t)-P.grav;
        P.vy = P.grav > 0 ? -SIM_PAD_BLUE_V : SIM_PAD_BLUE_V;
        P.events |= EV_GRAVITY;
        break;
    }
    P.grounded = 0;
}

/* Objects that can touch a box outside their cell: big saws (r 0.72),
 * orbs (0.1 beyond) and portals (a block above and below). */
#define REACHES_OUT(k) \
    ((k) == OBJ_SAW_BIG || (uint8_t)((k) - OBJ_ORB_YELLOW) <= OBJ_ORB_GREEN - OBJ_ORB_YELLOW || \
     (uint8_t)((k) - OBJ_PORTAL_CUBE) <= OBJ_SPEED_3 - OBJ_PORTAL_CUBE)

/* The object in cell (s_cx, cy) against the outer box; info: its
 * gs_tile_info. */
static void touch_cell(uint8_t cy, uint8_t info)
{
    uint8_t k = GTI_KIND(info);
    uint16_t key;
    s_cy = cy;
    at_cell();
    if (k <= OBJ_SAW_SMALL) {
        uint8_t hit;
        if (P.dead) return;
        switch (k) {
        case OBJ_SPIKE_UP: hit = OBJ_BOX(SIM_BOX_SPIKE_UP); break;
        case OBJ_SPIKE_DOWN: hit = OBJ_BOX(SIM_BOX_SPIKE_DOWN); break;
        case OBJ_SPIKE_SM_UP: hit = OBJ_BOX(SIM_BOX_SPIKE_SM_UP); break;
        case OBJ_SPIKE_SM_DOWN: hit = OBJ_BOX(SIM_BOX_SPIKE_SM_DOWN); break;
        case OBJ_SAW_BIG: hit = circle_hit(s_cx, cy, SIM_SAW_BIG_R); break;
        default: hit = circle_hit(s_cx, cy, SIM_SAW_SMALL_R); break;
        }
        if (hit) {
            P.dead = 1;
            P.events |= EV_DEATH;
        }
        return;
    }
    /* (each test below asks is_used after the box test: the same
     * answer as asking first, and quicker) */
    key = ((uint16_t)s_cx << 4) | cy;
    if (k <= OBJ_ORB_GREEN) {
        if (P.buf && OBJ_BOX(SIM_BOX_ORB) && !is_used(key)) {
            set_used(key);
            P.buf = 0;
            apply_orb(k);
            P.events |= EV_ORB;
            P.ev_cell = key;
        }
    } else if (k <= OBJ_PAD_BLUE) {
        if (((info & GTI_CEILING) ? OBJ_BOX(SIM_BOX_PAD_CEILING) : OBJ_BOX(SIM_BOX_PAD)) &&
            !is_used(key)) {
            set_used(key);
            apply_pad(k);
            P.events |= EV_PAD;
            P.ev_cell = key;
        }
    } else if (k <= OBJ_SPEED_3) {
        if (!OBJ_BOX(SIM_BOX_PORTAL) || is_used(key)) return;
        set_used(key);
        P.ev_cell = key;
        switch (k) {
        case OBJ_PORTAL_CUBE: enter_mode(MODE_CUBE, cy); P.events |= EV_PORTAL; break;
        case OBJ_PORTAL_SHIP: enter_mode(MODE_SHIP, cy); P.events |= EV_PORTAL; break;
        case OBJ_PORTAL_BALL: enter_mode(MODE_BALL, cy); P.events |= EV_PORTAL; break;
        case OBJ_PORTAL_UFO: enter_mode(MODE_UFO, cy); P.events |= EV_PORTAL; break;
        case OBJ_PORTAL_WAVE: enter_mode(MODE_WAVE, cy); P.events |= EV_PORTAL; break;
        case OBJ_PORTAL_GRAV_FLIP:
        case OBJ_PORTAL_GRAV_NORMAL: {
            int8_t ng = k == OBJ_PORTAL_GRAV_FLIP ? -1 : 1;
            if (ng != P.grav) {
                P.grav = ng;
                P.vy = (int16_t)((int32_t)P.vy * 2 / 5); /* x0.4 */
                P.grounded = 0;
                P.events |= EV_GRAVITY;
            }
            P.events |= EV_PORTAL;
            break;
        }
        default:
            P.speed_idx = (uint8_t)(k - OBJ_SPEED_0);
            P.speed = SPEED_HI[P.speed_idx];
            P.events |= EV_SPEED;
            break;
        }
    } else if (k == OBJ_COIN) {
        if (OBJ_BOX(SIM_BOX_COIN) && !is_used(key)) {
            set_used(key);
            P.coins |= (uint8_t)(1u << GTI_COIN(info));
            P.events |= EV_COIN;
            P.ev_cell = key;
        }
    }
}

/* Hazards, orbs, pads, portals and coins touching the outer box. Cells are
 * visited in the order the float version visits its objects (columns left
 * to right, each from the top down), over the columns and rows whose
 * objects can reach the box at all (a portal reaches a block above and
 * below its cell, an orb 0.1 beyond it); gs_ring_obj says which cells of a
 * column hold an object. */
static void touch_objects(void)
{
    uint16_t rows, own_rows;
    uint8_t n, k, r1;
    if (!s_near_obj) return;
    s_cw = s_hw;
    s_ch = s_hh;
    me_x();
    me_y();
    own_rows = rows_of(my0i, my1i);
    rows = rows_of(my0i - 1, my1i + 1);
    if (!rows) return;
    r1 = r_hi;
    s_cx = mx0i - 1;
    n = (uint8_t)(mx1i - mx0i) + 3;
    for (k = 0; k < n; k++, s_cx++) {
        uint8_t ci, cy;
        uint16_t m, bit, own;
        if ((uint16_t)s_cx >= gs_width) continue;
        ci = (uint8_t)s_cx & (GS_RING - 1);
        m = gs_ring_obj[ci] & rows;
        if (!m) continue;
        /* the cells around the box's own: only an object reaching out of
         * its cell (a big saw, an orb, a portal) can touch the box there */
        own = k == 0 || k == n - 1 ? 0 : own_rows;
        for (cy = r1, bit = BIT[r1];; cy--, bit >>= 1)
            if (m & bit) {
                uint8_t info = gs_tile_info[gs_ring[(ci << 4) | cy]];
                if ((own & bit) || REACHES_OUT(GTI_KIND(info))) touch_cell(cy, info);
                m &= ~bit;
                if (!m) break;
            }
    }
}

/* --- a sub-step --- */

/* PERF=1 builds time the parts of the ticks (in scanlines, for the
 * emulator test): physics, moving x, solids, inner box, objects, the
 * broad phase */
#if defined(__SDCC) && defined(GS_PERF)
uint16_t perf_now(void);
uint8_t g_perf_sim[6];
static uint8_t s_part[6];
static uint16_t s_part_t;
#define PART_BEGIN() (s_part_t = perf_now())
#define PART(i)                                   \
    do {                                          \
        uint16_t now_ = perf_now();               \
        s_part[i] += (uint8_t)(now_ - s_part_t);  \
        s_part_t = now_;                          \
    } while (0)
#else
#define PART_BEGIN()
#define PART(i)
#endif

/* fallen far below the ground or flown far above the level */
static uint8_t y_out(void)
{
    int16_t yi = Y_HI(), top = (int16_t)gs_height + SIM_Y_ABOVE;
    return yi < SIM_Y_MIN || yi > top || (yi == top && Y_LO());
}

static void substep(uint8_t held)
{
    uint8_t was_grounded;

    switch (P.mode) {
    case MODE_CUBE:
        if (P.grounded && held) {
            P.vy = P.grav > 0 ? SIM_CUBE_JUMP : -SIM_CUBE_JUMP;
            P.grounded = 0;
            P.buf = 0;
            P.jumps++;
            P.events |= EV_JUMP;
        } else if (P.grav > 0) {
            P.vy -= SIM_CUBE_GRAV;
            if (P.vy < -SIM_CUBE_MAXFALL) P.vy = -SIM_CUBE_MAXFALL;
        } else {
            P.vy += SIM_CUBE_GRAV;
            if (P.vy > SIM_CUBE_MAXFALL) P.vy = SIM_CUBE_MAXFALL;
        }
        break;
    case MODE_SHIP:
        if (P.grav > 0) {
            P.vy += held ? SIM_SHIP_UP : -SIM_SHIP_DOWN;
            if (P.vy > SIM_SHIP_MAXRISE) P.vy = SIM_SHIP_MAXRISE;
            if (P.vy < -SIM_SHIP_MAXFALL) P.vy = -SIM_SHIP_MAXFALL;
        } else {
            P.vy -= held ? SIM_SHIP_UP : -SIM_SHIP_DOWN;
            if (P.vy < -SIM_SHIP_MAXRISE) P.vy = -SIM_SHIP_MAXRISE;
            if (P.vy > SIM_SHIP_MAXFALL) P.vy = SIM_SHIP_MAXFALL;
        }
        break;
    case MODE_BALL:
        if (P.grounded && P.buf) {
            P.grav = (int8_t)-P.grav;
            P.vy = P.grav > 0 ? -SIM_BALL_KICK : SIM_BALL_KICK;
            P.grounded = 0;
            P.buf = 0;
            P.jumps++;
            P.events |= EV_JUMP | EV_GRAVITY;
        }
        if (P.grav > 0) {
            P.vy -= SIM_BALL_GRAV;
            if (P.vy < -SIM_BALL_MAXFALL) P.vy = -SIM_BALL_MAXFALL;
        } else {
            P.vy += SIM_BALL_GRAV;
            if (P.vy > SIM_BALL_MAXFALL) P.vy = SIM_BALL_MAXFALL;
        }
        break;
    case MODE_UFO:
        if (P.buf) {
            P.vy = P.grav > 0 ? SIM_UFO_JUMP : -SIM_UFO_JUMP;
            P.buf = 0;
            P.grounded = 0;
            P.jumps++;
            P.events |= EV_JUMP;
        }
        if (P.grav > 0) {
            P.vy -= SIM_UFO_GRAV;
            if (P.vy < -SIM_UFO_MAXFALL) P.vy = -SIM_UFO_MAXFALL;
        } else {
            P.vy += SIM_UFO_GRAV;
            if (P.vy > SIM_UFO_MAXFALL) P.vy = SIM_UFO_MAXFALL;
        }
        break;
    case MODE_WAVE: {
        int8_t dir = held ? P.grav : (int8_t)-P.grav;
        P.vy = dir > 0 ? (int16_t)P.speed : -(int16_t)P.speed;
        break;
    }
    }

    PART(0);
    s_prev_y = P.y;
    was_grounded = P.grounded;
    advance_x();
    {
        /* y += vy */
        uint16_t lo = Y_LO(), nlo = lo + (uint16_t)P.vy;
        SET_Y(Y_HI() + (P.vy < 0 ? -1 : 0) + (nlo < lo), nlo);
    }
    PART(1);

    s_hw = HIT_W[P.mode];
    s_hh = HIT_H[P.mode];
    P.grounded = 0;
    resolve_solids();
    if (P.grounded && !was_grounded) P.events |= EV_LAND;
    PART(2);

    if (inner_hits_solid() || y_out()) {
        P.dead = 1;
        P.events |= EV_DEATH;
        return;
    }
    PART(3);

    touch_objects();
    PART(4);
    if (P.dead) return;

    if (X_HI() >= gs_width) {
        P.done = 1;
        P.events |= EV_COMPLETE;
    }
}

/* Is anything within reach of the player during this tick? It moves less
 * than 0.27 block forwards and 0.5 up or down in a tick (the fastest speed,
 * a yellow pad's launch); the tests reach a cell beyond the box's (objects:
 * one column and row further). */
static void broad_phase(void)
{
    uint16_t rows, solid = 0, obj = 0;
    uint8_t n;
    s_cw = HIT_W[P.mode];
    s_ch = HIT_H[P.mode];
    me_x();
    me_y();
    rows = rows_of(my0i - 2, my1i + 2);
    s_cx = mx0i - 1;
    n = (uint8_t)(mx1i - mx0i) + 4;
    do {
        if ((uint16_t)s_cx < gs_width) {
            uint8_t ci = (uint8_t)s_cx & (GS_RING - 1);
            solid |= gs_ring_solid[ci];
            obj |= gs_ring_obj[ci];
        }
        s_cx++;
    } while (--n);
    s_near_solid = (solid & rows) != 0;
    s_near_obj = (obj & rows) != 0;
}

void gs_tick(uint8_t held, uint8_t pressed) GS_BANKED
{
    uint8_t i;
    P.events = 0;
    if (P.dead || P.done) return;
    if (pressed) P.buf = 1;
    if (!held) P.buf = 0;
    PART_BEGIN();
    broad_phase();
    PART(5);
    for (i = 0; i < SIM_SUBSTEPS; i++) {
        substep(held);
        if (P.dead || P.done) break;
    }
#if defined(__SDCC) && defined(GS_PERF)
    for (i = 0; i < 6; i++) {
        g_perf_sim[i] = s_part[i];
        s_part[i] = 0;
    }
#endif
    P.ticks++;
}
