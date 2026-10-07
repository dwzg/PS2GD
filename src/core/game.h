/* Entry points the frontends call. */
#ifndef PD_GAME_H
#define PD_GAME_H

#include "common.h"

void game_init(void);
/* Advance one 1/60 s tick with the currently held buttons (BTN_* mask). */
void game_tick(uint32_t held);
/*
 * Draw the current frame. The game ticks at a fixed 60 Hz but the display
 * may run at another rate (50 Hz PAL) or drop a frame, so frontends pass how
 * far real time has got between the previous tick and the latest one
 * (0 = draw the previous tick's state, 1 = the latest); moving things are
 * interpolated so the scrolling stays even.
 */
void game_render(float alpha);
/* Set up the drawing (game_init calls it). The presentation provides this
 * and game_render: game_draw.c in the vector family (PC, PS2, PSP). */
void game_draw_init(void);

/* Save progress now (frontends call this before shutting down). */
void game_flush_save(void);

/*
 * The system has put a menu of its own over the game (on = 1: the PSP's
 * HOME dialog) or taken it away (0). Meanwhile the frontend goes on drawing
 * frames but doesn't tick the game, nor make the ticks up afterwards, so
 * nothing moves (menus, the title's demo, fades, a run, the results), and
 * it silences the sound (audio_suspend, with the synth). A run is paused
 * here as START pauses it, so that it waits for the player when the menu
 * goes, and one being faded into is paused on its first tick. A button
 * held as the menu goes (the one that closed it) is no press, and a
 * direction held no repeat, until it has been let go.
 */
void game_suspend(int on);

/* One-line description of the current state (for logs/debugging). */
void game_status(char *buf, int cap);

/* Number of level attempts started since boot (lets test harnesses line
 * scripted input up with the start of a run). */
unsigned game_attempts_started(void);

#endif
