/*
 * Deterministic player simulation. One call to sim_tick advances the player
 * by 1/60 s using fixed sub-steps; it never touches rendering or audio so the
 * level solver can run it millions of times.
 */
#ifndef PD_SIM_H
#define PD_SIM_H

#include "level.h"

/* Horizontal speeds in blocks per second for speed portals 0..3. */
extern const float SIM_SPEEDS[4];

/* Events raised during a tick (for effects and sound). */
enum {
    EV_JUMP = 1u << 0,
    EV_LAND = 1u << 1,
    EV_ORB = 1u << 2,
    EV_PAD = 1u << 3,
    EV_PORTAL = 1u << 4,
    EV_GRAVITY = 1u << 5,
    EV_COIN = 1u << 6,
    EV_DEATH = 1u << 7,
    EV_COMPLETE = 1u << 8,
    EV_SPEED = 1u << 9
};

typedef struct {
    float x, y, vy;
    float speed;
    float floor_y, ceil_y; /* corridor bounds for non-cube modes */
    int8_t grav;           /* +1 normal, -1 upside down */
    uint8_t mode;
    uint8_t speed_idx;
    uint8_t grounded;
    uint8_t buf;  /* an unconsumed button press is being held */
    uint8_t dead;
    uint8_t done;
    uint8_t coins; /* bitmask collected this attempt */
    uint32_t events; /* events of the most recent tick */
    int ticks;
    int jumps;
    /* object that raised the last orb/pad/portal event (for effects) */
    int16_t ev_obj;
    uint32_t used[LEVEL_MAX_INTERACT / 32];
} Player;

void sim_reset(Player *p, const Level *L);

/* Advance one 1/60 s tick. held = button down this tick, pressed = went down this tick. */
void sim_tick(Player *p, const Level *L, int held, int pressed);

/* Player hitbox half extents for the current mode (outer box). */
void sim_hitbox(const Player *p, float *hw, float *hh);

/* Corridor height used by ship/ball/ufo/wave. */
#define CORRIDOR_H 10.0f

#endif
