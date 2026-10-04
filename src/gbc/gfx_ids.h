/*
 * Where the Game Boy Color build keeps its tiles in VRAM, shared by the ROM
 * and gbc_tool, which draws them.
 *
 * Background tiles use the 0x8000 addressing, so a tile number picks one of
 * 256 tiles in either VRAM bank:
 *   bank 0: the level tiles (tileset.h, numbered as level cells), the
 *           ground, and the title logo
 *   bank 1: the font, the progress bar and every sprite (8x16 sprites: a
 *           16x16 frame is four tiles, left column then right column)
 */
#ifndef GB_GFX_IDS_H
#define GB_GFX_IDS_H

/* VRAM bank 0: tiles 0 .. T_LEVEL_COUNT-1 are the level's (tileset.h) */
#define BT_LOGO 128          /* the title logo, BT_LOGO .. 255 */

/* VRAM bank 1 */
#define UT_FONT 0            /* ASCII 32..95 */
#define UT_ARROW_L 64
#define UT_ARROW_R 65
#define UT_STAR 66
#define UT_COIN_NO 67        /* coin not collected yet */
#define UT_COIN_YES 68
#define UT_BAR_L 70          /* progress bar caps */
#define UT_BAR_R 71
#define UT_BAR 72            /* + pixels filled, 0..8 */
#define UT_BLANK 81          /* solid colour 0 */
#define UT_DIAMOND 82        /* menu cursor */
/* white glyphs drawn into the level (over the sky, in colour 3 of the
 * object palettes, which is white): "ATTEMPT 5" */
#define UT_SKYFONT 96
#define SKYFONT_CHARS " 0123456789ATEMPNWBS%!"
/* The garage's choices sit half a tile below the background grid, each
 * drawn as a top tile (its upper half, in the tile's lower 4 rows) and the
 * tile under it (its lower half): */
#define UT_SOLID 118         /* colour swatches, solid colour 1, 2, 3: 3 pairs */

#define ST_CUBE 128          /* 6 frames, 0..75 degrees, of the player's icon (gfx_icons) */
#define ST_SHIP 152          /* 7 frames, nose 30 degrees up .. 30 down */
#define ST_BALL 180          /* 4 frames, 0..67.5 degrees */
#define ST_UFO 196           /* 3 frames, tilted left, level, right */
#define ST_WAVE 208          /* 3 frames: up, level, down */
#define ST_PART 220          /* 3x3 square particle */
#define ST_PART_SM 222       /* 2x2 */
#define ST_DOT 224           /* wave trail dot */
#define ST_CHECK 226         /* practice checkpoint */
#define ST_RING 228          /* 16x16 ring (orb/pad touch), 2 frames */
#define UT_ICON 236          /* the icons, unrotated: ICON_COUNT pairs */
#define ST_BOX 252           /* 8x16 sprite: a box around a tile (garage cursor) */

#define CUBE_FRAMES 6
#define SHIP_FRAMES 7
#define BALL_FRAMES 4
#define UFO_FRAMES 3
#define WAVE_FRAMES 3
#define SAW_FRAMES 4

/* Background palettes */
#define PAL_WORLD 0  /* sky, block fill, block edge, spike */
#define PAL_GROUND 1 /* ground, darker ground, ground line, separator */
#define PAL_YP 2     /* sky, yellow, pink, white */
#define PAL_BG 3     /* sky, blue, green, white */
#define PAL_OC 4     /* sky, orange, cyan, white */
#define PAL_GD 5     /* sky, gold, dark gold, white */
#define PAL_HUD 6    /* backing, white, bar green, grey */
#define PAL_TEXT 7   /* menu background, white, gold, dark */

/* Sprite palettes */
#define OPAL_PLAYER 0 /* -, primary, secondary, black */
#define OPAL_FX 1     /* -, primary, white, secondary */
#define OPAL_CHECK 2  /* -, green, white, dark green */

#endif
