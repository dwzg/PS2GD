/*
 * The GBA's display, as the frame being prepared: the registers, sprites,
 * palettes and per-scanline colours are built in RAM during the frame and
 * copied to the hardware in the next vertical blank (video_commit, from the
 * interrupt), so a picture never shows half made.
 *
 * Layout (mode 0, four tiled backgrounds, 4-bit tiles):
 *   BG0  text and menus, 512 pixels wide (ui.c)                 priority 0
 *   BG1  the level's blocks and spikes, 12 px to a block       priority 1
 *   BG2  the ground, and the corridor's floor and ceiling      priority 2
 *   BG3  the faint squares behind, scrolled slower              priority 3
 *   OBJ  the player, orbs, pads, portals, coins, saws, effects
 *   backdrop: the background's vertical gradient, one colour a scanline
 *
 * VRAM: char block 0 and the first half of 1 hold the level's 768 tile
 * slots (world.c); BG0, BG2 and BG3 share the tiles from there to the end
 * of char block 2 (indices 256..767 counted from char block 1); the maps
 * are in char block 3 (BG0's and BG3's two screen blocks wide).
 */
#ifndef PD_GBA_VIDEO_H
#define PD_GBA_VIDEO_H

#include <stdint.h>
#include "gba.h"
#include "art.h"

#define SCR_W 240
#define SCR_H 160

/* char blocks and screen blocks */
#define CB_LEVEL 0
#define CB_SHARED 1
#define SB_TEXT 24
#define SB_LEVEL 26
#define SB_GROUND 27
#define SB_SQUARES 28
/* shared tile indices (from char block 1) */
#define SHARED_TILE_FIRST 256
#define SHARED_TILE_END 768

/* BG palette banks */
#define PAL_WORLD 0  /* backdrop, blocks, spikes, squares, corridor bands */
#define PAL_GROUND 1 /* the ground below y = 0 */
/* 2..15: text and menus (ui.h) */


typedef struct {
    uint16_t dispcnt;
    uint16_t bgcnt[4];
    uint16_t hofs[4], vofs[4];
    uint16_t bldcnt, bldalpha, bldy;
    uint16_t win0h, win0v, win1h, win1v, winin, winout;
} VideoRegs;

/* A sprite's attributes; the fourth halfword of each group of four holds
 * the affine matrices (32 of them, pa pb pc pd in sprites 4n..4n+3). */
typedef struct {
    uint16_t attr0, attr1, attr2;
    int16_t aff;
} ObjAttr;

extern VideoRegs g_vid;
extern ObjAttr g_oam[128];
extern uint16_t g_pal_bg[256], g_pal_obj[256];
/* the prepared frame's per-scanline colours: line y's WC_* 0..HDMA_COLORS-1 */
extern uint16_t (*g_hdma)[HDMA_COLORS];

void video_init_hw(void);
/* Which of the two per-scanline tables g_hdma is (0 or 1): each keeps what
 * was put in it two frames before. */
int video_hdma_index(void);
/* In the vertical blank: show the prepared frame. */
void video_commit(void);
/* In a vertical blank with no new frame to show (the frame ran long): the
 * per-scanline colours again from the shown frame's first line. */
void video_hdma_restart(void);

/* Sprites: video_obj_begin() at the start of the frame's drawing, then add
 * them front to back; video_obj_end() hides the rest. */
/* The screen faded k/16 of the way to black (0: not at all), and a run's
 * flash, flash/256 of white added to the world (0..256: the PC's added
 * white): its palettes as shown, made from g_pal_bg and g_pal_obj, which
 * stay as they are (the per-scanline colours are made so where they are
 * made: WorldView.fade and .flash). Called once the frame's colours are
 * set. The flash is on the world's background colours and the sprites'
 * own, not on those of the sprites that add their light (the glows, the
 * particles, the finish line: what is behind them is flashed already)
 * nor on the text's. */
void video_fade(int k, int flash);
void video_obj_begin(void);
/* Returns the sprite's index, or -1 when all 128 are used. */
int video_obj(uint16_t attr0, uint16_t attr1, uint16_t attr2);
void video_obj_end(void);
/* An affine matrix for this frame (0..31), or -1: rotation by angle (a
 * full turn is 65536, clockwise on screen), scaled by sx, sy (8.8 fixed
 * point, 256 = 1; the picture grows as they grow). */
int video_aff(uint16_t angle, int32_t sx, int32_t sy);
/* sprites used / affine matrices used in the frame (for perf logs) */
int video_obj_count(void);

/* Copy to VRAM in the next vertical blank (text and menu tiles, which may
 * be on screen): words are 32-bit; returns 0 if the queue is full. */
int video_queue(volatile void *dst, const void *src, uint32_t words);

/* sin/cos for an angle (a full turn = 65536) in 1.14 fixed point */
int32_t video_sin(uint16_t angle);
static inline int32_t video_cos(uint16_t angle) { return video_sin((uint16_t)(angle + 16384)); }
/* sinf and cosf for animations, from the same table (1024 steps a turn):
 * the same cost for any angle, where sinf's reduction of a big one (the
 * game's clock times a speed, after a while) takes thousands of cycles
 * in software */
extern const int16_t g_sin_tab[];
static inline float tsinf(float rad)
{
    return (float)g_sin_tab[(int)(rad * (1024.0f / 6.2831853f)) & 1023] * (1.0f / 16384.0f);
}
static inline float tcosf(float rad)
{
    return (float)g_sin_tab[((int)(rad * (1024.0f / 6.2831853f)) + 256) & 1023] * (1.0f / 16384.0f);
}

#endif
