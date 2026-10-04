/*
 * Deterministic player simulation: the reference implementation of the
 * rules in sim_rules.h, in integers, so that it gives the same results on
 * every platform (and as fast as a CPU without floating point can). One
 * call to sim_tick advances the player by 1/60 s in fixed sub-steps; it
 * never touches rendering or audio so the level solver can run it millions
 * of times.
 */
#ifndef PD_SIM_H
#define PD_SIM_H

#include "level.h"

typedef struct {
    int32_t x;   /* 16.16 blocks (negative: before the start) */
    uint16_t xf; /* and 1/65536 of its last unit */
    int32_t y;   /* 16.16 blocks */
    int16_t vy;  /* 1/65536 block per sub-step */
    int8_t floor_y, ceil_y; /* corridor rows for non-cube modes */
    int8_t grav;            /* +1 normal, -1 upside down */
    uint8_t mode;
    uint8_t speed_idx;
    uint8_t grounded;
    uint8_t buf;  /* an unconsumed button press is being held */
    uint8_t dead;
    uint8_t done;
    uint8_t coins; /* bitmask collected this attempt */
    uint16_t events; /* EV_* of the most recent tick */
    uint16_t ticks;
    uint16_t jumps;
    /* object that raised the last orb/pad/portal/coin event (for effects) */
    int16_t ev_obj;
    uint32_t used[LEVEL_MAX_INTERACT / 32];
} Player;

void sim_reset(Player *p, const Level *L);

/* Advance one 1/60 s tick. held = button down this tick, pressed = went down this tick. */
void sim_tick(Player *p, const Level *L, int held, int pressed);

/* A tick of the run past the finish line, t ticks after it: the player
 * levels out and speeds off (SIM_EXIT_*). Nothing else happens. */
void sim_coast(Player *p, uint16_t t);

/* Has the player used up the interactable object id this attempt? */
static inline int sim_used(const Player *p, int id) { return (p->used[id >> 5] >> (id & 31)) & 1u; }

/* For drawing: the player in blocks and blocks per second. */
#define SIM_FIX_TO_F (1.0f / 65536.0f)
static inline float sim_x(const Player *p) { return (float)p->x * SIM_FIX_TO_F; }
static inline float sim_y(const Player *p) { return (float)p->y * SIM_FIX_TO_F; }
static inline float sim_vy(const Player *p) { return (float)p->vy * (SIM_SUBSTEPS * 60 * SIM_FIX_TO_F); }
/* Speeds of the speed portals 0..3 in blocks per second. */
extern const float SIM_SPEEDS[4];
static inline float sim_speed(const Player *p) { return SIM_SPEEDS[p->speed_idx]; }

/* Corridor height used by ship/ball/ufo/wave. */
#define CORRIDOR_H ((float)SIM_CORRIDOR)

#endif
