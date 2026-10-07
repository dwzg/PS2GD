/* The vector family's drawing of a level being played (play.c): the
 * player, the HUD, the pause menu and the results. */
#include <stdio.h>

#include "game_internal.h"
#include "draw.h"
#include "font.h"
#include "fx.h"
#include "icons.h"

static PlayState *ps_get(void) { return &g_game.play; }

/* Player pose for the frame being drawn (interpolated between ticks). */
typedef struct {
    float x, y, rot, angle;
} Pose;

static void draw_wave_trail(const PlayState *ps, const View *v, const Pose *pose, Color c)
{
    if (ps->trail_n < 2) return;
    /* the newest point follows the drawn player rather than the last tick */
    float tx[TRAIL_LEN], ty[TRAIL_LEN];
    memcpy(tx, ps->trail_x, sizeof(float) * (size_t)ps->trail_n);
    memcpy(ty, ps->trail_y, sizeof(float) * (size_t)ps->trail_n);
    tx[0] = pose->x;
    ty[0] = pose->y;
    gfx_blend(BLEND_ADD);
    for (int i = 0; i + 1 < ps->trail_n; i++) {
        float a0 = 1.0f - (float)i / ps->trail_n, a1 = 1.0f - (float)(i + 1) / ps->trail_n;
        draw_line2(view_sx(v, tx[i]), view_sy(v, ty[i]), view_sx(v, tx[i + 1]), view_sy(v, ty[i + 1]),
                   9.0f * a0 + 2.0f, col_with_alpha(c, 0.7f * a0), col_with_alpha(c, 0.7f * a1));
    }
    gfx_blend(BLEND_ALPHA);
    for (int i = 0; i + 1 < ps->trail_n; i++) {
        float a0 = 1.0f - (float)i / ps->trail_n;
        draw_line(view_sx(v, tx[i]), view_sy(v, ty[i]), view_sx(v, tx[i + 1]), view_sy(v, ty[i + 1]), 3.0f,
                  col_with_alpha(COL_WHITE, 0.9f * a0));
    }
}

static void draw_player(const PlayState *ps, const View *v, const Pose *pose)
{
    const Game *g = &g_game;
    const Player *p = &ps->p;
    Color c1 = g_player_colors[g->save.col1 % PLAYER_COLOR_COUNT];
    Color c2 = g_player_colors[g->save.col2 % PLAYER_COLOR_COUNT];
    float sx = view_sx(v, pose->x), sy = view_sy(v, pose->y);
    float ang = pose->angle;
    int flip = p->grav < 0;

    if (p->mode == MODE_WAVE) draw_wave_trail(ps, v, pose, c2);
    draw_glow(sx, sy, BLOCK_PX * 1.3f, col_with_alpha(c1, 0.22f + 0.15f * v->pulse));

    switch (p->mode) {
    case MODE_CUBE: icon_draw_cube(sx, sy, BLOCK_PX, pose->rot, g->save.icon, c1, c2); break;
    case MODE_SHIP: icon_draw_ship(sx, sy, BLOCK_PX, flip ? -ang : ang, flip, g->save.icon, c1, c2); break;
    case MODE_BALL: icon_draw_ball(sx, sy, BLOCK_PX, pose->rot, g->save.icon, c1, c2); break;
    case MODE_UFO: icon_draw_ufo(sx, sy, BLOCK_PX, ang, flip, g->save.icon, c1, c2); break;
    case MODE_WAVE: icon_draw_wave(sx, sy, BLOCK_PX * 0.85f, ang, c1, c2); break;
    }
}

static void draw_hud(const PlayState *ps)
{
    float frac = sim_x(&ps->p) / ps->L->end_x;
    if (ps->phase == PH_COMPLETE) frac = 1.0f;
    float x0 = UI_X(180), x1 = UI_X(440);
    render_progress_bar(x0, 14, x1, 24, frac, RGB(90, 255, 120), RGB(255, 255, 255));
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", clampi((int)(frac * 100.0f), 0, 100));
    font_draw_fancy(x1 + 14, font_center_y(14, 24, 2.0f), 2.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 2.0f,
                    ALIGN_LEFT, buf);

    if (ps->practice) {
        font_draw_fancy(SCREEN_W / 2, 32, 2.0f, RGB(120, 255, 150), RGB(40, 200, 90), RGB(0, 0, 0), 2.0f,
                        ALIGN_CENTER, "PRACTICE");
        if (ps->attempt_time < 5.0f && ps->attempt <= 2) {
            float a = clampf(5.0f - ps->attempt_time, 0.0f, 1.0f);
            font_draw(SCREEN_W / 2, SCREEN_H - 26, 2.0f, col_with_alpha(COL_WHITE, a), ALIGN_CENTER,
                      GLYPH_SQUARE " CHECKPOINT   " GLYPH_TRIANGLE " REMOVE");
        }
    }
    if (ps->best_popup_t > 0.0f) {
        /* Pops in, a little past full size, then fades out. It stays up into
         * the next attempt, so it sits above that attempt's counter. While
         * it grows it is drawn off the pixel grid (font_draw_fancy_grown):
         * on the grid it could only grow a whole screen pixel at a time, on
         * the PSP in one step from small to full size. It grows to the size
         * it has on the grid, its outline with it, and then goes onto the
         * grid. */
        float t = 2.0f - ps->best_popup_t;
        int grow = t < 0.35f;
        float k = grow ? ease_out_back(t / 0.35f) : 1.0f;
        float h = grow ? 7.0f * (font_pixel_y(4.0f) * k) : font_height(4.0f);
        float a = clampf(ps->best_popup_t / 0.4f, 0.0f, 1.0f);
        Color top = col_with_alpha(RGB(255, 255, 160), a), bottom = col_with_alpha(RGB(255, 170, 40), a);
        snprintf(buf, sizeof(buf), "NEW BEST %d%%", ps->best_popup_val);
        if (grow)
            font_draw_fancy_grown(SCREEN_W / 2, 104 - h * 0.5f, 4.0f, k, top, bottom, col_with_alpha(RGB(0, 0, 0), a),
                                  3.0f, ALIGN_CENTER, buf);
        else
            font_draw_fancy(SCREEN_W / 2, 104 - h * 0.5f, 4.0f, top, bottom, col_with_alpha(RGB(0, 0, 0), a), 3.0f,
                            ALIGN_CENTER, buf);
    }
}

static void draw_pause(const PlayState *ps)
{
    const Game *g = &g_game;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
    render_panel(UI_X(120), 60, UI_X(520), 400, RGBA(10, 14, 30, 230), RGBA(255, 255, 255, 160));
    font_draw_fancy(SCREEN_W / 2, 80, 4.0f, COL_WHITE, RGB(190, 210, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, ps->info.name);

    int idx = ps->level_idx < SAVE_MAX_LEVELS ? ps->level_idx : 0;
    char buf[48];
    float ty = font_center_y(130, 142, 2.0f); /* text beside the bars */
    font_draw(UI_X(150), ty, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "NORMAL");
    render_progress_bar(UI_X(250), 130, UI_X(440), 142, g->save.progress.best[idx] / 100.0f, RGB(90, 255, 120), RGB(200, 255, 200));
    snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best[idx]);
    font_draw(UI_X(452), ty, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    ty = font_center_y(156, 168, 2.0f);
    font_draw(UI_X(150), ty, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "PRACTICE");
    render_progress_bar(UI_X(250), 156, UI_X(440), 168, g->save.progress.best_practice[idx] / 100.0f, RGB(80, 200, 255), RGB(200, 240, 255));
    snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best_practice[idx]);
    font_draw(UI_X(452), ty, 2.0f, COL_WHITE, ALIGN_LEFT, buf);

    const char *items[4] = {"RESUME", "RESTART", ps->practice ? "NORMAL MODE" : "PRACTICE MODE", "EXIT LEVEL"};
    for (int i = 0; i < 4; i++) {
        float y = 196 + i * 42;
        int sel = i == ps->pause_sel;
        if (sel) {
            render_panel(UI_X(170), y - 8, UI_X(470), y + 30, RGBA(255, 255, 255, 40), RGB(120, 255, 150));
        }
        font_draw_fancy(SCREEN_W / 2, y, 3.0f, sel ? RGB(255, 255, 255) : RGB(170, 180, 200),
                        sel ? RGB(150, 255, 170) : RGB(110, 120, 140), RGB(0, 0, 0), 2.0f, ALIGN_CENTER, items[i]);
    }
    snprintf(buf, sizeof(buf), "ATTEMPT %d", ps->attempt);
    /* about as far above the bottom border as the level name is below the top */
    font_draw(SCREEN_W / 2, 368, 2.0f, RGB(160, 170, 190), ALIGN_CENTER, buf);
}

static void draw_results(const PlayState *ps)
{
    const Game *g = &g_game;
    float t = ps->phase_t;
    if (t < 0.4f) return;
    /* pops in like the NEW BEST popup (draw_hud) */
    int grow = t < 0.7f;
    float k = grow ? ease_out_back((t - 0.4f) / 0.3f) : 1.0f;
    float h = grow ? 7.0f * (font_pixel_y(5.0f) * k) : font_height(5.0f);
    if (grow)
        font_draw_fancy_grown(SCREEN_W / 2, 90 - h * 0.5f, 5.0f, k, RGB(255, 255, 170), RGB(255, 170, 30),
                              RGB(0, 0, 0), 3.0f, ALIGN_CENTER, "LEVEL COMPLETE!");
    else
        font_draw_fancy(SCREEN_W / 2, 90 - h * 0.5f, 5.0f, RGB(255, 255, 170), RGB(255, 170, 30), RGB(0, 0, 0),
                        3.0f, ALIGN_CENTER, "LEVEL COMPLETE!");
    if (t < 1.6f) return;
    float a = clampf((t - 1.6f) / 0.3f, 0.0f, 1.0f);
    render_panel(UI_X(140), 130, UI_X(500), 380, RGBA(10, 14, 30, (int)(220 * a)), RGBA(255, 255, 255, (int)(160 * a)));
    char buf[64];
    snprintf(buf, sizeof(buf), "ATTEMPTS: %d", ps->attempt);
    font_draw(SCREEN_W / 2, 152, 3.0f, COL_WHITE, ALIGN_CENTER, buf);
    snprintf(buf, sizeof(buf), "JUMPS: %d", ps->jumps_session);
    font_draw(SCREEN_W / 2, 184, 3.0f, COL_WHITE, ALIGN_CENTER, buf);
    int secs = (int)ps->time_session;
    snprintf(buf, sizeof(buf), "TIME: %d:%02d", secs / 60, secs % 60);
    font_draw(SCREEN_W / 2, 216, 3.0f, COL_WHITE, ALIGN_CENTER, buf);

    /* coins */
    int n = ps->L->ncoins;
    uint8_t have = ps->level_idx < SAVE_MAX_LEVELS ? g->save.progress.coins[ps->level_idx] : 0;
    for (int i = 0; i < n; i++) {
        float cx = SCREEN_W / 2 + (i - (n - 1) * 0.5f) * 50;
        int got = (have >> i) & 1;
        int fresh = (ps->coins_gained >> i) & 1;
        float sc = fresh ? 1.0f + 0.2f * sinf(g->t * 8.0f) : 1.0f;
        if (got) render_coin(cx, 268, 15 * sc, g->t * 3.0f + i, 1.0f, 0);
        else draw_circle(cx, 268, 15, RGBA(0, 0, 0, 150));
    }
    if (ps->practice)
        font_draw(SCREEN_W / 2, 292, 2.0f, RGB(120, 255, 150), ALIGN_CENTER, "PRACTICE RUN");
    else if (ps->new_best)
        font_draw(SCREEN_W / 2, 292, 2.0f, RGB(255, 230, 100), ALIGN_CENTER, "FIRST CLEAR!");

    const char *opts[2] = {"MENU", "REPLAY"};
    for (int i = 0; i < 2; i++) {
        float cx = SCREEN_W / 2 + (i == 0 ? -90 : 90);
        int sel = ps->results_sel == i;
        if (sel) render_panel(cx - 75, 318, cx + 75, 360, RGBA(255, 255, 255, 40), RGB(120, 255, 150));
        font_draw_fancy(cx, 328, 3.0f, sel ? COL_WHITE : RGB(160, 170, 190), sel ? RGB(150, 255, 170) : RGB(110, 120, 140),
                        RGB(0, 0, 0), 2.0f, ALIGN_CENTER, opts[i]);
    }
}

static Pose make_pose(const PlayState *ps)
{
    const float a = g_game.alpha;
    Pose pose = {lerpf(ps->prev_x, sim_x(&ps->p), a), lerpf(ps->prev_y, sim_y(&ps->p), a), lerpf(ps->prev_rot, ps->rot, a),
                 lerpf(ps->prev_angle, ps->vis_angle, a)};
    return pose;
}

/* --- also used by the title screen's demo run (demo_draw.c) --------- */

void play_view(const PlayState *ps, View *v)
{
    const Game *g = &g_game;
    play_camera(ps, &v->cam_x, &v->cam_y);
    view_snap(v);
    v->time = g->t;
    v->pulse = beat_pulse();
    v->pal = &ps->pal;
}

void play_draw_player(const PlayState *ps, const View *v)
{
    Pose pose = make_pose(ps);
    draw_player(ps, v, &pose);
}

void play_render(void)
{
    const Game *g = &g_game;
    const PlayState *ps = ps_get();
    const float a = g->alpha;
    Pose pose = make_pose(ps);
    View v;
    play_view(ps, &v);

    render_background(&v);
    render_ground(&v, ps->corr_floor, ps->corr_ceil, ps->corr_alpha);

    /* attempt counter painted into the world near the start */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), ps->practice ? "PRACTICE %d" : "ATTEMPT %d", ps->attempt);
        float ax = view_sx(&v, 3.0f), ay = view_sy(&v, 6.2f);
        if (ax > -400) font_draw_fancy(ax, ay, 4.0f, COL_WHITE, RGB(210, 225, 255), RGB(0, 0, 0), 3.0f, ALIGN_LEFT, buf);
    }

    uint8_t saved = ps->level_idx < SAVE_MAX_LEVELS ? g->save.progress.coins[ps->level_idx] : 0;
    render_level(&v, ps->L, &ps->p, saved);

    /* practice checkpoints */
    if (ps->practice) {
        for (int i = 0; i < ps->ncp; i++) {
            float cx = view_sx(&v, sim_x(&ps->cp[i].p)), cy = view_sy(&v, sim_y(&ps->cp[i].p));
            if (cx < -20 || cx > SCREEN_W + 20) continue;
            render_checkpoint(cx, cy);
        }
    }

    if (ps->phase != PH_DEAD) draw_player(ps, &v, &pose);
    fx_draw(FX_WORLD, v.cam_x, v.cam_y, (1.0f - a) * TICK_DT);

    if (ps->flash > 0.0f) {
        gfx_blend(BLEND_ADD);
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_with_alpha(COL_WHITE, ps->flash * 0.5f));
        gfx_blend(BLEND_ALPHA);
    }

    draw_hud(ps);
    if (ps->phase == PH_COMPLETE) draw_results(ps);
    if (ps->paused) draw_pause(ps);
}
