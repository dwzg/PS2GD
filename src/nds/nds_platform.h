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

/* what is drawn between these goes on the bottom screen (soft_nds.c),
 * drawn by the CPU, rather than to the 3D engine */
void gfx_nds_bottom_begin(void);
void gfx_nds_bottom_end(void);

/* soft_nds.c: the bottom screen's bitmap. What is drawn is queued and
 * drawn by soft_run, a command at a time until `until` (nds_clock), into
 * the bitmap that was the target when it was queued; coordinates in 1/16
 * pixel (soft_tri) or pixels (16.16 for soft_disc) */
void soft_init(void);
/* draw into fb (256x192) from now on, or into the screen's copy (NULL) */
void soft_target(uint16_t *fb);
uint16_t *soft_screen(void);
void soft_rect(int x0, int y0, int x1, int y1, uint32_t top, uint32_t bottom);
void soft_tri(const int16_t *xy, const uint32_t *c);
void soft_glyph(int x, int y, int px, int py, const uint8_t rows[7], uint32_t top, uint32_t bottom);
void soft_disc(int32_t cx, int32_t cy, int32_t r, uint32_t c);
/* a 256x192 bitmap copied into another (queued too) */
void soft_copy(uint16_t *dst, const uint16_t *src);
/* the screen's copy goes to the screen at the vertical blank after the
 * queue drew everything before this */
void soft_present(void);
int soft_busy(void);
void soft_run(uint32_t until);
void soft_vblank(void);

/* main_nds.c: the bus clock (33.5 MHz), counting up */
uint32_t nds_clock(void);

/* bottom_nds.c: what the bottom screen shows, drawn again when it changes */
void bottom_nds_init(void);
/* until: how long it may draw (nds_clock) */
void bottom_nds_update(uint32_t until);
/* right after the vertical blank starts: a new picture is shown */
void bottom_nds_vblank(void);

/* audio_nds.c: returns 0 when there is no sound (no file system) */
int audio_nds_init(void);
/* once a frame, from the loop, after the frame's drawing: writes the
 * sound ahead, and reads the songs ahead from the card if there is time
 * for it before `until` (nds_clock; else only what is needed soon) */
void audio_nds_update(uint32_t until);
/* the music written ahead as far as it can be (240 ms), before something
 * that holds up the loop for a while (writing a save) */
void audio_nds_hold(void);
/* (for the emulator test) the song playing as the frame's sound was
 * written, and its time as heard then, in samples of song time */
extern volatile int32_t g_audio_song, g_audio_heard;
/* where the sound channels play: samples since they started */
uint32_t audio_nds_play_pos(void);

/* save_nds.c */
int save_nds_init(int argc, char **argv);

#endif
