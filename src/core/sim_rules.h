/*
 * The rules of the player's physics, in fixed point: every number that
 * decides where the player goes, what it lands on and what it touches.
 *
 * src/core/sim.c is the reference implementation of these rules, in
 * portable integer C; every platform runs it, or an implementation of its
 * own that gives exactly the same results tick by tick (the Game Boy
 * Color's, src/gbc/gbsim.c, checked by `gbc_tool difftest`). So the game
 * plays the same on every platform, and a level the solver (pd_tool) proves
 * beatable is beatable everywhere.
 *
 * Only plain C and <stdint.h> here: the Game Boy's compiler reads it too.
 *
 * Units: positions are 16.16 fixed point in blocks (y = 0 is the ground's
 * surface, y grows upwards); x has 16 more bits of fraction on top (Player
 * .xf), so that the speeds below are exact. Velocities are in 1/65536 block
 * per sub-step, accelerations in 1/65536 block per sub-step per sub-step.
 * A tick (1/60 s) is SIM_SUBSTEPS sub-steps.
 */
#ifndef PD_SIM_RULES_H
#define PD_SIM_RULES_H

#include <stdint.h>

/* What a level cell holds: a solid (in the level's grid) or an object. */
enum ObjType {
    OBJ_NONE = 0,
    /* solids */
    OBJ_BLOCK,
    OBJ_SLAB_LO,
    OBJ_SLAB_HI,
    /* hazards */
    OBJ_SPIKE_UP,
    OBJ_SPIKE_DOWN,
    OBJ_SPIKE_SM_UP,
    OBJ_SPIKE_SM_DOWN,
    OBJ_SAW_BIG,
    OBJ_SAW_SMALL,
    /* orbs (activated by pressing while touching) */
    OBJ_ORB_YELLOW,
    OBJ_ORB_PINK,
    OBJ_ORB_BLUE,
    OBJ_ORB_GREEN,
    /* pads (activated by touching) */
    OBJ_PAD_YELLOW,
    OBJ_PAD_PINK,
    OBJ_PAD_BLUE,
    /* portals */
    OBJ_PORTAL_CUBE,
    OBJ_PORTAL_SHIP,
    OBJ_PORTAL_BALL,
    OBJ_PORTAL_UFO,
    OBJ_PORTAL_WAVE,
    OBJ_PORTAL_GRAV_FLIP,
    OBJ_PORTAL_GRAV_NORMAL,
    OBJ_SPEED_0,
    OBJ_SPEED_1,
    OBJ_SPEED_2,
    OBJ_SPEED_3,
    /* collectibles */
    OBJ_COIN,
    OBJ_TYPE_COUNT
};

enum PlayerMode { MODE_CUBE = 0, MODE_SHIP, MODE_BALL, MODE_UFO, MODE_WAVE, MODE_COUNT };

/* Events raised during a tick (for effects and sound). */
#define EV_JUMP 0x0001u
#define EV_LAND 0x0002u
#define EV_ORB 0x0004u
#define EV_PAD 0x0008u
#define EV_PORTAL 0x0010u
#define EV_GRAVITY 0x0020u
#define EV_COIN 0x0040u
#define EV_DEATH 0x0080u
#define EV_COMPLETE 0x0100u
#define EV_SPEED 0x0200u

#define SIM_SUBSTEPS 4
#define SIM_ONE 65536L /* one block */
/* Past the finish line (sim_coast, only drawn: nothing collides) the
 * player stops climbing or falling, and after SIM_EXIT_WAIT ticks speeds
 * up by a sub-step's distance a tick, up to SIM_EXIT_MAX sub-steps' a
 * tick, off the screen. */
#define SIM_EXIT_WAIT 12
#define SIM_EXIT_MAX 48

/* Ship, ball, UFO and wave fly in a corridor this many rows high, centred
 * on the portal that started it: its floor is the portal's row - 4. */
#define SIM_CORRIDOR 10

/* The four speed portals' speeds, 8.4, 10.5, 13.125 and 15.75 blocks per
 * second, per sub-step: whole 1/65536 blocks and 1/65536 of those. */
#define SIM_SPEED_HI {2293, 2867, 3584, 4300}
#define SIM_SPEED_LO {49807, 13107, 0, 52429}

/* Cube: apex ~2.3 blocks, ~0.44 s airtime. */
#define SIM_CUBE_GRAV 108     /* 95 blocks/s^2 */
#define SIM_CUBE_JUMP 5707    /* 20.9 blocks/s */
#define SIM_CUBE_MAXFALL 7100 /* 26 blocks/s */
#define SIM_SHIP_UP 66        /* 58 */
#define SIM_SHIP_DOWN 55      /* 48 */
#define SIM_SHIP_MAXRISE 2348 /* 8.6 */
#define SIM_SHIP_MAXFALL 2731 /* 10 */
#define SIM_BALL_GRAV 93      /* 82 */
#define SIM_BALL_KICK 1638    /* 6 */
#define SIM_BALL_MAXFALL 6554 /* 24 */
#define SIM_UFO_GRAV 71       /* 62 */
#define SIM_UFO_JUMP 3495     /* 12.8 */
#define SIM_UFO_MAXFALL 4369  /* 16 */
/* The wave moves up or down as fast as forwards: vy = +-SIM_SPEED_HI. */

/* Orb and pad launch speeds for the cube, ship, ball and UFO: a cube's
 * jump times 1, 0.72, 1.4 and 0.86, and that times 1, 0.62, 0.75 and 0.8
 * for the mode. The blue ones flip gravity and push away from the new
 * floor at half / 0.6 of a jump, whatever the mode. On a wave, the blue and
 * green orbs and the blue pad only flip gravity, the others do nothing. */
#define SIM_ORB_YELLOW_V {5707, 3538, 4280, 4566}
#define SIM_ORB_PINK_V {4109, 2548, 3082, 3287}
#define SIM_PAD_YELLOW_V {7990, 4954, 5992, 6392}
#define SIM_PAD_PINK_V {4908, 3043, 3681, 3926}
#define SIM_ORB_BLUE_V 2854
#define SIM_PAD_BLUE_V 3424

/* Boxes overlap if they do by more than this (0.001 block) on both axes. */
#define SIM_OVERLAP_EPS 66
/* The player lands on a solid it was above before the sub-step (within
 * 0.02 block), or whose top it is at most a step-up below (0.25 block, a
 * wave 0.06); the same for hanging under one (not as a cube). */
#define SIM_SNAP_EPS 1311
#define SIM_STEP_UP 16384
#define SIM_STEP_UP_WAVE 3932

/* Player hitbox half extents by mode (cube, ship, ball, UFO, wave): the
 * outer box lands on solids and touches objects, the inner one dies in a
 * solid. 0.5/0.45/0.45/0.45/0.16 x 0.5/0.30/0.45/0.38/0.16 and
 * 0.18/0.15/0.16/0.16/0.10 x 0.18/0.12/0.15/0.15/0.10 blocks. */
#define SIM_HIT_W {32768, 29491, 29491, 29491, 10486}
#define SIM_HIT_H {32768, 19661, 29491, 24904, 10486}
#define SIM_INNER_W {11796, 9830, 10486, 10486, 6554}
#define SIM_INNER_H {11796, 7864, 9830, 9830, 6554}

/* Boxes of the objects: x0, y0, x1, y1 from the bottom left of their cell. */
#define SIM_BOX_SPIKE_UP 26214L, 13107L, 39322L, 39322L       /* 0.4..0.6 x 0.2..0.6 */
#define SIM_BOX_SPIKE_DOWN 26214L, 26214L, 39322L, 52429L     /* 0.4..0.8 */
#define SIM_BOX_SPIKE_SM_UP 26214L, 3277L, 39322L, 22938L     /* 0.05..0.35 */
#define SIM_BOX_SPIKE_SM_DOWN 26214L, 42598L, 39322L, 62259L  /* 0.65..0.95 */
#define SIM_BOX_ORB -6554L, -6554L, 72090L, 72090L            /* the cell and 0.1 around it */
#define SIM_BOX_PAD 3277L, 0L, 62259L, 19661L                 /* 0.05..0.95 x 0..0.3 */
#define SIM_BOX_PAD_CEILING 3277L, 45875L, 62259L, 65536L     /* 0.7..1, on a ceiling */
#define SIM_BOX_PORTAL 9830L, -65536L, 55706L, 131072L        /* 0.15..0.85, a block above and below */
#define SIM_BOX_COIN 6554L, 6554L, 58982L, 58982L             /* 0.1..0.9 */
/* Saws are circles around the cell's centre (0.72 and 0.36 blocks). The
 * player's box touches one if, measured in whole 1/256 blocks (rounded
 * down), its nearest point is closer than the radius. */
#define SIM_SAW_BIG_R 47186
#define SIM_SAW_SMALL_R 23593

/* Out of the level, dead: below y = -6, or above its height + 24 rows. */
#define SIM_Y_MIN (-6)
#define SIM_Y_ABOVE 24

/* The player starts at x = 0, y = 0.5 (on the ground), as a cube. */

#endif
