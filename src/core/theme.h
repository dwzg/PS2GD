/* Color palettes for levels and the player icon kit. */
#ifndef PD_THEME_H
#define PD_THEME_H

#include "common.h"

typedef struct {
    Color bg_top, bg_bot;
    Color ground, ground_line;
    Color block_fill, block_edge;
    Color accent;
} Palette;

#define PALETTE_COUNT 10
extern const Palette g_palettes[PALETTE_COUNT];

void palette_lerp(Palette *out, const Palette *a, const Palette *b, float t);

#define PLAYER_COLOR_COUNT 14
extern const Color g_player_colors[PLAYER_COLOR_COUNT];

#define ICON_COUNT 8
extern const char *const g_icon_names[ICON_COUNT];

/* Gameplay object colors. */
#define COL_ORB_YELLOW RGB(255, 226, 40)
#define COL_ORB_PINK RGB(255, 84, 200)
#define COL_ORB_BLUE RGB(50, 170, 255)
#define COL_ORB_GREEN RGB(70, 255, 110)
#define COL_PORTAL_CUBE RGB(70, 255, 130)
#define COL_PORTAL_SHIP RGB(255, 90, 210)
#define COL_PORTAL_BALL RGB(255, 96, 48)
#define COL_PORTAL_UFO RGB(255, 186, 30)
#define COL_PORTAL_WAVE RGB(40, 222, 255)
#define COL_PORTAL_FLIP RGB(255, 232, 60)
#define COL_PORTAL_NORMAL RGB(60, 150, 255)
#define COL_COIN RGB(255, 206, 52)

#endif
