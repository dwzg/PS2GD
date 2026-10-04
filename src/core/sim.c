/*
 * The player's physics: the reference implementation of sim_rules.h (see
 * there for the units). Plain integer C, written to be read: a platform
 * that needs it faster has its own copy (src/gbc/gbsim.c), which must give
 * the same results tick by tick.
 */
#include "sim.h"

/* for drawing only: the speeds of SIM_SPEED_HI / LO in blocks per second */
const float SIM_SPEEDS[4] = {8.4f, 10.5f, 13.125f, 15.75f};

#define ONE SIM_ONE

static const uint16_t SPEED_HI[4] = SIM_SPEED_HI;
static const uint16_t SPEED_LO[4] = SIM_SPEED_LO;
static const int16_t ORB_YELLOW_V[4] = SIM_ORB_YELLOW_V;
static const int16_t ORB_PINK_V[4] = SIM_ORB_PINK_V;
static const int16_t PAD_YELLOW_V[4] = SIM_PAD_YELLOW_V;
static const int16_t PAD_PINK_V[4] = SIM_PAD_PINK_V;
static const int32_t HIT_W[MODE_COUNT] = SIM_HIT_W;
static const int32_t HIT_H[MODE_COUNT] = SIM_HIT_H;
static const int32_t INNER_W[MODE_COUNT] = SIM_INNER_W;
static const int32_t INNER_H[MODE_COUNT] = SIM_INNER_H;

/* A box in 16.16 blocks. */
typedef struct {
    int32_t x0, y0, x1, y1;
} Box;

/* whole blocks of a 16.16 position, rounded down */
static int32_t cell_of(int32_t v) { return v >> 16; }

static Box player_box(const Player *p, int32_t hw, int32_t hh)
{
    Box b = {p->x - hw, p->y - hh, p->x + hw, p->y + hh};
    return b;
}

/* Does the box overlap the box x0..x1, y0..y1 of cell (cx, cy)? */
static int overlaps(const Box *a, int32_t cx, int32_t cy, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t bx = cx * ONE, by = cy * ONE;
    return a->x0 < bx + x1 - SIM_OVERLAP_EPS && a->x1 > bx + x0 + SIM_OVERLAP_EPS &&
           a->y0 < by + y1 - SIM_OVERLAP_EPS && a->y1 > by + y0 + SIM_OVERLAP_EPS;
}
#define OVERLAPS(a, cx, cy, box) overlaps(a, cx, cy, box)

/* Does the box touch the circle of radius r around the centre of cell (cx, cy)? */
static int touches_circle(const Box *a, int32_t cx, int32_t cy, int32_t r)
{
    int32_t ccx = cx * ONE + ONE / 2, ccy = cy * ONE + ONE / 2;
    int32_t px = ccx < a->x0 ? a->x0 : (ccx > a->x1 ? a->x1 : ccx);
    int32_t py = ccy < a->y0 ? a->y0 : (ccy > a->y1 ? a->y1 : ccy);
    int32_t dx = px > ccx ? px - ccx : ccx - px, dy = py > ccy ? py - ccy : ccy - py;
    if (dx >= r || dy >= r) return 0;
    dx >>= 8;
    dy >>= 8;
    r >>= 8;
    return dx * dx + dy * dy < r * r;
}

/* Does the box overlap the solid t in cell (cx, cy)? */
static int overlaps_solid(const Box *a, int t, int32_t cx, int32_t cy)
{
    if (t == OBJ_SLAB_LO) return overlaps(a, cx, cy, 0, 0, ONE, ONE / 2);
    if (t == OBJ_SLAB_HI) return overlaps(a, cx, cy, 0, ONE / 2, ONE, ONE);
    return overlaps(a, cx, cy, 0, 0, ONE, ONE);
}

static int is_block(const Level *L, int32_t cx, int32_t cy) { return level_solid_at(L, cx, cy) == OBJ_BLOCK; }
static int is_used(const Player *p, int id) { return sim_used(p, id); }
static void set_used(Player *p, int id) { p->used[id >> 5] |= 1u << (id & 31); }

void sim_reset(Player *p, const Level *L)
{
    memset(p, 0, sizeof(*p));
    p->y = ONE / 2;
    p->grav = 1;
    p->mode = MODE_CUBE;
    p->speed_idx = (uint8_t)clampi(L ? L->start_speed : 1, 0, 3);
    p->grounded = 1;
    p->floor_y = 0;
    p->ceil_y = SIM_CORRIDOR;
    p->ev_obj = -1;
}

static void enter_mode(Player *p, int mode, int cy)
{
    if (mode != MODE_CUBE) {
        int fl = cy - SIM_CORRIDOR / 2 + 1;
        if (fl < 0) fl = 0;
        p->floor_y = (int8_t)fl;
        p->ceil_y = (int8_t)(fl + SIM_CORRIDOR);
    }
    if (p->mode != mode) {
        p->vy = (int16_t)(p->vy / 2);
        p->mode = (uint8_t)mode;
    }
    p->grounded = 0;
}

/* Put the player's low edge on y (land on a solid), or its high edge (hang
 * under one). */
static void put_low(Player *p, int32_t y, int32_t hh) { p->y = y + hh; p->vy = 0; }
static void put_high(Player *p, int32_t y, int32_t hh) { p->y = y - hh; p->vy = 0; }

/* Land on / bump into solid cells overlapping the outer box: column by
 * column, each from the bottom up. */
static void resolve_solids(Player *p, const Level *L, int32_t hw, int32_t hh, int32_t prev_y)
{
    const int can_ceil = p->mode != MODE_CUBE;
    const int32_t step_up = p->mode == MODE_WAVE ? SIM_STEP_UP_WAVE : SIM_STEP_UP;
    const int32_t prev_low = prev_y - hh, prev_high = prev_y + hh;
    Box me = player_box(p, hw, hh);
    int32_t cx0 = cell_of(me.x0), cx1 = cell_of(me.x1);
    int32_t cy0 = cell_of(me.y0), cy1 = cell_of(me.y1);

    for (int32_t cx = cx0; cx <= cx1; cx++) {
        for (int32_t cy = cy0; cy <= cy1; cy++) {
            int t = level_solid_at(L, cx, cy);
            if (!t) continue;
            me = player_box(p, hw, hh);
            if (!overlaps_solid(&me, t, cx, cy)) continue;

            /* the solid's top and bottom, and whether nothing solid covers them */
            int32_t top = cy * ONE + (t == OBJ_SLAB_LO ? ONE / 2 : ONE);
            int32_t bottom = cy * ONE + (t == OBJ_SLAB_HI ? ONE / 2 : 0);
            int top_open = t != OBJ_BLOCK || !is_block(L, cx, cy + 1);
            int bottom_open = t != OBJ_BLOCK || !is_block(L, cx, cy - 1);
            /* the low edge was above the top before, or is a step-up below it */
            int onto = prev_low >= top - SIM_SNAP_EPS || me.y0 >= top - step_up;
            int under = prev_high <= bottom + SIM_SNAP_EPS || me.y1 <= bottom + step_up;

            /* "feet" and "head" relative to gravity */
            if (p->grav > 0) {
                if (p->vy <= 0 && top_open && onto) {
                    put_low(p, top, hh);
                    p->grounded = 1;
                } else if (can_ceil && p->vy >= 0 && bottom_open && under) {
                    put_high(p, bottom, hh);
                }
            } else {
                if (p->vy >= 0 && bottom_open && under) {
                    put_high(p, bottom, hh);
                    p->grounded = 1;
                } else if (can_ceil && p->vy <= 0 && top_open && onto) {
                    put_low(p, top, hh);
                }
            }
        }
    }

    /* World floor / corridor bounds never kill, they just stop you. */
    if (p->mode == MODE_CUBE) {
        if (p->y - hh < 0) {
            p->y = hh;
            if (p->vy < 0) p->vy = 0;
            if (p->grav > 0) p->grounded = 1;
        }
    } else {
        if (p->y - hh < p->floor_y * ONE) {
            p->y = p->floor_y * ONE + hh;
            if (p->vy < 0) p->vy = 0;
            if (p->grav > 0) p->grounded = 1;
        }
        if (p->y + hh > p->ceil_y * ONE) {
            p->y = p->ceil_y * ONE - hh;
            if (p->vy > 0) p->vy = 0;
            if (p->grav < 0) p->grounded = 1;
        }
    }
}

static int inner_hits_solid(const Player *p, const Level *L)
{
    Box in = player_box(p, INNER_W[p->mode], INNER_H[p->mode]);
    for (int32_t cx = cell_of(in.x0); cx <= cell_of(in.x1); cx++) {
        for (int32_t cy = cell_of(in.y0); cy <= cell_of(in.y1); cy++) {
            int t = level_solid_at(L, cx, cy);
            if (t && overlaps_solid(&in, t, cx, cy)) return 1;
        }
    }
    return 0;
}

static int hazard_hit(const LevelObj *o, const Box *me)
{
    switch (o->type) {
    case OBJ_SPIKE_UP: return OVERLAPS(me, o->cx, o->cy, SIM_BOX_SPIKE_UP);
    case OBJ_SPIKE_DOWN: return OVERLAPS(me, o->cx, o->cy, SIM_BOX_SPIKE_DOWN);
    case OBJ_SPIKE_SM_UP: return OVERLAPS(me, o->cx, o->cy, SIM_BOX_SPIKE_SM_UP);
    case OBJ_SPIKE_SM_DOWN: return OVERLAPS(me, o->cx, o->cy, SIM_BOX_SPIKE_SM_DOWN);
    case OBJ_SAW_BIG: return touches_circle(me, o->cx, o->cy, SIM_SAW_BIG_R);
    case OBJ_SAW_SMALL: return touches_circle(me, o->cx, o->cy, SIM_SAW_SMALL_R);
    default: return 0;
    }
}

/* the vy of a launch v away from the floor */
static int16_t launch(const Player *p, int v) { return (int16_t)(p->grav > 0 ? v : -v); }

static void flip_gravity(Player *p)
{
    p->grav = (int8_t)-p->grav;
    p->events |= EV_GRAVITY;
}

static void apply_orb(Player *p, int type)
{
    if (p->mode == MODE_WAVE) {
        if (type == OBJ_ORB_BLUE || type == OBJ_ORB_GREEN) flip_gravity(p);
        return;
    }
    switch (type) {
    case OBJ_ORB_YELLOW: p->vy = launch(p, ORB_YELLOW_V[p->mode]); break;
    case OBJ_ORB_PINK: p->vy = launch(p, ORB_PINK_V[p->mode]); break;
    case OBJ_ORB_BLUE:
        flip_gravity(p);
        p->vy = launch(p, -SIM_ORB_BLUE_V);
        break;
    case OBJ_ORB_GREEN:
        flip_gravity(p);
        p->vy = launch(p, ORB_YELLOW_V[p->mode]);
        break;
    }
    p->grounded = 0;
}

static void apply_pad(Player *p, int type)
{
    if (p->mode == MODE_WAVE) {
        if (type == OBJ_PAD_BLUE) flip_gravity(p);
        return;
    }
    switch (type) {
    case OBJ_PAD_YELLOW: p->vy = launch(p, PAD_YELLOW_V[p->mode]); break;
    case OBJ_PAD_PINK: p->vy = launch(p, PAD_PINK_V[p->mode]); break;
    case OBJ_PAD_BLUE:
        flip_gravity(p);
        p->vy = launch(p, -SIM_PAD_BLUE_V);
        break;
    }
    p->grounded = 0;
}

static void enter_portal(Player *p, const LevelObj *o)
{
    switch (o->type) {
    case OBJ_PORTAL_CUBE: enter_mode(p, MODE_CUBE, o->cy); p->events |= EV_PORTAL; break;
    case OBJ_PORTAL_SHIP: enter_mode(p, MODE_SHIP, o->cy); p->events |= EV_PORTAL; break;
    case OBJ_PORTAL_BALL: enter_mode(p, MODE_BALL, o->cy); p->events |= EV_PORTAL; break;
    case OBJ_PORTAL_UFO: enter_mode(p, MODE_UFO, o->cy); p->events |= EV_PORTAL; break;
    case OBJ_PORTAL_WAVE: enter_mode(p, MODE_WAVE, o->cy); p->events |= EV_PORTAL; break;
    case OBJ_PORTAL_GRAV_FLIP:
    case OBJ_PORTAL_GRAV_NORMAL: {
        int8_t ng = o->type == OBJ_PORTAL_GRAV_FLIP ? -1 : 1;
        if (ng != p->grav) {
            p->grav = ng;
            p->vy = (int16_t)(p->vy * 2 / 5);
            p->grounded = 0;
            p->events |= EV_GRAVITY;
        }
        p->events |= EV_PORTAL;
        break;
    }
    default:
        p->speed_idx = (uint8_t)(o->type - OBJ_SPEED_0);
        p->events |= EV_SPEED;
        break;
    }
}

/* Hazards, orbs, pads, portals and coins touching the outer box, in the
 * level's object order: columns left to right, each from the top down
 * (an object reaches at most a block out of its cell). */
static void touch_objects(Player *p, const Level *L, int32_t hw, int32_t hh)
{
    Box me = player_box(p, hw, hh);
    int c0 = clampi(cell_of(me.x0) - 1, 0, L->width);
    int c1 = clampi(cell_of(me.x1) + 1, 0, L->width - 1);
    for (int c = c0; c <= c1; c++) {
        for (int i = L->col_start[c]; i < L->col_start[c + 1]; i++) {
            const LevelObj *o = &L->objs[i];
            if (o->type <= OBJ_SAW_SMALL) {
                if (!p->dead && hazard_hit(o, &me)) {
                    p->dead = 1;
                    p->events |= EV_DEATH;
                }
                continue;
            }
            if (is_used(p, o->id)) continue;
            if (o->type <= OBJ_ORB_GREEN) {
                if (p->buf && OVERLAPS(&me, o->cx, o->cy, SIM_BOX_ORB)) {
                    set_used(p, o->id);
                    p->buf = 0;
                    apply_orb(p, o->type);
                    p->events |= EV_ORB;
                    p->ev_obj = (int16_t)i;
                }
            } else if (o->type <= OBJ_PAD_BLUE) {
                if ((o->flags & OF_CEILING) ? OVERLAPS(&me, o->cx, o->cy, SIM_BOX_PAD_CEILING)
                                            : OVERLAPS(&me, o->cx, o->cy, SIM_BOX_PAD)) {
                    set_used(p, o->id);
                    apply_pad(p, o->type);
                    p->events |= EV_PAD;
                    p->ev_obj = (int16_t)i;
                }
            } else if (o->type <= OBJ_SPEED_3) {
                if (OVERLAPS(&me, o->cx, o->cy, SIM_BOX_PORTAL)) {
                    set_used(p, o->id);
                    p->ev_obj = (int16_t)i;
                    enter_portal(p, o);
                }
            } else if (o->type == OBJ_COIN) {
                if (OVERLAPS(&me, o->cx, o->cy, SIM_BOX_COIN)) {
                    set_used(p, o->id);
                    p->coins |= (uint8_t)(1u << ((o->flags >> 4) & 3));
                    p->events |= EV_COIN;
                    p->ev_obj = (int16_t)i;
                }
            }
        }
    }
}

/* x += the speed's step, exactly (x and xf are x in 1/2^32 blocks) */
static void advance_x(Player *p)
{
    uint32_t xf = (uint32_t)p->xf + SPEED_LO[p->speed_idx];
    p->x += SPEED_HI[p->speed_idx] + (xf >> 16);
    p->xf = (uint16_t)xf;
}

static void accelerate(Player *p, int a, int maxfall)
{
    /* a towards the floor, at most maxfall */
    if (p->grav > 0) {
        p->vy = (int16_t)(p->vy - a);
        if (p->vy < -maxfall) p->vy = (int16_t)-maxfall;
    } else {
        p->vy = (int16_t)(p->vy + a);
        if (p->vy > maxfall) p->vy = (int16_t)maxfall;
    }
}

static void substep(Player *p, const Level *L, int held)
{
    switch (p->mode) {
    case MODE_CUBE:
        if (p->grounded && held) {
            p->vy = launch(p, SIM_CUBE_JUMP);
            p->grounded = 0;
            p->buf = 0;
            p->jumps++;
            p->events |= EV_JUMP;
        } else {
            accelerate(p, SIM_CUBE_GRAV, SIM_CUBE_MAXFALL);
        }
        break;
    case MODE_SHIP: {
        int up = held ? SIM_SHIP_UP : -SIM_SHIP_DOWN;
        if (p->grav > 0) {
            p->vy = (int16_t)(p->vy + up);
            if (p->vy > SIM_SHIP_MAXRISE) p->vy = SIM_SHIP_MAXRISE;
            if (p->vy < -SIM_SHIP_MAXFALL) p->vy = -SIM_SHIP_MAXFALL;
        } else {
            p->vy = (int16_t)(p->vy - up);
            if (p->vy < -SIM_SHIP_MAXRISE) p->vy = -SIM_SHIP_MAXRISE;
            if (p->vy > SIM_SHIP_MAXFALL) p->vy = SIM_SHIP_MAXFALL;
        }
        break;
    }
    case MODE_BALL:
        if (p->grounded && p->buf) {
            p->grav = (int8_t)-p->grav;
            p->vy = launch(p, -SIM_BALL_KICK);
            p->grounded = 0;
            p->buf = 0;
            p->jumps++;
            p->events |= EV_JUMP | EV_GRAVITY;
        }
        accelerate(p, SIM_BALL_GRAV, SIM_BALL_MAXFALL);
        break;
    case MODE_UFO:
        if (p->buf) {
            p->vy = launch(p, SIM_UFO_JUMP);
            p->buf = 0;
            p->grounded = 0;
            p->jumps++;
            p->events |= EV_JUMP;
        }
        accelerate(p, SIM_UFO_GRAV, SIM_UFO_MAXFALL);
        break;
    case MODE_WAVE:
        p->vy = launch(p, held ? SPEED_HI[p->speed_idx] : -SPEED_HI[p->speed_idx]);
        break;
    }

    int32_t prev_y = p->y;
    int was_grounded = p->grounded;
    advance_x(p);
    p->y += p->vy;

    int32_t hw = HIT_W[p->mode], hh = HIT_H[p->mode];
    p->grounded = 0;
    resolve_solids(p, L, hw, hh, prev_y);
    if (p->grounded && !was_grounded) p->events |= EV_LAND;

    if (inner_hits_solid(p, L) || p->y < SIM_Y_MIN * ONE || p->y > (L->height + SIM_Y_ABOVE) * ONE) {
        p->dead = 1;
        p->events |= EV_DEATH;
        return;
    }

    touch_objects(p, L, hw, hh);
    if (p->dead) return;

    if (p->x >= L->width * ONE) {
        p->done = 1;
        p->events |= EV_COMPLETE;
    }
}

void sim_tick(Player *p, const Level *L, int held, int pressed)
{
    p->events = 0;
    if (p->dead || p->done) return;
    if (pressed) p->buf = 1;
    if (!held) p->buf = 0;
    for (int i = 0; i < SIM_SUBSTEPS; i++) {
        substep(p, L, held);
        if (p->dead || p->done) break;
    }
    p->ticks++;
}

void sim_coast(Player *p, uint16_t t)
{
    int32_t hh = HIT_H[p->mode];
    int n = SIM_SUBSTEPS + (t > SIM_EXIT_WAIT ? t - SIM_EXIT_WAIT : 0);
    if (n > SIM_EXIT_MAX) n = SIM_EXIT_MAX;
    for (int i = 0; i < n; i++) advance_x(p);
    /* level out, within the bounds that stop the player */
    p->y += (int32_t)p->vy * SIM_SUBSTEPS;
    p->vy = (int16_t)(p->vy / 2);
    if (p->mode == MODE_CUBE) {
        if (p->y < hh) p->y = hh;
    } else {
        if (p->y < p->floor_y * ONE + hh) p->y = p->floor_y * ONE + hh;
        if (p->y > p->ceil_y * ONE - hh) p->y = p->ceil_y * ONE - hh;
    }
}
