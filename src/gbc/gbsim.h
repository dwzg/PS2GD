/*
 * Fixed-point player simulation for the Game Boy Color build.
 *
 * The same rules as src/core/sim.c (same constants, hitboxes, sub-steps and
 * order of checks), in integers: the Game Boy has no floating point, and its
 * CPU has no multiply instruction. This file is plain C that builds with
 * SDCC for the ROM and with the host compiler for gbc_tool, whose solver
 * proves every level can be finished with exactly this code.
 *
 * Units: positions are 16.16 fixed point in blocks; velocities are in
 * 1/65536 block per sub-step (1/240 s), so integrating is one addition.
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

#define GS_SUBSTEPS 4
#define GS_ONE 65536L /* one block */
/* Levels are stored as columns of GS_ROWS cells (rows above the art are
 * empty, see tileset.h for what a cell holds). */
#define GS_ROWS 16
#define GS_CORRIDOR 10 /* ship/ball/UFO/wave corridor height in rows */

enum { GM_CUBE = 0, GM_SHIP, GM_BALL, GM_UFO, GM_WAVE };

/* Events raised during a tick (same bits as src/core/sim.h). */
#define GE_JUMP 0x0001u
#define GE_LAND 0x0002u
#define GE_ORB 0x0004u
#define GE_PAD 0x0008u
#define GE_PORTAL 0x0010u
#define GE_GRAVITY 0x0020u
#define GE_COIN 0x0040u
#define GE_DEATH 0x0080u
#define GE_COMPLETE 0x0100u
#define GE_SPEED 0x0200u

/* Objects used this attempt (orbs, pads, portals, coins): only the ones
 * near the player can be touched again, so a short ring of cell keys is
 * enough (gbc_tool checks that no level needs more). */
#define GS_USED_N 8

typedef struct {
    uint32_t x;     /* 16.16 blocks */
    uint16_t xfrac; /* and 1/65536 of its last unit (see advance_x) */
    int32_t y;      /* 16.16 blocks */
    int16_t vy;     /* 1/65536 block per sub-step */
    uint16_t speed; /* 1/65536 block per sub-step */
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

/* Per-sub-step horizontal speeds of the four speed portals. */
extern const uint16_t GS_SPEEDS[4];

#endif
