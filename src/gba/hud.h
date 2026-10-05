/*
 * The play screen's overlays on the GBA (play_draw.c's draw_hud,
 * draw_pause and draw_results, and the attempt counter in the world): text
 * and panels on BG0, the texts that grow or move in sprites.
 */
#ifndef PD_GBA_HUD_H
#define PD_GBA_HUD_H

#include "game_internal.h"

/* A run begins: the screen's text cleared. */
void hud_enter(void);
/* The frame's overlay, before anything of the frame is drawn: the pause
 * menu or the results begun, shown (once drawn whole: they are drawn out of
 * sight over the frames they take) or gone. */
void hud_begin(const PlayState *ps);
/* The pause menu is on the screen this frame (the world under it darkened:
 * no see-through sprites, no flash). */
int hud_pause_shown(void);
/* The sprites in front of the run (NEW BEST, LEVEL COMPLETE), under the
 * text: before the run's sprites. */
void hud_sprites_front(const PlayState *ps);
/* The attempt counter in the world, behind the blocks: after them. */
void hud_sprites_world(const PlayState *ps);
/* The text and panels on BG0, and the effects of the frame (the flash,
 * the pause's dimming). */
void hud_draw(const PlayState *ps);

#endif
