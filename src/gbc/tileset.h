/*
 * Level cells of the Game Boy Color build.
 *
 * A level is stored as one byte per cell, and that byte is the background
 * tile drawn there (VRAM bank 0): a block's tile already shows which of its
 * sides are exposed, a portal is three tiles stacked on its cell, and the
 * ceiling of a ship, ball, UFO or wave corridor is drawn in from its portal
 * on. What the simulation needs to know about a tile (what kind of object
 * it is, a coin's number, a pad on a ceiling) is in gs_tile_info.
 */
#ifndef GB_TILESET_H
#define GB_TILESET_H

#include <stdint.h>

/* Object kinds, numbered like enum ObjType in src/core/level.h. */
enum {
    GT_NONE = 0,
    GT_BLOCK,
    GT_SLAB_LO,
    GT_SLAB_HI,
    GT_SPIKE_UP,
    GT_SPIKE_DOWN,
    GT_SPIKE_SM_UP,
    GT_SPIKE_SM_DOWN,
    GT_SAW_BIG,
    GT_SAW_SMALL,
    GT_ORB_YELLOW,
    GT_ORB_PINK,
    GT_ORB_BLUE,
    GT_ORB_GREEN,
    GT_PAD_YELLOW,
    GT_PAD_PINK,
    GT_PAD_BLUE,
    GT_PORTAL_CUBE,
    GT_PORTAL_SHIP,
    GT_PORTAL_BALL,
    GT_PORTAL_UFO,
    GT_PORTAL_WAVE,
    GT_PORTAL_FLIP,
    GT_PORTAL_NORMAL,
    GT_SPEED_0,
    GT_SPEED_1,
    GT_SPEED_2,
    GT_SPEED_3,
    GT_COIN
};

/* gs_tile_info[tile]: kind in the low 5 bits, a coin's number in bits 5-6,
 * bit 7 set for a pad hanging from a ceiling. */
#define GTI_KIND(i) ((i) & 31u)
#define GTI_COIN(i) (((i) >> 5) & 3u)
#define GTI_CEILING 0x80u

/* Exposed sides of a block (as EDGE_* in src/core/level.h). */
#define GE_EDGE_L 1
#define GE_EDGE_R 2
#define GE_EDGE_T 4
#define GE_EDGE_B 8

/* Tiles that can appear in a level. */
enum {
    T_EMPTY = 0,
    T_BLOCK = 1,         /* + exposed-edge mask, 16 tiles */
    T_SLAB_LO = 17,      /* + left/right edge mask, 4 tiles */
    T_SLAB_HI = 21,      /* + left/right edge mask, 4 tiles */
    T_SPIKE_UP = 25,
    T_SPIKE_DOWN,
    T_SPIKE_SM_UP,
    T_SPIKE_SM_DOWN,
    T_SAW,               /* small saw; its pixels are animated */
    T_ORB_YELLOW = 30,
    T_ORB_PINK,
    T_ORB_BLUE,
    T_ORB_GREEN,
    T_PAD_YELLOW = 34,
    T_PAD_PINK,
    T_PAD_BLUE,
    T_PAD_YELLOW_C = 37, /* on a ceiling */
    T_PAD_PINK_C,
    T_PAD_BLUE_C,
    T_PORTAL = 40,       /* + 3 * (kind - GT_PORTAL_CUBE) + part: 0 top, 1 middle (the object), 2 bottom */
    T_COIN = 73,         /* + coin number */
    /* scenery, all with a separator line variant next (every 4 blocks) */
    T_GROUND_TOP = 77,   /* the ground's surface; also a corridor's raised floor */
    T_GROUND_LOW = 79,   /* the ground below it */
    T_CEIL_EDGE = 81,    /* a corridor's ceiling, its surface at the bottom */
    T_GROUND_FILL = 83,  /* above a corridor's ceiling */
    T_LEVEL_COUNT = 85
};

#define T_PORTAL_KINDS (GT_SPEED_3 - GT_PORTAL_CUBE + 1)

extern const uint8_t gs_tile_info[T_LEVEL_COUNT];

#endif
