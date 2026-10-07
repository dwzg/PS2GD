/*
 * Sprites of a run on the GBA: the player, the level's orbs, pads, portals,
 * coins and saws, the practice checkpoints, the finish line and the
 * core's particles (fx.c), drawn from the core's state in the world's
 * camera (world.h).
 */
#ifndef PD_GBA_SPRITES_H
#define PD_GBA_SPRITES_H

#include "game_internal.h"

void sprites_init(void);
/* The sprites' palettes as the level's objects use them (after a menu
 * used some banks for its own). */
void sprites_palettes(void);

/* Sprite sizes (shape and size): squares, wide, tall */
enum { SQ8, SQ16, SQ32, SQ64, W16x8, W32x8, W32x16, W64x32, T8x16, T8x32, T16x32, T32x64 };
/* A sprite at screen (x, y) (its top left), if any of it can show. */
void spr(int x, int y, int size, int tile, int pal, uint16_t a0, uint16_t a1);
/* An affine sprite centred on (cx, cy): w x h, drawn in a box twice its
 * size if dbl; nothing if aff < 0 (no matrix was left). */
void spr_aff(int cx, int cy, int w, int h, int size, int tile, int pal, int aff, int dbl, uint16_t a0);
/* the priority of the sprites added from now on: 0 over the text, 1 the world's */
void spr_prio(int prio);
/* an angle in radians as video_aff takes it */
uint16_t spr_angle(float rad);
/* A vehicle of the player's (MODE_*) centred at (x, y): turned by angle
 * (radians, clockwise: its frame drawn so, art.h VF_*), upside down if
 * flip (the ship and the UFO). */
void spr_vehicle(int mode, int x, int y, float angle, int flip);
/* The frame of a picture turned through period radians in n frames (the
 * nearest to angle), and of one turned from lo to hi (the nearest, at
 * the ends beyond them). */
int spr_frame_turn(float angle, float period, int n);
int spr_frame_range(float angle, float lo, float hi, int n);
/* The garage's choice: the icon's vehicles into VRAM, the colours into
 * the player's palette (only does work when they change). */
void sprites_garage(int icon, int col1, int col2);
/* The run's sprites, front to back: particles, the player, checkpoints,
 * the level's objects (after any sprites already added this frame). */
void sprites_run(const PlayState *ps, int show_player);
/* The glows sprites_run found (after the sprites added since, which are
 * drawn over them). */
void sprites_glows(void);
/* Particles of a space (FX_WORLD in the world's camera, FX_SCREEN on the
 * screen, 640x448 virtual pixels scaled to the GBA's). */
void sprites_fx(int space);

#endif
