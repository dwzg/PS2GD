/* State shared between the menu screens and the gameplay screen. */
#ifndef PD_GAME_INTERNAL_H
#define PD_GAME_INTERNAL_H

#include "game.h"
#include "level.h"
#include "sim.h"
#include "theme.h"
#include "save.h"
#include "render.h"

enum Screen { SCR_TITLE = 0, SCR_SELECT, SCR_PLAY, SCR_GARAGE, SCR_OPTIONS };

enum PlayPhase { PH_RUN = 0, PH_DEAD, PH_COMPLETE };

#define MAX_CHECKPOINTS 64
#define TRAIL_LEN 28

typedef struct {
    Player p;
    float cam_y, cam_target_y;
    float corr_floor, corr_ceil, corr_alpha;
    int pal_from, pal_to, trig_idx;
    float pal_t;
} PlaySnapshot;

typedef struct {
    Level *L;
    int level_idx;
    LevelInfo info;
    Player p;
    int practice;
    int phase;
    float phase_t;
    int attempt;
    float attempt_time;

    float cam_x, cam_y, cam_target_y;
    float corr_floor, corr_ceil, corr_alpha;

    /* visuals */
    float rot;
    float vis_angle;
    float trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
    int trail_n;
    float shake;
    float flash;
    int ground_fx_tick;

    /* palette transitions */
    Palette pal;
    int pal_from, pal_to, trig_idx;
    float pal_t;

    /* practice checkpoints */
    PlaySnapshot cp[MAX_CHECKPOINTS];
    int ncp;

    /* pause menu */
    int paused, pause_sel;

    /* popups */
    float best_popup_t;
    int best_popup_val;
    int new_best;
    int coins_gained;
    int results_sel;
    int jumps_session;
    float time_session;
} PlayState;

typedef struct {
    int screen, next_screen;
    float fade;  /* 0 = fully visible, 1 = black */
    int fading;  /* +1 fading out, -1 fading in */
    uint32_t held, pressed, prev;
    float rep_t[4]; /* autorepeat timers for up/down/left/right */
    uint32_t repeat; /* pressed + autorepeat for directions */
    float t;
    SaveData save;
    int save_dirty;

    int menu_sel;
    int sel_level;
    float sel_scroll;
    int garage_row, garage_mode;
    int options_sel;
    int erase_confirm;
    float erase_t;
    int start_practice;

    /* title animation */
    float title_x, title_y, title_vy, title_rot;
    int title_pal;
    float title_pal_t;

    PlayState play;
} Game;

extern Game g_game;

/* screens */
void screen_go(int scr);
void menus_tick(void);
void menus_render(void);
void play_start(int level_idx, int practice);
void play_tick(void);
void play_render(void);
void play_exit(void);

/* shared drawing used by menus */
void draw_menu_backdrop(const Palette *pal, float scroll, float pulse);
float beat_pulse(void);

#endif
