/* Player vehicle drawing (cube, ship, ball, ufo, wave). */
#ifndef PD_ICONS_H
#define PD_ICONS_H

#include "common.h"

/*
 * cx, cy: screen center; size: one block in pixels; angle: radians,
 * clockwise on screen. flip mirrors vertically (upside-down gravity).
 */
void icon_draw_cube(float cx, float cy, float size, float angle, int icon, Color c1, Color c2);
void icon_draw_ship(float cx, float cy, float size, float angle, int flip, int icon, Color c1, Color c2);
void icon_draw_ball(float cx, float cy, float size, float angle, int icon, Color c1, Color c2);
void icon_draw_ufo(float cx, float cy, float size, float angle, int flip, int icon, Color c1, Color c2);
void icon_draw_wave(float cx, float cy, float size, float angle, Color c1, Color c2);

void icon_draw_mode(int mode, float cx, float cy, float size, float angle, int flip, int icon,
                    Color c1, Color c2);

#endif
