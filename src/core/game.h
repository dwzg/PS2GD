/* Entry points the frontends call. */
#ifndef PD_GAME_H
#define PD_GAME_H

#include "common.h"

void game_init(void);
/* Advance one 1/60 s tick with the currently held buttons (BTN_* mask). */
void game_tick(uint32_t held);
void game_render(void);

/* Save progress now (frontends call this before shutting down). */
void game_flush_save(void);

/* One-line description of the current state (for logs/debugging). */
void game_status(char *buf, int cap);

#endif
