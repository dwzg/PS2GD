/* What the DS frontend's files give each other. */
#ifndef PD_NDS_PLATFORM_H
#define PD_NDS_PLATFORM_H

#include <stdint.h>

/* gfx_nds.c: the 3D engine as gfx.h */
void gfx_nds_init(void);
void gfx_nds_begin(void);
void gfx_nds_end(void);
/* after game_init (the font): the outlines of the game's text, made now */
void gfx_nds_prepare_outlines(void);
/* right after the vertical blank starts: textures made since are copied in */
void gfx_nds_vblank(void);
/* primitives left out (past the engine's polygons a frame) since boot */
int gfx_nds_dropped(void);
/* the most polygons a frame since the last call */
int gfx_nds_max_polys(void);

/* audio_nds.c: returns 0 when there is no sound (no file system) */
int audio_nds_init(void);
/* once a frame, from the loop, after the frame's drawing: writes the
 * sound ahead, and reads the songs ahead from the card when time_left
 * (else only what is needed soon) */
void audio_nds_update(int time_left);
/* where the sound channels play: samples since they started */
uint32_t audio_nds_play_pos(void);

/* save_nds.c */
int save_nds_init(int argc, char **argv);

#endif
