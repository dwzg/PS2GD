/*
 * The bottom screen: the controls in the menus; the chosen level's records
 * in the level select; in a run, the level, the attempt, its coins, and
 * room to touch (the touch screen jumps: main_nds.c). Drawn by the CPU
 * with the core's own text, panels, coins and icons (soft_nds.c), laid out
 * as the top screen is (the virtual screen, SCREEN_W x 448), and only when
 * what it shows changes: a screen, a level, an attempt, a coin. What
 * doesn't change in a run is drawn once a level, the rest over a copy of
 * it. The drawing is queued and done in the time the frames leave over
 * (soft_nds.c), and shown whole at the next vertical blank.
 */
#include <stdio.h>
#include <string.h>

#include "nds_platform.h"
#include "../core/draw.h"
#include "../core/font.h"
#include "../core/game_internal.h"
#include "../core/icons.h"
#include "../core/version.h"

/* what the screen shows: drawn again when it changes */
typedef struct {
    int screen, level, practice, attempt, run_coins, best, best_practice, saved_coins, checkpoints, paused;
    int icon, col1, col2, ready;
    unsigned total_attempts, total_jumps;
} Shown;

static Shown s_shown;
/* the backdrop (gradient, title), drawn once; and what doesn't change in a
 * run (all of it elsewhere) over it: drawn again for another screen or
 * level, the rest over a copy of it */
static uint16_t s_backdrop[256 * 192] __attribute__((aligned(32)));
static uint16_t s_static[256 * 192] __attribute__((aligned(32)));
static int s_have_backdrop;
static Shown s_static_of;

#define TEXT RGB(225, 232, 250)
#define DIM RGB(150, 160, 185)
#define GOLD RGB(255, 240, 160)

static void what_is_shown(Shown *s)
{
    const Game *g = &g_game;
    const PlayState *ps = &g->play;
    memset(s, 0, sizeof(*s));
    s->ready = 1;
    s->screen = g->screen == SCR_PLAY ? SCR_PLAY : g->screen == SCR_SELECT ? SCR_SELECT : SCR_TITLE;
    s->icon = g->save.icon;
    s->col1 = g->save.col1;
    s->col2 = g->save.col2;
    if (s->screen == SCR_TITLE) {
        s->total_attempts = g->save.progress.total_attempts;
        s->total_jumps = g->save.progress.total_jumps;
        return;
    }
    s->level = s->screen == SCR_PLAY ? ps->level_idx : g->sel_level;
    if (s->level < 0 || s->level >= SAVE_MAX_LEVELS) s->level = 0;
    s->best = g->save.progress.best[s->level];
    s->best_practice = g->save.progress.best_practice[s->level];
    s->saved_coins = g->save.progress.coins[s->level];
    s->total_attempts = g->save.progress.attempts[s->level];
    if (s->screen == SCR_PLAY) {
        s->practice = ps->practice;
        s->attempt = ps->attempt;
        s->run_coins = ps->p.coins;
        s->checkpoints = ps->ncp;
        s->paused = ps->paused;
    }
}

/* (panels are opaque here: the colour see-through ones come out over the
 * backdrop, where it is darkest; the CPU blends a pixel slowly) */
#define PANEL RGB(11, 13, 29)
#define PANEL_EDGE RGB(64, 68, 88)

static void backdrop(void)
{
    gfx_rect_v(0, 0, SCREEN_W, SCREEN_H, RGB(20, 24, 52), RGB(6, 8, 20));
    gfx_rect(0, 62, SCREEN_W, 62 + grid_h(2.0f), RGBA(255, 255, 255, 40));
    font_draw_fancy(SCREEN_W / 2, 16, 4.0f, COL_WHITE, RGB(190, 210, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, GAME_TITLE);
}

static Color c1_of(const Shown *s) { return g_player_colors[s->col1 % PLAYER_COLOR_COUNT]; }
static Color c2_of(const Shown *s) { return g_player_colors[s->col2 % PLAYER_COLOR_COUNT]; }

/* the menus' screen, in parts (0, 1) */
static void draw_controls(const Shown *s, int part)
{
    char buf[48];
    if (part == 1) {
        /* the player's cube, as chosen in the garage, and the totals */
        icon_draw_cube(80, 372, 64, 0.0f, s->icon, c1_of(s), c2_of(s));
        snprintf(buf, sizeof(buf), "ATTEMPTS %u", s->total_attempts);
        font_draw(140, 344, 2.0f, TEXT, ALIGN_LEFT, buf);
        snprintf(buf, sizeof(buf), "JUMPS %u", s->total_jumps);
        font_draw(140, 372, 2.0f, TEXT, ALIGN_LEFT, buf);
        font_draw(SCREEN_W - 12, SCREEN_H - 6 - font_height(1.0f), 1.0f, DIM, ALIGN_RIGHT, g_version);
        return;
    }
    render_panel(24, 84, SCREEN_W - 24, 300, PANEL, PANEL_EDGE);
    font_draw(48, 100, 2.0f, GOLD, ALIGN_LEFT, "CONTROLS");
    static const char *const rows[][2] = {
        {"JUMP", "A  B  UP  L  R"},
        {"", "OR TOUCH THIS SCREEN"},
        {"PAUSE", "START"},
        {"PRACTICE", "Y CHECKPOINT  X REMOVE"},
        {"MENUS", "A OK  B BACK"},
    };
    for (int i = 0; i < 5; i++) {
        font_draw(48, 132 + i * 30, 2.0f, DIM, ALIGN_LEFT, rows[i][0]);
        font_draw(186, 132 + i * 30, 2.0f, TEXT, ALIGN_LEFT, rows[i][1]);
    }
}

/* the level's name and difficulty (part 0) and its bests (1), from y down */
static void draw_records(const Shown *s, const LevelInfo *info, float y, int part)
{
    char buf[48];
    if (part == 0) {
        Color dc = difficulty_color(info->difficulty);
        draw_diff_badge(70, y + 34, info->difficulty, 26);
        font_draw_fancy(118, y + 8, 3.0f, COL_WHITE, RGB(200, 215, 255), RGB(0, 0, 0), 2.0f, ALIGN_LEFT, info->name);
        font_draw(118, y + 42, 2.0f, dc, ALIGN_LEFT, difficulty_name(info->difficulty));
        return;
    }
    float ty = font_center_y(y + 82, y + 94, 2.0f);
    float bx = 48 + font_width("PRACTICE", 2.0f) + 14;
    font_draw(48, ty, 2.0f, DIM, ALIGN_LEFT, "NORMAL");
    render_progress_bar(bx, y + 82, SCREEN_W - 120, y + 94, s->best / 100.0f, RGB(90, 255, 120), RGB(200, 255, 200));
    snprintf(buf, sizeof(buf), "%d%%", s->best);
    font_draw(SCREEN_W - 106, ty, 2.0f, TEXT, ALIGN_LEFT, buf);
    ty = font_center_y(y + 110, y + 122, 2.0f);
    font_draw(48, ty, 2.0f, DIM, ALIGN_LEFT, "PRACTICE");
    render_progress_bar(bx, y + 110, SCREEN_W - 120, y + 122, s->best_practice / 100.0f, RGB(80, 200, 255),
                        RGB(200, 240, 255));
    snprintf(buf, sizeof(buf), "%d%%", s->best_practice);
    font_draw(SCREEN_W - 106, ty, 2.0f, TEXT, ALIGN_LEFT, buf);
}

/* a coin as render_coin draws one face on (its rim, face, inner face and
 * bar), in discs the CPU fills in integers: a coin's triangles cost more
 * than the rest of the screen. Collected in this run, or before (pale),
 * or not yet (faint). */
static void coin(float cx, float cy, float r, int got, int saved)
{
    const float g = draw_pixel_grid() * 65536.0f;
    float a = got || saved ? 1.0f : 0.35f;
    Color c = got ? col_with_alpha(COL_COIN, a) : col_with_alpha(RGB(230, 240, 255), 0.45f * a);
    int32_t x = (int32_t)(cx * g), y = (int32_t)(cy * g);
    soft_disc(x, y, (int32_t)((r + 2) * g), col_with_alpha(RGB(40, 20, 0), 0.8f * a));
    soft_disc(x, y, (int32_t)(r * g), c);
    soft_disc(x, y, (int32_t)(r * 0.68f * g), col_scale(c, 0.78f));
    gfx_rect(cx - 2, cy - r * 0.35f, cx + 2, cy + r * 0.35f, col_scale(c, 1.2f));
}

static void draw_coins(int n, int got, int saved, float cx, float y)
{
    for (int i = 0; i < n; i++) coin(cx + (i - (n - 1) * 0.5f) * 44, y, 15, (got >> i) & 1, (saved >> i) & 1);
}

/* the level's header, read once a level */
static const LevelInfo *info_of(int level)
{
    static LevelInfo info;
    static int which = -1;
    if (which != level) {
        level_info(level, &info);
        which = level;
    }
    return &info;
}

/* the level select's screen, in parts (0, 1, 2) */
static void draw_select(const Shown *s, int part)
{
    const LevelInfo *info = info_of(s->level);
    char buf[48];
    if (part == 0) {
        render_panel(24, 84, SCREEN_W - 24, 330, PANEL, PANEL_EDGE);
        font_draw(SCREEN_W / 2, 372, 2.0f, TEXT, ALIGN_CENTER, "A PLAY   Y PRACTICE   B BACK");
        return;
    }
    draw_records(s, info, 100, part - 1);
    if (part == 1) return;
    font_draw(48, 252, 2.0f, DIM, ALIGN_LEFT, "COINS");
    draw_coins(info->ncoins, s->saved_coins, s->saved_coins, 220, 258);
    snprintf(buf, sizeof(buf), "ATTEMPTS %u", s->total_attempts);
    font_draw(48, 290, 2.0f, TEXT, ALIGN_LEFT, buf);
}

/* a run, in parts (0, 1, 2): what changes in it (the attempt, its coins,
 * the pause) apart */
static void draw_play_static(const Shown *s, int part)
{
    if (part == 0) {
        render_panel(24, 78, SCREEN_W - 24, 236, PANEL, PANEL_EDGE);
        /* where to touch */
        render_panel(24, 290, SCREEN_W - 24, SCREEN_H - 14, RGB(24, 28, 48), RGB(58, 62, 84));
        return;
    }
    draw_records(s, info_of(s->level), 90, part - 1);
}

/* the parts each screen's static layer is drawn in, a frame each */
static int parts_of(int screen)
{
    return screen == SCR_TITLE ? 2 : 3;
}

static void draw_play(const Shown *s)
{
    char buf[48];
    snprintf(buf, sizeof(buf), s->practice ? "PRACTICE  %d CHECKPOINT%s" : "ATTEMPT %d", s->practice ? s->checkpoints : s->attempt,
             s->checkpoints == 1 ? "" : "S");
    font_draw(48, 254, 2.0f, s->practice ? RGB(120, 220, 255) : GOLD, ALIGN_LEFT, buf);
    draw_coins(info_of(s->level)->ncoins, s->run_coins, s->saved_coins, SCREEN_W - 120, 260);
    font_draw(SCREEN_W / 2, 350, 3.0f, s->paused ? GOLD : RGB(150, 155, 175), ALIGN_CENTER,
              s->paused ? "PAUSED" : "TOUCH TO JUMP");
}

/* what the static layer shows: all of it but the run's */
static void static_part(Shown *s)
{
    if (s->screen != SCR_PLAY) return;
    s->attempt = s->run_coins = s->checkpoints = s->paused = 0;
}

void bottom_nds_init(void)
{
    soft_init();
}

void bottom_nds_update(uint32_t until)
{
    Shown s;
    what_is_shown(&s);
    /* (queued while the last is drawn, or drawn: then only when it changed) */
    if (!soft_busy() && memcmp(&s, &s_shown, sizeof(s))) {
        s_shown = s;
        gfx_nds_bottom_begin();
        if (!s_have_backdrop) {
            soft_target(s_backdrop);
            backdrop();
            s_have_backdrop = 1;
        }
        Shown st = s;
        static_part(&st);
        if (memcmp(&st, &s_static_of, sizeof(st))) {
            /* another static layer */
            s_static_of = st;
            soft_copy(s_static, s_backdrop);
            soft_target(s_static);
            for (int part = 0; part < parts_of(s.screen); part++) {
                if (s.screen == SCR_PLAY) draw_play_static(&s, part);
                else if (s.screen == SCR_SELECT) draw_select(&s, part);
                else draw_controls(&s, part);
            }
        }
        soft_target(NULL);
        soft_copy(soft_screen(), s_static);
        if (s.screen == SCR_PLAY) draw_play(&s);
        gfx_nds_bottom_end();
        soft_present();
    }
    soft_run(until);
}

void bottom_nds_vblank(void)
{
    soft_vblank();
}
