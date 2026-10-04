/*
 * What the two halves of playing a level share: play.c, the frame and the
 * level's ROM bank, and play_fx.c, sprites, effects, the progress bar,
 * palettes, deaths and finishes (in a ROM bank of its own).
 */
#ifndef GBC_PLAY_H
#define GBC_PLAY_H

#include "gbc.h"

#define PLAYER_SX 45 /* the player's x on screen */
#define GROUND_SY 128 /* screen y of the ground's surface */
#define MAX_CP 8
#define TRAIL 10

enum { PH_RUN = 0, PH_DEAD, PH_RESPAWN, PH_COMPLETE };

/* OAM slots */
#define OAM_PLAYER 0
#define OAM_PART 2
#define OAM_TRAIL 10
#define OAM_CHECK 20
#define OAM_RING 24
#define NPART 8
#define NCHECK 4

typedef struct {
    GsPlayer p;
    uint8_t trig, pal_from, pal_to, pal_t;
    uint16_t rot;
} Snap;

/* the phase of the run (PH_*) */
extern uint8_t g_phase;
/* the level being played, and its run */
extern const GbLevel *L;
extern uint8_t s_practice, s_t, s_ncp, s_new_best;
extern uint16_t s_attempts, s_jumps_total;
extern uint32_t s_ticks_total;
extern Snap s_cp[MAX_CP];
/* the camera's x (world pixels), the player's turn and the ship's tilt */
extern int16_t s_cam;
extern uint16_t s_rot;
extern int8_t s_ship_f;
/* palettes: triggers passed, the change under way, the beat's flash, the
 * fade, whether to rebuild them, a beat to flash */
extern uint8_t s_trig, s_pal_from, s_pal_to, s_pal_t;
extern uint8_t s_flash, s_fade, s_pal_dirty, s_beat, s_beat_down;
/* the title's demo run (play_title), and its frames for the palettes */
extern uint8_t s_demo;
extern uint16_t s_demo_t;

/* 16.16 blocks to pixels (8 a block), without a 13-step shift loop */
static int16_t px_of(uint32_t x)
{
    return (int16_t)(((uint16_t)(x >> 16) << 3) | ((uint8_t)(x >> 8) >> 5));
}
static int16_t py_of(int32_t y) { return GROUND_SY - px_of((uint32_t)y); }
static int16_t col_of(int16_t px) { return (int16_t)((px + 256) >> 3) - 32; } /* floor, px >= -256 */

/* play_fx.c */
void hud_thresholds(void) BANKED;
void hud_update(void) BANKED;
void palettes(void) BANKED;
void draw_player(void) BANKED;
void draw_trail(void) BANKED;
void draw_checkpoints(void) BANKED;
void draw_effects(void) BANKED;
void burst(int16_t sx, int16_t sy, uint8_t frames) BANKED;
void hide_all_sprites(void) BANKED;
void on_death(void) BANKED;
void on_complete(void) BANKED;
void finish_exit(void) BANKED;
void fx_ring(int16_t x, uint8_t y) BANKED;
void fx_shift(int16_t dx) BANKED;
void fx_restart(void) BANKED;

#endif
