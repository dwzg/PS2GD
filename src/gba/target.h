/*
 * Game Boy Advance target (see src/core/common.h). The GBA draws the game
 * with its own tiles and sprites, 12 pixels to a block: its 240x160
 * screen shows 20 blocks across and 13.3 up, about what the PC's 640x448
 * shows (18.8 and 13.2). The shared game logic places the camera for a
 * virtual screen of the same 20 blocks, 680 = 20 * 34 wide; the vector
 * renderer that would draw into it is not part of this build.
 */
#ifndef PD_TARGET_H
#define PD_TARGET_H

#define PD_GBA 1
#define SCREEN_W 680
#define TARGET_NAME "GBA"
#define BTN_NAME_L "L"
#define BTN_NAME_R "R"
/* unused: no vector drawing on the GBA */
#define PIXEL_GRID 0.0f
/* fewer particles than the PC's 700 (fx.c): the GBA shows 96 at most (with
 * the fireworks' other sprites, 109 of its 128); when they are all in use
 * the oldest go first */
#define FX_MAX_PARTICLES 96
/* and the GBA has fx.h's particles of its own, in fixed point (fx_gba.c):
 * fx.c is left out */
#define FX_TARGET 1
/* the levels, and the title's run's, parsed at build time (level.c's hook:
 * gba_tool, src/host/gba_levels.c; src/gba/levels_gba.c finds them) */
#define LEVEL_PREBUILT 1
/* the title's run's snapshots, where to pick it up when the music jumps
 * (demo.c): every block rather than 15, so that picking it up replays 6
 * ticks at most rather than 90; gba_tool makes them (levels_gba.c), so the
 * title plays nothing ahead */
#define DEMO_SNAP_EVERY 1
/* the options' OUTPUT: the songs mixed for the headphones (the other
 * platforms' sound) or for the GBA's speaker (audio_gba.c, gba_audio.c) */
#define AUDIO_OUTPUT_OPTION 1

#endif
