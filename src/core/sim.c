#include "sim.h"

/* Speed portal values in blocks/second (slow, normal, fast, faster). */
const float SIM_SPEEDS[4] = {8.4f, 10.5f, 13.125f, 15.75f};

#define SUBSTEPS 4

/* Cube: apex ~2.3 blocks, ~0.44 s airtime. */
#define CUBE_GRAV 95.0f
#define CUBE_JUMP 20.9f
#define CUBE_MAXFALL 26.0f

#define SHIP_UP 58.0f
#define SHIP_DOWN 48.0f
#define SHIP_MAXRISE 8.6f
#define SHIP_MAXFALL 10.0f

#define BALL_GRAV 82.0f
#define BALL_KICK 6.0f
#define BALL_MAXFALL 24.0f

#define UFO_GRAV 62.0f
#define UFO_JUMP 12.8f
#define UFO_MAXFALL 16.0f

#define SNAP_EPS 0.02f
#define OVERLAP_EPS 0.001f

typedef struct {
    float x0, y0, x1, y1;
} Box;

static int box_overlap(const Box *a, const Box *b)
{
    return a->x0 < b->x1 - OVERLAP_EPS && a->x1 > b->x0 + OVERLAP_EPS &&
           a->y0 < b->y1 - OVERLAP_EPS && a->y1 > b->y0 + OVERLAP_EPS;
}

static int box_circle(const Box *a, float cx, float cy, float r)
{
    float px = clampf(cx, a->x0, a->x1), py = clampf(cy, a->y0, a->y1);
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy < r * r;
}

static int solid_box(const Level *L, int cx, int cy, Box *b)
{
    int t = level_solid_at(L, cx, cy);
    if (!t) return 0;
    b->x0 = (float)cx;
    b->x1 = (float)cx + 1.0f;
    b->y0 = (float)cy;
    b->y1 = (float)cy + 1.0f;
    if (t == OBJ_SLAB_LO) b->y1 = cy + 0.5f;
    else if (t == OBJ_SLAB_HI) b->y0 = cy + 0.5f;
    return t;
}

void sim_hitbox(const Player *p, float *hw, float *hh)
{
    switch (p->mode) {
    case MODE_SHIP: *hw = 0.45f; *hh = 0.30f; break;
    case MODE_BALL: *hw = 0.45f; *hh = 0.45f; break;
    case MODE_UFO: *hw = 0.45f; *hh = 0.38f; break;
    case MODE_WAVE: *hw = 0.16f; *hh = 0.16f; break;
    default: *hw = 0.5f; *hh = 0.5f; break;
    }
}

static void inner_hitbox(const Player *p, float *hw, float *hh)
{
    switch (p->mode) {
    case MODE_SHIP: *hw = 0.15f; *hh = 0.12f; break;
    case MODE_WAVE: *hw = 0.10f; *hh = 0.10f; break;
    case MODE_CUBE: *hw = 0.18f; *hh = 0.18f; break;
    default: *hw = 0.16f; *hh = 0.15f; break;
    }
}

static int is_used(const Player *p, int id) { return (p->used[id >> 5] >> (id & 31)) & 1u; }
static void set_used(Player *p, int id) { p->used[id >> 5] |= 1u << (id & 31); }

void sim_reset(Player *p, const Level *L)
{
    memset(p, 0, sizeof(*p));
    p->x = 0.0f;
    p->y = 0.5f;
    p->grav = 1;
    p->mode = MODE_CUBE;
    p->speed_idx = (uint8_t)clampi(L ? L->start_speed : 1, 0, 3);
    p->speed = SIM_SPEEDS[p->speed_idx];
    p->grounded = 1;
    p->floor_y = 0.0f;
    p->ceil_y = CORRIDOR_H;
    p->ev_obj = -1;
}

static float orb_mult(int mode)
{
    switch (mode) {
    case MODE_SHIP: return 0.62f;
    case MODE_BALL: return 0.75f;
    case MODE_UFO: return 0.8f;
    default: return 1.0f;
    }
}

static void enter_mode(Player *p, int mode, const LevelObj *o)
{
    if (mode != MODE_CUBE) {
        float center = o->cy + 0.5f;
        float fl = floorf(center - CORRIDOR_H * 0.5f + 0.5f);
        if (fl < 0.0f) fl = 0.0f;
        p->floor_y = fl;
        p->ceil_y = fl + CORRIDOR_H;
    }
    if (p->mode != mode) {
        p->vy *= 0.5f;
        p->mode = (uint8_t)mode;
    }
    p->grounded = 0;
}

/* Land on / bump into solid cells overlapping the outer box. */
static void resolve_solids(Player *p, const Level *L, float hw, float hh, float prev_y)
{
    const int can_ceil = p->mode != MODE_CUBE;
    const float g = (float)p->grav;
    const float step_up = p->mode == MODE_WAVE ? 0.06f : 0.25f;
    Box me = {p->x - hw, p->y - hh, p->x + hw, p->y + hh};
    int cx0 = (int)floorf(me.x0), cx1 = (int)floorf(me.x1);
    int cy0 = (int)floorf(me.y0), cy1 = (int)floorf(me.y1);

    for (int cx = cx0; cx <= cx1; cx++) {
        for (int cy = cy0; cy <= cy1; cy++) {
            Box b;
            int t = solid_box(L, cx, cy, &b);
            if (!t) continue;
            me.y0 = p->y - hh;
            me.y1 = p->y + hh;
            if (!box_overlap(&me, &b)) continue;

            /* "feet" and "head" surfaces relative to gravity */
            if (g > 0.0f) {
                int top_exposed = t != OBJ_BLOCK || level_solid_at(L, cx, cy + 1) != OBJ_BLOCK;
                int bot_exposed = t != OBJ_BLOCK || level_solid_at(L, cx, cy - 1) != OBJ_BLOCK;
                float prev_bottom = prev_y - hh, prev_top = prev_y + hh;
                if (p->vy <= 0.0001f && top_exposed &&
                    (prev_bottom >= b.y1 - SNAP_EPS || b.y1 - me.y0 <= step_up)) {
                    p->y = b.y1 + hh;
                    p->vy = 0.0f;
                    p->grounded = 1;
                } else if (can_ceil && p->vy >= -0.0001f && bot_exposed &&
                           (prev_top <= b.y0 + SNAP_EPS || me.y1 - b.y0 <= step_up)) {
                    p->y = b.y0 - hh;
                    p->vy = 0.0f;
                }
            } else {
                int top_exposed = t != OBJ_BLOCK || level_solid_at(L, cx, cy - 1) != OBJ_BLOCK;
                int bot_exposed = t != OBJ_BLOCK || level_solid_at(L, cx, cy + 1) != OBJ_BLOCK;
                float prev_feet = prev_y + hh, prev_head = prev_y - hh;
                if (p->vy >= -0.0001f && top_exposed &&
                    (prev_feet <= b.y0 + SNAP_EPS || me.y1 - b.y0 <= step_up)) {
                    p->y = b.y0 - hh;
                    p->vy = 0.0f;
                    p->grounded = 1;
                } else if (can_ceil && p->vy <= 0.0001f && bot_exposed &&
                           (prev_head >= b.y1 - SNAP_EPS || b.y1 - me.y0 <= step_up)) {
                    p->y = b.y1 + hh;
                    p->vy = 0.0f;
                }
            }
        }
    }

    /* World floor / corridor bounds never kill, they just stop you. */
    if (p->mode == MODE_CUBE) {
        if (p->y - hh < 0.0f) {
            p->y = hh;
            if (p->vy < 0.0f) p->vy = 0.0f;
            if (g > 0.0f) p->grounded = 1;
        }
    } else {
        if (p->y - hh < p->floor_y) {
            p->y = p->floor_y + hh;
            if (p->vy < 0.0f) p->vy = 0.0f;
            if (g > 0.0f) p->grounded = 1;
        }
        if (p->y + hh > p->ceil_y) {
            p->y = p->ceil_y - hh;
            if (p->vy > 0.0f) p->vy = 0.0f;
            if (g < 0.0f) p->grounded = 1;
        }
    }
}

static int inner_hits_solid(const Player *p, const Level *L)
{
    float hw, hh;
    inner_hitbox(p, &hw, &hh);
    Box in = {p->x - hw, p->y - hh, p->x + hw, p->y + hh};
    for (int cx = (int)floorf(in.x0); cx <= (int)floorf(in.x1); cx++) {
        for (int cy = (int)floorf(in.y0); cy <= (int)floorf(in.y1); cy++) {
            Box b;
            if (solid_box(L, cx, cy, &b) && box_overlap(&in, &b)) return 1;
        }
    }
    return 0;
}

static int hazard_hit(const LevelObj *o, const Box *me)
{
    float x = o->cx, y = o->cy;
    Box h;
    switch (o->type) {
    case OBJ_SPIKE_UP: h = (Box){x + 0.4f, y + 0.2f, x + 0.6f, y + 0.6f}; break;
    case OBJ_SPIKE_DOWN: h = (Box){x + 0.4f, y + 0.4f, x + 0.6f, y + 0.8f}; break;
    case OBJ_SPIKE_SM_UP: h = (Box){x + 0.4f, y + 0.05f, x + 0.6f, y + 0.35f}; break;
    case OBJ_SPIKE_SM_DOWN: h = (Box){x + 0.4f, y + 0.65f, x + 0.6f, y + 0.95f}; break;
    case OBJ_SAW_BIG: return box_circle(me, x + 0.5f, y + 0.5f, 0.72f);
    case OBJ_SAW_SMALL: return box_circle(me, x + 0.5f, y + 0.5f, 0.36f);
    default: return 0;
    }
    return box_overlap(me, &h);
}

static void apply_orb(Player *p, int type)
{
    float m = orb_mult(p->mode);
    if (p->mode == MODE_WAVE) {
        if (type == OBJ_ORB_BLUE || type == OBJ_ORB_GREEN) {
            p->grav = (int8_t)-p->grav;
            p->events |= EV_GRAVITY;
        }
        return;
    }
    switch (type) {
    case OBJ_ORB_YELLOW: p->vy = CUBE_JUMP * m * p->grav; break;
    case OBJ_ORB_PINK: p->vy = CUBE_JUMP * 0.72f * m * p->grav; break;
    case OBJ_ORB_BLUE:
        p->grav = (int8_t)-p->grav;
        p->vy = -CUBE_JUMP * 0.5f * p->grav;
        p->events |= EV_GRAVITY;
        break;
    case OBJ_ORB_GREEN:
        p->grav = (int8_t)-p->grav;
        p->vy = CUBE_JUMP * m * p->grav;
        p->events |= EV_GRAVITY;
        break;
    }
    p->grounded = 0;
}

static void apply_pad(Player *p, int type)
{
    float m = orb_mult(p->mode);
    if (p->mode == MODE_WAVE) {
        if (type == OBJ_PAD_BLUE) {
            p->grav = (int8_t)-p->grav;
            p->events |= EV_GRAVITY;
        }
        return;
    }
    switch (type) {
    case OBJ_PAD_YELLOW: p->vy = CUBE_JUMP * 1.4f * m * p->grav; break;
    case OBJ_PAD_PINK: p->vy = CUBE_JUMP * 0.86f * m * p->grav; break;
    case OBJ_PAD_BLUE:
        p->grav = (int8_t)-p->grav;
        p->vy = -CUBE_JUMP * 0.6f * p->grav;
        p->events |= EV_GRAVITY;
        break;
    }
    p->grounded = 0;
}

static void touch_objects(Player *p, const Level *L, float hw, float hh)
{
    Box me = {p->x - hw, p->y - hh, p->x + hw, p->y + hh};
    int c0 = clampi((int)floorf(me.x0) - 2, 0, L->width);
    int c1 = clampi((int)floorf(me.x1) + 2, 0, L->width - 1);
    for (int c = c0; c <= c1; c++) {
        for (int i = L->col_start[c]; i < L->col_start[c + 1]; i++) {
            const LevelObj *o = &L->objs[i];
            float x = o->cx, y = o->cy;
            if (o->type <= OBJ_SAW_SMALL) {
                if (!p->dead && hazard_hit(o, &me)) {
                    p->dead = 1;
                    p->events |= EV_DEATH;
                }
                continue;
            }
            if (is_used(p, o->id)) continue;
            if (o->type <= OBJ_ORB_GREEN) {
                Box b = {x + 0.5f - 0.6f, y + 0.5f - 0.6f, x + 0.5f + 0.6f, y + 0.5f + 0.6f};
                if (p->buf && box_overlap(&me, &b)) {
                    set_used(p, o->id);
                    p->buf = 0;
                    apply_orb(p, o->type);
                    p->events |= EV_ORB;
                    p->ev_obj = (int16_t)i;
                }
            } else if (o->type <= OBJ_PAD_BLUE) {
                Box b = (o->flags & OF_CEILING) ? (Box){x + 0.05f, y + 0.7f, x + 0.95f, y + 1.0f}
                                                : (Box){x + 0.05f, y, x + 0.95f, y + 0.3f};
                if (box_overlap(&me, &b)) {
                    set_used(p, o->id);
                    apply_pad(p, o->type);
                    p->events |= EV_PAD;
                    p->ev_obj = (int16_t)i;
                }
            } else if (o->type <= OBJ_SPEED_3) {
                Box b = {x + 0.15f, y - 1.0f, x + 0.85f, y + 2.0f};
                if (!box_overlap(&me, &b)) continue;
                set_used(p, o->id);
                p->ev_obj = (int16_t)i;
                switch (o->type) {
                case OBJ_PORTAL_CUBE: enter_mode(p, MODE_CUBE, o); p->events |= EV_PORTAL; break;
                case OBJ_PORTAL_SHIP: enter_mode(p, MODE_SHIP, o); p->events |= EV_PORTAL; break;
                case OBJ_PORTAL_BALL: enter_mode(p, MODE_BALL, o); p->events |= EV_PORTAL; break;
                case OBJ_PORTAL_UFO: enter_mode(p, MODE_UFO, o); p->events |= EV_PORTAL; break;
                case OBJ_PORTAL_WAVE: enter_mode(p, MODE_WAVE, o); p->events |= EV_PORTAL; break;
                case OBJ_PORTAL_GRAV_FLIP:
                case OBJ_PORTAL_GRAV_NORMAL: {
                    int8_t ng = o->type == OBJ_PORTAL_GRAV_FLIP ? -1 : 1;
                    if (ng != p->grav) {
                        p->grav = ng;
                        p->vy *= 0.4f;
                        p->grounded = 0;
                        p->events |= EV_GRAVITY;
                    }
                    p->events |= EV_PORTAL;
                    break;
                }
                default:
                    p->speed_idx = (uint8_t)(o->type - OBJ_SPEED_0);
                    p->speed = SIM_SPEEDS[p->speed_idx];
                    p->events |= EV_SPEED;
                    break;
                }
            } else if (o->type == OBJ_COIN) {
                Box b = {x + 0.1f, y + 0.1f, x + 0.9f, y + 0.9f};
                if (box_overlap(&me, &b)) {
                    set_used(p, o->id);
                    p->coins |= (uint8_t)(1u << ((o->flags >> 4) & 3));
                    p->events |= EV_COIN;
                    p->ev_obj = (int16_t)i;
                }
            }
        }
    }
}

static void substep(Player *p, const Level *L, int held, float h)
{
    float g = (float)p->grav;

    switch (p->mode) {
    case MODE_CUBE:
        if (p->grounded && held) {
            p->vy = CUBE_JUMP * g;
            p->grounded = 0;
            p->buf = 0;
            p->jumps++;
            p->events |= EV_JUMP;
        } else {
            p->vy -= CUBE_GRAV * g * h;
            if (p->vy * g < -CUBE_MAXFALL) p->vy = -CUBE_MAXFALL * g;
        }
        break;
    case MODE_SHIP:
        p->vy += (held ? SHIP_UP : -SHIP_DOWN) * g * h;
        if (p->vy * g > SHIP_MAXRISE) p->vy = SHIP_MAXRISE * g;
        if (p->vy * g < -SHIP_MAXFALL) p->vy = -SHIP_MAXFALL * g;
        break;
    case MODE_BALL:
        if (p->grounded && p->buf) {
            p->grav = (int8_t)-p->grav;
            g = (float)p->grav;
            p->vy = -BALL_KICK * g;
            p->grounded = 0;
            p->buf = 0;
            p->jumps++;
            p->events |= EV_JUMP | EV_GRAVITY;
        }
        p->vy -= BALL_GRAV * g * h;
        if (p->vy * g < -BALL_MAXFALL) p->vy = -BALL_MAXFALL * g;
        break;
    case MODE_UFO:
        if (p->buf) {
            p->vy = UFO_JUMP * g;
            p->buf = 0;
            p->grounded = 0;
            p->jumps++;
            p->events |= EV_JUMP;
        }
        p->vy -= UFO_GRAV * g * h;
        if (p->vy * g < -UFO_MAXFALL) p->vy = -UFO_MAXFALL * g;
        break;
    case MODE_WAVE:
        p->vy = (held ? 1.0f : -1.0f) * p->speed * g;
        break;
    }

    float prev_y = p->y;
    int was_grounded = p->grounded;
    p->x += p->speed * h;
    p->y += p->vy * h;

    float hw, hh;
    sim_hitbox(p, &hw, &hh);
    p->grounded = 0;
    resolve_solids(p, L, hw, hh, prev_y);
    if (p->grounded && !was_grounded) p->events |= EV_LAND;

    if (inner_hits_solid(p, L) || p->y < -6.0f || p->y > L->height + 24.0f) {
        p->dead = 1;
        p->events |= EV_DEATH;
        return;
    }

    touch_objects(p, L, hw, hh);
    if (p->dead) return;

    if (p->x >= L->end_x) {
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
    const float h = TICK_DT / SUBSTEPS;
    for (int i = 0; i < SUBSTEPS; i++) {
        substep(p, L, held, h);
        if (p->dead || p->done) break;
    }
    p->ticks++;
}
