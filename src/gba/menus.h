/* The menus on the GBA (menus.c). */
#ifndef PD_GBA_MENUS_H
#define PD_GBA_MENUS_H

#include "game_internal.h"

/* At start: what the menus show of the levels. */
void menus_init(void);
/* A menu screen was opened: its text and sprites. */
void menus_enter(int screen);
/* The menu's text and sprites for this frame (over its backdrop). */
void menus_draw(int screen);
/* What a screen draws after the world behind it, that can wait for a frame
 * with the time (the title's texts: it draws the title's run in between) */
void menus_draw_text(int screen);

/* A menu's backdrop: the ground and squares scrolled by `scroll` blocks
 * (gba_draw.c). */
void draw_menu_world(const Palette *pal, float scroll);

#endif
