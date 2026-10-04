/*
 * Playing a level.
 *
 * The screen: row 0 is the progress bar, level rows 14..0 are background
 * rows 1..15 (a block is a tile), rows 16-17 the ground. The background map
 * is 32 tiles wide and scrolls; a new column of the level is drawn into it
 * just before it comes into view. The camera keeps the player 45 pixels in
 * from the left (5.65 blocks, as on the other versions) and never moves up
 * or down: a whole level is 10 rows high.
 *
 * This file is the frame and what reads the level's ROM bank (mapped while
 * a level plays): the physics' cells and the columns streamed in. The
 * sprites, effects, progress bar, palettes, deaths and finishes are in
 * play_fx.c, in a bank of their own (play.h has what both share).
 */
#include <string.h>

#include "gbc.h"
#include "play.h"

/* for the emulator test: the phase, and frames that took longer than a
 * frame while running */
uint8_t g_phase;
uint16_t g_dropped;
GsPlayer g_snap; /* the player after the last tick (frames end mid-tick) */
static uint8_t s_vbl_start;
extern volatile uint8_t g_vbl_count;

const GbLevel *L;
uint8_t s_practice;
static uint8_t s_quit;
static uint8_t s_fw; /* the fireworks past the finish */
uint8_t s_t;
uint16_t s_attempts, s_jumps_total;
uint32_t s_ticks_total;
int16_t s_cam;
static int16_t s_col;
uint8_t s_trig, s_pal_from, s_pal_to, s_pal_t;
uint8_t s_flash, s_fade, s_pal_dirty, s_beat, s_beat_down;
uint16_t s_rot;
int8_t s_ship_f;
Snap s_cp[MAX_CP];
uint8_t s_ncp;
static uint8_t s_paused;
/* The button that started or resumed the level may still be down: it
 * doesn't jump until it has been let go. */
static uint8_t s_jump_lock;
uint8_t s_new_best;
static Snap s_start;

/* The title's demo run (play_title): the level loops, the button follows
 * the other versions' press table (gbc_demo_press, keyed by x), and the
 * map column of level column c is (c + s_map_off) & 31, so that moving the
 * run back by a loop leaves the picture where it is. */
uint8_t s_demo;
static uint8_t s_map_off, s_demo_held, s_sel;
static uint32_t s_demo_last;
uint16_t s_demo_t;
/* for the emulator test: loops played, and times the run died */
uint16_t g_demo_loops, g_demo_deaths;

/* a ball turns a quarter in 0.45 blocks: per tick at each speed (1/256 of a quarter) */
static const uint8_t BALL_SPIN[4] = {50, 63, 79, 95};

/* --- the background --- */

static void build_column(int16_t c, uint8_t *t, uint8_t *a)
{
    static const uint8_t COIN_BIT[4] = {1, 2, 4, 8};
    uint8_t r = COL_ROWS, tile;
    /* filled from the bottom (level row 0) up */
    t += COL_ROWS;
    a += COL_ROWS;
    if (s_demo && c >= (int16_t)L->width) c -= GBC_DEMO_LOOP;
    if (c >= 0 && (uint16_t)c < L->width) {
        const uint8_t *cells = L->cells + ((uint16_t)c << 4);
        do {
            tile = *cells++;
            if ((uint8_t)(tile - T_COIN) < 4 && (gs_p.coins & COIN_BIT[tile - T_COIN])) tile = T_EMPTY;
            *--t = tile;
            *--a = gfx_bg_attr[tile];
        } while (--r);
    } else {
        do {
            *--t = T_EMPTY;
            *--a = gfx_bg_attr[T_EMPTY];
        } while (--r);
    }
    /* the finish line, over the sky around the level's end */
    if (!s_demo) {
        int16_t k = c - (int16_t)L->width + 3;
        if (k >= 0 && k < T_FINISH_COLS) {
            uint8_t ft = T_FINISH + (uint8_t)k, fa = gfx_bg_attr[ft];
            r = COL_ROWS;
            do {
                if (*t == T_EMPTY) {
                    *t = ft;
                    *a = fa;
                }
                t++;
                a++;
            } while (--r);
        }
    }
}

/* Draw the columns coming into view, at most max of them. */
static void stream(uint8_t max)
{
    uint8_t t[COL_ROWS], a[COL_ROWS];
    /* (a column ahead of the screen's last: drawing can wait a frame) */
    int16_t want = col_of(s_cam) + 22;
    while (s_col <= want && max && col_queue_free()) {
        build_column(s_col, t, a);
        col_queue((uint8_t)((s_col + s_map_off) & 31), t, a);
        s_col++;
        max--;
    }
}



/* --- palettes --- */


/* --- sprites --- */



/* the cube spins in the air and settles on a side when it lands */
static void spin(void)
{
    int8_t g = gs_p.grav;
    if (gs_p.mode == MODE_CUBE) {
        if (gs_p.grounded) {
            uint8_t r = (uint8_t)s_rot;
            if (r) {
                if (r < 128) s_rot -= r < 49 ? r : 49;
                else s_rot += (uint8_t)(256 - r) < 49 ? (uint8_t)(256 - r) : 49;
            }
        } else {
            s_rot += g > 0 ? 20 : -20;
        }
    } else if (gs_p.mode == MODE_BALL) {
        s_rot += g > 0 ? BALL_SPIN[gs_p.speed_idx] : -BALL_SPIN[gs_p.speed_idx];
    }
}






/* --- attempts --- */

static void take_snap(Snap *s)
{
    s->p = gs_p;
    s->trig = s_trig;
    s->pal_from = s_pal_from;
    s->pal_to = s_pal_to;
    s->pal_t = s_pal_t;
    s->rot = s_rot;
}

/* Start an attempt (from a checkpoint in practice): the screen is dark,
 * the columns are drawn over the next few frames. */
static void restore(const Snap *s)
{
    gs_p = s->p;
    g_snap = gs_p;
    s_trig = s->trig;
    s_pal_from = s->pal_from;
    s_pal_to = s->pal_to;
    s_pal_t = s->pal_t;
    s_rot = s->rot;
    s_ship_f = 3;
    s_cam = px_of(gs_p.x) - PLAYER_SX;
    s_col = col_of(s_cam);
    g_scx = (uint8_t)(s_cam + (s_map_off << 3));
    s_attempts++;
    if (!s_demo) progress_attempt(&g_save.progress, L->id, s_practice);
    s_demo_last = 0;
    s_demo_held = 0;
    fx_restart();
}




/* --- the frame --- */

/* The title's run: is the button down at x (before the tick)? A range
 * stepped over since the last tick counts too. */
static uint8_t demo_button(uint32_t x)
{
    uint8_t i;
    for (i = 0; i < GBC_DEMO_PRESSES; i++) {
        uint32_t x0 = gbc_demo_press[i][0], x1 = gbc_demo_press[i][1];
        if ((x >= x0 && x < x1) || (s_demo_last < x0 && x >= x1)) return 1;
    }
    return 0;
}

/* ... past the loop's end it goes back a loop, which looks the same */
static void demo_loop(void)
{
    uint8_t i;
    gs_p.x -= (uint32_t)GBC_DEMO_LOOP << 16;
    s_demo_last -= (uint32_t)GBC_DEMO_LOOP << 16;
    for (i = 0; i < GS_USED_N; i++) gs_p.used[i] = 0; /* orbs and pads work again */
    s_cam -= GBC_DEMO_LOOP * 8;
    s_col -= GBC_DEMO_LOOP;
    fx_shift(-GBC_DEMO_LOOP * 8);
    s_map_off = (uint8_t)((s_map_off + GBC_DEMO_LOOP) & 31);
    g_demo_loops++;
}

static void run_tick(void)
{
    uint8_t held = (g_keys & (J_A | J_UP)) != 0, pressed = (g_pressed & (J_A | J_UP)) != 0;
    uint16_t ev;
    if (s_demo) {
        held = demo_button(gs_p.x);
        pressed = held && !s_demo_held;
        s_demo_held = held;
        s_demo_last = gs_p.x;
    } else if (s_jump_lock) {
        /* (from the pad itself: the emulator test gives the game its
         * presses another way) */
        if (joypad() & (J_A | J_UP)) held = pressed = 0;
        else s_jump_lock = 0;
    }
    if (s_practice) {
        if ((g_pressed & J_B) && !gs_p.dead) {
            if (s_ncp == MAX_CP) {
                memmove(&s_cp[0], &s_cp[1], sizeof(Snap) * (MAX_CP - 1));
                s_ncp--;
            }
            take_snap(&s_cp[s_ncp++]);
            sfx_play(SFX_CHECKPOINT);
        }
        if ((g_pressed & J_SELECT) && s_ncp) {
            s_ncp--;
            sfx_play(SFX_BACK);
        }
    }
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        gs_step(held, pressed);
        PERF_END(PERF_SIM, t);
    }
    g_snap = gs_p;
    ev = gs_p.events;
    spin();
    if (ev & (EV_ORB | EV_PAD)) {
        /* a ring around the object's cell centre */
        fx_ring((int16_t)(gs_p.ev_cell >> 4) * 8 + 4, (uint8_t)(GROUND_SY - (gs_p.ev_cell & 15) * 8 - 4));
    }
    if (ev & EV_COIN) {
        int16_t c = (int16_t)(gs_p.ev_cell >> 4);
        cell_queue((uint8_t)((c + s_map_off) & 31), (uint8_t)(15 - (gs_p.ev_cell & 15)), T_EMPTY, gfx_bg_attr[T_EMPTY]);
        sfx_play(SFX_COIN);
    }
    if (s_demo) {
        if (gs_p.x >= (uint32_t)GBC_DEMO_WRAP << 16) demo_loop();
        if (gs_p.dead) {
            /* (the press table is checked not to die: pd_tool demo) */
            g_demo_deaths++;
            g_phase = PH_DEAD;
            s_t = 0;
        }
    } else if (gs_p.dead) {
        on_death();
    } else if (gs_p.done) {
        on_complete();
    }
}

/* Scanlines since this frame's vertical blank began (255: the next one
 * has begun already). */
static uint8_t frame_lines(void)
{
    uint8_t ly = LY_REG;
    if (g_vbl_count != s_vbl_start) return 255;
    return ly >= 144 ? (uint8_t)(ly - 144) : (uint8_t)(ly + 10);
}

static void play_frame(void)
{
    if (s_demo) {
        /* the title's menu: PLAY or GARAGE */
        if (g_pressed & (J_A | J_START)) {
            sfx_play(SFX_SELECT);
            s_quit = 1;
            return;
        }
        if (g_pressed & (J_LEFT | J_RIGHT | J_SELECT)) {
            s_sel ^= 1;
            ui_title_menu(s_sel);
            sfx_play(SFX_MOVE);
        }
    }
    if (music_beat) {
        /* for palettes(), which may run a frame later */
        s_beat = 1;
        s_beat_down = music_bar_beat == 0;
    }
    if (s_paused) {
        if (g_pressed & (J_A | J_SELECT | J_B)) {
            s_paused = 0;
            HIDE_WIN;
            SHOW_SPRITES;
        }
        if (g_pressed & J_A) {
            music_pause(0);
            s_jump_lock = 1;
        } else if (g_pressed & J_SELECT) {
            if (g_phase == PH_RUN && gs_p.ticks > PROGRESS_LEFT_TICKS) {
                gs_p.dead = 1;
                on_death();
            }
            s_ncp = 0;
            s_new_best = 0;
            s_attempts = 0;
            s_ticks_total = 0;
            s_jumps_total = 0;
            g_phase = PH_RESPAWN;
            s_t = 0;
            music_pause(0);
        } else if (g_pressed & J_B) {
            if (g_phase == PH_RUN && gs_p.ticks > PROGRESS_LEFT_TICKS) {
                gs_p.dead = 1;
                on_death();
            }
            s_quit = 1;
        }
        return;
    }
    if ((g_pressed & J_START) && g_phase == PH_RUN && !s_demo) {
        s_paused = 1;
        music_pause(1);
        sfx_play(SFX_SELECT);
        HIDE_SPRITES; /* they would cover the menu */
        ui_pause(s_practice);
        return;
    }

    switch (g_phase) {
    case PH_RUN:
        run_tick();
        break;
    case PH_DEAD:
        s_t++;
        if (s_t >= (s_practice ? 24 : 45)) {
            g_phase = PH_RESPAWN;
            s_t = 0;
        }
        break;
    case PH_RESPAWN:
        /* fade out (8 frames), draw the start (6), fade in (8) */
        s_t++;
        if (s_t <= 8) {
            s_fade = (uint8_t)(8 - s_t);
            s_pal_dirty = 1;
            if (s_t == 8) {
                restore(s_practice && s_ncp ? &s_cp[s_ncp - 1] : &s_start);
            }
        } else if (s_t == 15) {
            /* the attempt is written at the start of the level, a new
             * best above it */
            if (!(s_practice && s_ncp) && !s_demo) {
                ui_attempt(s_attempts);
                if (s_new_best) ui_new_best(s_new_best, 2);
            }
        } else if (s_t > 15) {
            s_fade = (uint8_t)(s_t - 15);
            s_pal_dirty = 1;
            if (s_fade >= 8) {
                s_fade = 8;
                g_phase = PH_RUN;
                if (s_demo) music_play(SONG_MENU_GB);
                else if (!s_practice) music_play(SONG_FIRST_LEVEL_GB + L->song);
            }
        }
        break;
    case PH_COMPLETE: {
        int16_t stop = (int16_t)(L->width * 8) - 100;
        /* s_t stops at 255 (the results are drawn once, at 70, and A
         * works from then on); the fireworks go round every 256 frames */
        if (s_t < 255) s_fw = ++s_t;
        else s_fw++;
        finish_exit();
        gs_p.grounded = 1; /* (a cube settles on a side) */
        spin();
        if (s_cam < stop) {
            int16_t d = (stop - s_cam) >> 3;
            s_cam += d > 2 ? 2 : (d < 1 ? 1 : d);
        }
        if (s_t == 70)
            ui_results(s_practice, s_attempts, s_jumps_total, (uint16_t)(s_ticks_total / 60),
                       s_practice ? 0 : L->ncoins, gs_p.coins);
        if (s_t > 70 && (g_pressed & (J_A | J_START))) s_quit = 1;
        if ((s_fw & 15) == 0 && s_fw < 64) burst((int16_t)(40 + (s_fw << 1)), (int16_t)(30 + (s_fw & 31)), 24);
        break;
    }
    }

    if (g_phase == PH_RUN) s_cam = px_of(gs_p.x) - PLAYER_SX;
    g_scx = (uint8_t)(s_cam + (s_map_off << 3));
    if ((g_frame & 3) == 0) saw_frame((uint8_t)((g_frame >> 2) & 3));
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        draw_player();
        draw_trail();
        draw_effects();
        if (s_practice) draw_checkpoints();
        PERF_END(PERF_SPRITES, t);
    }
    /* Then what can wait a frame if this one is short of time (a frame
     * that runs late slows the game and its music down): the columns
     * coming into view, the progress bar and the palettes, each only if
     * its longest run (about 14, 20 and 31 scanlines) still fits. */
    {
#ifdef PD_PERF
        uint16_t t;
#endif
        PERF_BEGIN(t);
        if (g_phase == PH_RESPAWN) stream(4);
        else if (frame_lines() < 154 - 20) stream(2);
        PERF_END(PERF_STREAM, t);
        PERF_BEGIN(t);
        if ((g_phase == PH_RUN || g_phase == PH_COMPLETE) && !s_demo && frame_lines() < 154 - 26) hud_update();
        PERF_END(PERF_HUD, t);
        PERF_BEGIN(t);
        if (frame_lines() < 154 - 37) palettes();
        PERF_END(PERF_PAL, t);
    }
}

/* Set up level lv to play with the display off, its first screen drawn. */
static void level_begin(const GbLevel *lv)
{
    L = lv;
    SWITCH_ROM_MBC5(L->bank);
    gs_cells = L->cells;
    gs_width = L->width;
    gs_height = L->height;
    gs_ring_reset();

    video_blank();
    hide_all_sprites();
    HIDE_WIN;
    s_quit = 0;
    s_paused = 0;
    s_ncp = 0;
    s_attempts = 0;
    s_ticks_total = 0;
    s_jumps_total = 0;
    s_new_best = 0;
    s_map_off = 0;

    gs_reset(L->speed);
    s_trig = 0;
    s_pal_from = s_pal_to = L->pal;
    s_pal_t = 48;
    s_rot = 0;
    take_snap(&s_start);
    restore(&s_start);
    {
        uint8_t t[COL_ROWS], a[COL_ROWS], i;
        for (i = 0; i < 23; i++) {
            build_column(s_col, t, a);
            col_queue((uint8_t)((s_col + s_map_off) & 31), t, a);
            s_col++;
            if (!col_queue_free()) video_flush();
        }
        video_flush();
    }
    ui_ground(32);
    s_fade = 8;
    s_flash = 0;
    pal_level(s_pal_from, s_pal_to, 255, 0, 8);
    g_scx = (uint8_t)s_cam;
}

/* Play frames until s_quit. */
static void level_loop(void)
{
    g_phase = PH_RUN;
    g_dropped = 0;
    s_vbl_start = g_vbl_count;
    while (!s_quit) {
#ifdef PD_PERF
        uint16_t t, tm;
#endif
        frame_wait();
        /* the last frame's work ran past the next vblank */
        if ((uint8_t)(g_vbl_count - s_vbl_start) > 1 && g_phase == PH_RUN && !s_paused) g_dropped++;
        s_vbl_start = g_vbl_count;
        PERF_BEGIN(t);
        /* the music first: it must not wait */
        PERF_BEGIN(tm);
        music_tick();
        PERF_END(PERF_MUSIC, tm);
        play_frame();
        PERF_END(PERF_FRAME, t);
    }
}

void play_level(uint8_t level, uint8_t practice)
{
    g_screen = SCR_PLAY;
    s_practice = practice;
    level_begin(&gbc_levels[level]);
    fill_win(0, 0, 20, 10, UT_BLANK, PAL_TEXT | 0x08); /* (the title was there) */
    ui_hud_init();
    hud_thresholds();
    ui_attempt(s_attempts);
    g_hud_split = 1;
    video_on();
    music_play(practice ? SONG_PRACTICE_GB : SONG_FIRST_LEVEL_GB + L->song);
    s_jump_lock = 1;
    level_loop();
    music_stop();
    video_blank();
    save_write(); /* the attempts of runs left early */
    HIDE_WIN;
    hide_all_sprites();
    g_hud_split = 0;
    g_scx = 0;
}

uint8_t play_title(void)
{
    g_screen = SCR_TITLE;
    s_practice = 0;
    s_demo = 1;
    s_demo_t = 0;
    s_sel = 0;
    level_begin(&gbc_demo_level);
    ui_title(s_sel);
    LYC_REG = 79;
    g_title_split = 1;
    video_on();
    music_play(SONG_MENU_GB);
    s_jump_lock = 0;
    level_loop();
    video_blank();
    hide_all_sprites();
    g_title_split = 0;
    LYC_REG = 7;
    LCDC_REG &= ~LCDCF_BG9C00;
    s_demo = 0;
    s_map_off = 0;
    g_scx = 0;
    return s_sel;
}
