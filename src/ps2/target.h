/*
 * The PS2 as the portable core sees it (common.h includes this): a 640x448
 * virtual screen, drawn 1:1 by the GS, and the names of its buttons in help
 * texts. Every platform of the core has a target.h like this one in its
 * source folder, on the include path of its build.
 */
#ifndef PD_TARGET_H
#define PD_TARGET_H

#define SCREEN_W 640
#define TARGET_NAME "PLAYSTATION 2"
#define BTN_NAME_L "L1"
#define BTN_NAME_R "R1"
/* the GS draws the virtual screen 1:1 in NTSC; in PAL it is stretched to
 * 512 lines, and gfx_gskit.c sets the grid down the screen to that */
#define PIXEL_GRID 1.0f
/* the options' FLICKER FILTER row, in the PS2 build alone (the PC build
 * that plays this layout has no interlaced picture to filter) */
#ifdef PS2
#define FLICKER_OPTION 1
#endif

#endif
