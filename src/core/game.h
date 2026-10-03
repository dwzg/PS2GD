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

/* Save progress now (frontends call this before shutting down). */
void game_flush_save(void);

/* One-line description of the current state (for logs/debugging). */
void game_status(char *buf, int cap);

/* Number of level attempts started since boot (lets test harnesses line
 * scripted input up with the start of a run). */
unsigned game_attempts_started(void);

#endif
