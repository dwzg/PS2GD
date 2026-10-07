/*
 * The Nintendo DS as the portable core sees it (common.h includes this).
 * The game is drawn by the DS's 3D engine on its top screen, 256x192:
 * the virtual screen keeps the PS2's 448 lines and is as wide as the
 * screen's shape asks (448 * 256 / 192, rounded down), so it is scaled the
 * same both ways, 192/448, and menus laid out for 640 stay centred through
 * UI_X (they keep within 597). PD_NDS is defined too, for what else
 * differs.
 */
#ifndef PD_TARGET_H
#define PD_TARGET_H

#define PD_NDS 1
#define SCREEN_W 597
#define TARGET_NAME "DS"
#define BTN_NAME_L "L"
#define BTN_NAME_R "R"
/* device pixels per virtual pixel: 192 of the screen's lines for 448 */
#define PIXEL_GRID (192.0f / SCREEN_H)
/* the help texts name the DS's buttons: font.c draws A, B, Y and X where
 * the PlayStation's face buttons would be (the DS's A, B, Y and X are read
 * as the core's CROSS, CIRCLE, SQUARE and TRIANGLE: pad in main_nds.c) */
#define FACE_BUTTON_LETTERS 1
/* the options' OUTPUT: the songs as the synth mixes them for headphones,
 * or its mix for the DS's own small speakers, both recorded (nds_tool) */
#define AUDIO_OUTPUT_OPTION 1
/* fewer particles than the PC's 700 (fx.c): each is a call of the
 * software floating point and a polygon of the 2048 the 3D engine draws
 * a frame */
#define FX_MAX_PARTICLES 160
/* circles cut into as many segments as the screen's pixels show (draw.c):
 * by their radius in device pixels */
#define CURVE_DETAIL PIXEL_GRID

#endif
