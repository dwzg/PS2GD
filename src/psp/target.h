/*
 * The PSP as the portable core sees it (common.h includes this). Its
 * 480x272 screen is wider than the PS2's, so the virtual screen is too
 * (levels show more of what is ahead, menus laid out for 640 stay centred
 * through UI_X), the screen is scaled down to the LCD, and help texts name
 * the PSP's buttons. PD_PSP is defined too, for what else differs.
 */
#ifndef PD_TARGET_H
#define PD_TARGET_H

#define SCREEN_W 790 /* 448 * 480 / 272, rounded down */
#define TARGET_NAME "PSP"
#define BTN_NAME_L "L"
#define BTN_NAME_R "R"
/* device pixels per virtual pixel, across and down: the 790 virtual pixels
 * across come to 479.6 of the LCD's 480 */
#define PIXEL_GRID (272.0f / SCREEN_H)
/* the options' OUTPUT: the synth's mix for headphones, or for the PSP's
 * own small speakers (audio.c: the bass cut, compressed) */
#define AUDIO_OUTPUT_OPTION 1
/* and AUTO: headphones plugged in or not (main_psp.c, sceHprm) */
#define AUDIO_OUTPUT_AUTO 1

#endif
