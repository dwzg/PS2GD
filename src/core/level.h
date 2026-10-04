/*
 * Level data.
 *
 * Levels are authored as ASCII art (see src/levels/ and docs/LEVEL_FORMAT.md).
 * A parsed level is a solid grid (blocks/slabs) plus a list of objects sorted
 * by column (each column's from the top down) for spikes, saws, orbs, pads,
 * portals and coins.
 */
#ifndef PD_LEVEL_H
#define PD_LEVEL_H

#include "common.h"
#include "sim_rules.h" /* object types, player modes */

/* Object flags */
#define OF_CEILING 0x01 /* pad/spike attached to a ceiling */

/* Block edge mask (which sides are exposed), used for drawing outlines. */
#define EDGE_L 0x01
#define EDGE_R 0x02
#define EDGE_T 0x04
#define EDGE_B 0x08

#define LEVEL_MAX_TRIGGERS 64
#define LEVEL_MAX_INTERACT 1024

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint16_t id; /* index into the per-attempt "used" bitset (interactables) */
    int16_t cx, cy;
} LevelObj;

typedef struct {
    float x;     /* world x where the change starts */
    uint8_t pal; /* palette index */
} ColorTrigger;

typedef struct {
    char name[32];
    char author[24];
    int song;
    int difficulty; /* 0 easy .. 5 demon-ish */
    int stars;
    int start_speed;
    int start_pal;

    int width, height; /* in cells */
    uint8_t *grid;     /* width*height solid types (OBJ_BLOCK..OBJ_SLAB_HI) */
    uint8_t *edges;    /* width*height exposed-edge mask */

    LevelObj *objs;
    int nobjs;
    int *col_start; /* width+1 entries: objs in column c are [col_start[c], col_start[c+1]) */

    int ninteract;
    int ncoins;
    float end_x;

    ColorTrigger trig[LEVEL_MAX_TRIGGERS];
    int ntrig;
} Level;

/* Parse an ASCII level. Returns NULL on allocation failure. */
Level *level_parse(const char *const *src);
void level_free(Level *L);

static inline int level_solid_at(const Level *L, int cx, int cy)
{
    if (cx < 0 || cy < 0 || cx >= L->width || cy >= L->height) return 0;
    return L->grid[cy * L->width + cx];
}

/* Static metadata about built-in levels (available without parsing). */
typedef struct {
    const char *const *src;
} LevelEntry;

extern const LevelEntry g_levels[];
extern const int g_level_count;

/* Read the header fields (#name etc.) of a built-in level cheaply. */
typedef struct {
    char name[32];
    int difficulty, stars, song, pal, ncoins;
} LevelInfo;

void level_info(int index, LevelInfo *out);

const char *difficulty_name(int d);

#endif
