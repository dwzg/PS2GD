/*
 * Levels and palettes as gbc_tool exports them for the Game Boy Color
 * (build/gbc/gen/gbc_data.h declares the tables).
 */
#ifndef GB_LEVELDATA_H
#define GB_LEVELDATA_H

#include <stdint.h>

typedef struct {
    uint16_t x;  /* column where the colour change starts */
    uint8_t pal; /* palette index */
} GbTrigger;

typedef struct {
    const char *name;
    uint8_t difficulty, stars, song, speed, pal;
    uint8_t height; /* rows, as Level.height */
    uint8_t ncoins;
    uint8_t bank;   /* ROM bank holding the cells */
    uint8_t ntrig;
    uint16_t width;
    const uint8_t *cells; /* GS_ROWS tiles a column, in `bank` */
    const GbTrigger *trig;
} GbLevel;

/* Colours of one level palette (src/core/theme.c), as 8-bit RGB. */
enum {
    LC_SKY = 0,
    LC_FILL,        /* block fill over the sky */
    LC_EDGE,        /* block and spike outlines */
    LC_SPIKE,       /* spike and saw bodies */
    LC_GROUND,
    LC_GROUND_DARK,
    LC_LINE,        /* the ground's surface line */
    LC_SEP,         /* lines between ground tiles */
    LC_HUD,         /* backing of the progress bar row */
    LC_HUD_DIM,     /* empty part of the bar */
    LC_COUNT
};

/* a level's colours, 5-bit red, green, blue and a 0 (palette.c steps
 * through them 4 bytes at a time) */
typedef struct {
    uint8_t c[LC_COUNT][4];
} GbLevelPal;

/* Colours that don't change with the level's palette. */
enum {
    FC_YELLOW = 0, /* orbs, pads, portals */
    FC_PINK,
    FC_BLUE,
    FC_GREEN,
    FC_WHITE,
    FC_ORANGE,
    FC_CYAN,
    FC_GOLD,       /* coins */
    FC_DARK_GOLD,
    FC_BAR,        /* the progress bar */
    FC_MENU,       /* menu background */
    FC_MENU_GOLD,
    FC_BLACK,
    FC_COUNT
};

#endif
