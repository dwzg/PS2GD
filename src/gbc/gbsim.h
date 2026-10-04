/*
 * The player's physics on the Game Boy Color: the rules of
 * src/core/sim_rules.h, as src/core/sim.c implements them for every other
 * platform, rewritten for an 8-bit CPU without a multiply instruction (see
 * gbsim.c). It must give exactly the same results as src/core/sim.c, tick
 * by tick: `gbc_tool difftest` checks that on every level the Game Boy
 * plays, and the emulator test plays the ROM against it.
 *
 * Plain C that builds with SDCC for the ROM and with the host compiler for
 * gbc_tool. Units as in sim_rules.h.
 */
#ifndef GBSIM_H
#define GBSIM_H

#include <stdint.h>

#include "tileset.h"

/* On the Game Boy the simulation's code has a ROM bank of its own (bank 0
 * is too small for it), called through GBDK's bank-switching calls. */
#ifdef __SDCC
#define GS_BANKED __banked
#else
#define GS_BANKED
#endif

/* Levels are stored as columns of GS_ROWS cells (rows above the art are
 * empty, see tileset.h for what a cell holds): a level higher than that
 * isn't on the Game Boy. */
#define GS_ROWS 16

/* Objects used this attempt (orbs, pads, portals, coins): only the ones
 * near the player can be touched again, so a short ring of cell keys is
 * enough (gbc_tool checks that no level on the Game Boy needs more). */
#define GS_USED_N 8

typedef struct {
    uint32_t x;     /* 16.16 blocks */
    uint16_t xfrac; /* and 1/65536 of its last unit */
    int32_t y;      /* 16.16 blocks */
    int16_t vy;     /* 1/65536 block per sub-step */
    uint16_t speed; /* SIM_SPEED_HI[speed_idx] */
    int8_t floor_y, ceil_y; /* corridor rows for non-cube modes */
    int8_t grav;            /* +1 normal, -1 upside down */
    uint8_t mode;
    uint8_t speed_idx;
    uint8_t grounded;
    uint8_t buf; /* an unconsumed press is being held */
    uint8_t dead;
    uint8_t done;
    uint8_t coins; /* bitmask collected this attempt */
    uint16_t events;
    uint16_t ticks;
    uint16_t jumps;
    uint16_t ev_cell; /* cell key (column << 4 | row) of the last orb/pad/portal/coin event */
    uint8_t used_pos;
    uint16_t used[GS_USED_N]; /* cell key + 1, 0 = free */
} GsPlayer;

/* The level being played: GS_ROWS cells per column, column-major. */
extern const uint8_t *gs_cells;
extern uint16_t gs_width;  /* columns; the finish line is at x = width */
extern uint8_t gs_height;  /* rows the level was authored with (+2, like Level.height) */

/* The simulation reads the cells from a copy of the columns around the
 * player (the level stays in its own ROM bank): column c is at
 * gs_ring[(c % GS_RING) * GS_ROWS]. gs_step() brings it up to date and
 * ticks; within a tick the player moves less than a block, so columns
 * from 3 behind to 4 ahead are enough. */
#define GS_RING 8
extern uint8_t gs_ring[GS_RING * GS_ROWS];
/* rows of each ring column that hold an object (not empty, not solid),
 * that are solid, and that are whole blocks */
extern uint16_t gs_ring_obj[GS_RING], gs_ring_solid[GS_RING], gs_ring_block[GS_RING];

/* The player the simulation works on. */
extern GsPlayer gs_p;

void gs_reset(uint8_t start_speed) GS_BANKED;

/* Copy the columns around the player into gs_ring, then advance one 1/60 s
 * tick. held = button down this tick, pressed = went down this tick. */
void gs_step(uint8_t held, uint8_t pressed);
void gs_ring_update(void);
/* Forget the ring's columns (a new level). */
void gs_ring_reset(void);
void gs_tick(uint8_t held, uint8_t pressed) GS_BANKED;

/* Hitbox half extents (16.16) of the current mode. */
void gs_hitbox(uint16_t *hw, uint16_t *hh) GS_BANKED;


#endif
