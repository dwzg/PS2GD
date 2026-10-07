/*
 * The level's world on the GBA: the blocks and spikes (BG1), the ground and
 * the corridor's bands (BG2), the squares behind (BG3), the gradient and
 * the colours, for a camera and a level palette from the core's state.
 * 12 pixels to a block; world pixel x = x * 12, and world pixel y grows
 * downwards from the ground's surface (y_px = -y * 12).
 */
#ifndef PD_GBA_WORLD_H
#define PD_GBA_WORLD_H

#include "level.h"
#include "theme.h"
#include "art.h"
#include "gen.h"
#include "video.h"

/* the shared tiles (counted from char block 1, video.h): the blank one,
 * the ground's, the squares', then text (ui.c) */
#define WORLD_TILE_BLANK SHARED_TILE_FIRST
#define WORLD_TILES_END (SHARED_TILE_FIRST + 1 + GT_COUNT + SQ_TILE_COUNT)

typedef struct {
    const Level *L;      /* NULL: no level (a menu's backdrop) */
    float cam_x, cam_y;  /* the core's camera: the screen's left and bottom edges, in blocks */
    const Palette *pal;
    float pulse;         /* the beat, 0..1 */
    float corr_floor, corr_ceil, corr_alpha;
    float squares_x;     /* how far the squares behind have scrolled (blocks; usually cam_x) */
    int loops;           /* the title's demo run: it goes back by DEMO_LOOP blocks onto the same picture */
    int fade;            /* 0..16: the screen faded so far to black (video_fade) */
    int flash;           /* 0..256: a run's flash, white added (video_fade) */
} WorldView;

void world_init(void);
/* Draw the frame's world: scrolling, the tiles coming into view, colours. */
void world_draw(const WorldView *v);
/* The camera of the latest world_draw in world pixels (the screen's top
 * left), for sprites placed in the world. */
extern int32_t g_cam_px, g_cam_py;
/* A world colour as the GBA's 15 bits. */
uint16_t world_rgb15(Color c);
/* The background's gradient colour at screen line y (8-bit channels). */
Color world_backdrop(int y);
/* The ground's colour a little under its line, as last drawn (what the
 * hints at the bottom of the screen are over). */
Color world_ground(void);
/* The glow around blocks at its strongest, as last drawn: the colour
 * added (the blocks' edge, 0.20 of it, 0.30 on the beat). */
Color world_halo(void);

#endif
