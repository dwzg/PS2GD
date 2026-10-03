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

    /* state at the start of the latest tick, for interpolated rendering */
    float prev_cam_x, prev_cam_y, prev_x, prev_y, prev_rot, prev_angle;

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
    float alpha; /* render interpolation factor, see game_render() */
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

    PlayState play;
    PlayState demo; /* the run behind the title screen */
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
unsigned play_attempts_started(void);

/* gameplay pieces reused by the title screen's demo run */
void play_begin_tick(PlayState *ps); /* before moving the player: keep the previous state for interpolation */
void play_end_tick(PlayState *ps);   /* after: effects, rotation, camera */
void play_place(PlayState *ps);      /* the player was put somewhere new: reset camera and effects */
void play_view(const PlayState *ps, View *v);
void play_draw_player(const PlayState *ps, const View *v);

/* the title screen's demo run (demo.c): a loop of DEMO_LOOP columns, 8 bars
 * of the menu song at normal speed; at x = DEMO_WRAP the run moves back by
 * DEMO_LOOP, onto the same view */
#define DEMO_LOOP 180
#define DEMO_TAIL 24 /* the loop's first columns, repeated after it */
#define DEMO_WRAP (DEMO_LOOP + 8.0f)
const char *const *demo_level_src(void);
/* beat: the menu song's beat as heard, or NULL if unknown; returns 1 if the run died */
int demo_tick(PlayState *ps, const float *beat);
void demo_render(const PlayState *ps, const Palette *pal);
void demo_free(PlayState *ps);

/* shared drawing used by menus */
void draw_menu_backdrop(const Palette *pal, float scroll, float pulse);
float beat_pulse(void);

#endif
