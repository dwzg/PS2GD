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

/* the options screen's rows (Game.options_sel) */
/* (AUDIO_OUTPUT_OPTION, a target's: OUTPUT, the sound mixed for headphones
 * or for the speakers, save.speaker; the GBA's and the PSP's. FLICKER_OPTION:
 * FLICKER FILTER, save.flicker; the PS2's) */
enum {
    OPT_MUSIC = 0,
    OPT_SFX,
#if AUDIO_OUTPUT_OPTION
    OPT_OUTPUT,
#endif
    OPT_DELAY,
#if FLICKER_OPTION
    OPT_FLICKER,
#endif
    OPT_ERASE,
    OPT_BACK,
    OPT_COUNT
};

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
    int garage_row;
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
/* game_suspend: pause the run, or (starting) the one being faded into on
 * its first tick */
void play_suspend(int starting);
unsigned play_attempts_started(void);

/* gameplay pieces reused by the title screen's demo run */
void play_begin_tick(PlayState *ps); /* before moving the player: keep the previous state for interpolation */
void play_end_tick(PlayState *ps);   /* after: effects, rotation, camera */
void play_place(PlayState *ps);      /* the player was put somewhere new: reset camera and effects */
void play_view(const PlayState *ps, View *v);
/* The camera to draw with: between the latest two ticks (Game.alpha), shaken
 * after a death */
void play_camera(const PlayState *ps, float *x, float *y);
/* Past the finish (PH_COMPLETE): the x the camera glides to a stop at */
float play_camera_stop(const PlayState *ps);
void play_draw_player(const PlayState *ps, const View *v);

/* the title screen's demo run (demo.c): a loop of DEMO_LOOP columns, 8 bars
 * of the menu song at normal speed; at x = DEMO_WRAP the run moves back by
 * DEMO_LOOP, onto the same view */
#define DEMO_LOOP 180
/* the loop's first columns, repeated after it: enough to fill the screen
 * until the run wraps, with the level's end gate (and its glow) beyond the
 * right edge (34 = BLOCK_PX) */
#define DEMO_TAIL (24 + (SCREEN_W - 640 + 33) / 34)
#define DEMO_WRAP (DEMO_LOOP + 8.0f)
/* the menu covers the top of the screen: the run is shown lower */
#define DEMO_CAM_DROP 1.2f
const char *const *demo_level_src(void);
/* Snapshots of the run's first time round, every DEMO_SNAP_EVERY blocks
 * (target.h; 15 if not set), so that following the music to a new place
 * replays a few blocks rather than the loop up to there: demo_tick makes
 * them by playing the run ahead over the title's first seconds. A tool
 * may make them instead (gba_tool, the GBA's, at build time: the same
 * run): demo_record plays it from the start into snap[0..n), one every
 * `every` blocks; demo_take_snapshots takes such a set as the run's own
 * (n of them, every DEMO_SNAP_EVERY blocks), and the title then plays
 * nothing ahead. */
typedef struct {
    Player p;
    float last_x;
    int held, valid;
} DemoSnap;
void demo_record(DemoSnap *snap, int n, int every);
void demo_take_snapshots(const DemoSnap *snap, int n);
/* the run's presses (for gbc_tool): button held while x is in [x0, x1),
 * or when a tick steps over the whole range */
int demo_press_count(void);
void demo_press(int i, float *x0, float *x1);
/* beat: the menu song's beat as heard, or NULL if unknown; returns 1 if the run died */
int demo_tick(PlayState *ps, const float *beat);
void demo_render(const PlayState *ps, const Palette *pal);
void demo_free(PlayState *ps);

/* for drawing, used by every presentation (game.c): the beat, 0..1, and
 * the menus' colours (the title goes through the level palettes, 6 s each) */
float beat_pulse(void);
void menu_palette(Palette *out);
/* the vector family's menu backdrop and the title's buttons (game_draw.c):
 * kind 0 garage (the player's cube), 1 play, 2 options, -1 the frame alone */
void draw_menu_backdrop(const Palette *pal, float scroll, float pulse);
void draw_button(float cx, float cy, float size, int selected, Color c, int kind);
/* the level select's face for a difficulty (game_draw.c) */
void draw_diff_badge(float cx, float cy, int d, float r);

#endif
