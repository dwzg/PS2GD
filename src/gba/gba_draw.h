/*
 * The GBA's presentation of the core's game: it provides game_draw_init()
 * and game_render() (game.h), as the vector renderer does for the other
 * platforms, drawing the same state with tiles and sprites.
 */
#ifndef PD_GBA_DRAW_H
#define PD_GBA_DRAW_H

/* The display hardware, before game_init. */
void gba_draw_init_hw(void);
/* In the vertical blank: show the frame game_render prepared. */
void gba_draw_commit(void);
/* In a vertical blank with no new frame ready: the shown one again. */
void gba_draw_hold(void);
/* beat_pulse() for the frame being drawn (worked out once a frame: its
 * expf takes thousands of cycles) */
extern float g_frame_pulse;

#endif
