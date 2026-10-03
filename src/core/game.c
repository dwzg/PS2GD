#include <stdio.h>

#include "game_internal.h"
#include "audio.h"
#include "draw.h"
#include "font.h"
#include "fx.h"
#include "icons.h"
#include "version.h"

Game g_game;

#define FADE_TIME 0.22f

float beat_pulse(void)
{
    const Game *g = &g_game;
    const PlayState *ps = &g->play;
    int song = audio_current_song();
    if (song < 0) return 0.0f;
    float b;
    if (g->screen == SCR_PLAY && !ps->practice && ps->phase == PH_RUN) {
        /* the song started with the attempt: the level clock is exact and
         * smoother than the audio thread's chunked position */
        float t = ps->attempt_time - (1.0f - g->alpha) * TICK_DT;
        b = t * audio_song_bpm(song) / 60.0f;
    } else {
        b = audio_song_beat();
    }
    if (b < 0.0f) return 0.0f;
    float f = b - floorf(b);
    float accent = ((int)b % 4) == 0 ? 1.0f : 0.65f; /* stronger on each bar's downbeat */
    return accent * expf(-f * 5.0f);
}

static void on_enter(int scr)
{
    Game *g = &g_game;
    switch (scr) {
    case SCR_TITLE:
    case SCR_SELECT:
    case SCR_GARAGE:
    case SCR_OPTIONS:
        if (audio_current_song() != SONG_MENU) audio_play_song(SONG_MENU, 0.0f);
        if (g->save_dirty) {
            save_store(&g->save);
            g->save_dirty = 0;
        }
        break;
    case SCR_PLAY:
        play_start(g->sel_level, g->start_practice);
        break;
    }
}

void screen_go(int scr)
{
    Game *g = &g_game;
    if (g->fading) return;
    g->next_screen = scr;
    g->fading = 1;
}

void game_flush_save(void)
{
    if (g_game.save_dirty) {
        save_store(&g_game.save);
        g_game.save_dirty = 0;
    }
}

void game_status(char *buf, int cap)
{
    static const char *names[] = {"title", "select", "play", "garage", "options"};
    const Game *g = &g_game;
    if (g->screen == SCR_PLAY && g->play.L)
        snprintf(buf, (size_t)cap, "screen=play level=%d attempt=%d phase=%d x=%.1f%s best=%d", g->play.level_idx,
                 g->play.attempt, g->play.phase, (double)g->play.p.x, g->play.practice ? " practice" : "",
                 g->save.best[g->play.level_idx % SAVE_MAX_LEVELS]);
    else
        snprintf(buf, (size_t)cap, "screen=%s sel=%d level=%d song=%d", names[g->screen % 5], g->menu_sel,
                 g->sel_level, audio_current_song());
}

unsigned game_attempts_started(void)
{
    return play_attempts_started();
}

void game_init(void)
{
    Game *g = &g_game;
    memset(g, 0, sizeof(*g));
    draw_init();
    font_init();
    fx_clear();
    save_load(&g->save);
    audio_set_volume(g->save.music_vol, g->save.sfx_vol);
    audio_set_user_delay(g->save.audio_delay * 0.01f);
    g->screen = SCR_TITLE;
    g->menu_sel = 1;
    g->alpha = 1.0f;
    g->fade = 1.0f;
    g->fading = -1;
    on_enter(SCR_TITLE);
}

static void update_input(uint32_t held)
{
    Game *g = &g_game;
    g->held = held;
    g->pressed = held & ~g->prev;
    g->prev = held;
    g->repeat = g->pressed;
    static const uint32_t dirs[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
    for (int i = 0; i < 4; i++) {
        if (!(held & dirs[i])) {
            g->rep_t[i] = 0.0f;
            continue;
        }
        g->rep_t[i] += TICK_DT;
        if (g->rep_t[i] > 0.38f) {
            g->rep_t[i] -= 0.09f;
            g->repeat |= dirs[i];
        }
    }
}

void game_tick(uint32_t held)
{
    Game *g = &g_game;
    update_input(held);
    g->t += TICK_DT;

    if (g->fading > 0) {
        g->fade += TICK_DT / FADE_TIME;
        if (g->fade >= 1.0f) {
            g->fade = 1.0f;
            if (g->screen == SCR_PLAY && g->next_screen != SCR_PLAY) play_exit();
            g->screen = g->next_screen;
            g->fading = -1;
            fx_clear_space(FX_SCREEN);
            on_enter(g->screen);
        }
        fx_update(TICK_DT);
        return;
    }
    if (g->fading < 0) {
        g->fade -= TICK_DT / FADE_TIME;
        if (g->fade <= 0.0f) {
            g->fade = 0.0f;
            g->fading = 0;
        }
    }

    if (g->screen == SCR_PLAY) play_tick();
    else menus_tick();
    fx_update(TICK_DT);
}

void game_render(float alpha)
{
    Game *g = &g_game;
    /* Everything drawn below reads g->t for its animations; let it see the
     * interpolated time so they advance as evenly as the scrolling. */
    float tick_t = g->t;
    /* while fading out the screen is frozen (nothing ticks): no blending */
    g->alpha = g->fading > 0 ? 1.0f : clampf(alpha, 0.0f, 1.0f);
    g->t -= (1.0f - g->alpha) * TICK_DT;
    gfx_blend(BLEND_ALPHA);
    if (g->screen == SCR_PLAY) play_render();
    else menus_render();
    if (g->fade > 0.0f) gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_with_alpha(COL_BLACK, g->fade));
    g->t = tick_t;
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

void draw_menu_backdrop(const Palette *pal, float scroll, float pulse)
{
    View v;
    v.cam_x = scroll;
    v.cam_y = -2.4f;
    v.time = g_game.t;
    v.pulse = pulse;
    v.pal = pal;
    render_background(&v);
    render_ground(&v, 0.0f, CORRIDOR_H, 0.0f);
}

static void menu_palette(Palette *out)
{
    Game *g = &g_game;
    int a = (int)(g->t / 6.0f) % PALETTE_COUNT;
    int b = (a + 1) % PALETTE_COUNT;
    float f = fmodf(g->t, 6.0f) / 6.0f;
    f = smoothstepf((f - 0.8f) / 0.2f);
    palette_lerp(out, &g_palettes[a], &g_palettes[b], f);
}

static Color c1_of(void) { return g_player_colors[g_game.save.col1 % PLAYER_COLOR_COUNT]; }
static Color c2_of(void) { return g_player_colors[g_game.save.col2 % PLAYER_COLOR_COUNT]; }

/* --- title ---------------------------------------------------------- */

/* The menu song's beat as it is heard, or NULL while it isn't playing (with
 * no audio output its clock stands still). */
static const float *menu_beat(void)
{
    static float last, beat;
    static int still;
    float t = audio_song_time();
    still = t == last ? still + 1 : 0;
    last = t;
    beat = audio_song_beat();
    return audio_current_song() == SONG_MENU && still < 30 ? &beat : NULL;
}

static void title_tick(void)
{
    Game *g = &g_game;
    demo_tick(&g->demo, menu_beat());

    if (g->repeat & BTN_LEFT) { g->menu_sel = (g->menu_sel + 2) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_RIGHT) { g->menu_sel = (g->menu_sel + 1) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->pressed & (BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_MENU_SELECT);
        static const int dest[3] = {SCR_GARAGE, SCR_SELECT, SCR_OPTIONS};
        screen_go(dest[g->menu_sel]);
    }
}

static void draw_button(float cx, float cy, float size, int selected, Color c, int kind)
{
    Game *g = &g_game;
    float s = size * (selected ? 1.08f + 0.04f * sinf(g->t * 6.0f) : 1.0f);
    if (selected) draw_glow(cx, cy, s * 1.3f, col_with_alpha(c, 0.5f));
    render_panel(cx - s * 0.5f - 4, cy - s * 0.5f - 4, cx + s * 0.5f + 4, cy + s * 0.5f + 4, RGBA(0, 0, 0, 200),
                 RGBA(0, 0, 0, 220));
    render_panel(cx - s * 0.5f, cy - s * 0.5f, cx + s * 0.5f, cy + s * 0.5f, c,
                 selected ? COL_WHITE : col_scale(c, 1.3f));
    gfx_rect_v(cx - s * 0.5f + 6, cy - s * 0.5f + 4, cx + s * 0.5f - 6, cy - s * 0.1f,
               RGBA(255, 255, 255, 70), RGBA(255, 255, 255, 0));
    switch (kind) {
    case 0: icon_draw_cube(cx, cy, s * 0.5f, 0.0f, g->save.icon, c1_of(), c2_of()); break;
    case 1: {
        float r = s * 0.28f;
        gfx_tri(cx - r * 0.7f - 3, cy - r - 4, RGB(0, 0, 0), cx + r + 5, cy, RGB(0, 0, 0), cx - r * 0.7f - 3, cy + r + 4,
                RGB(0, 0, 0));
        gfx_tri(cx - r * 0.7f, cy - r, COL_WHITE, cx + r, cy, COL_WHITE, cx - r * 0.7f, cy + r, RGB(220, 230, 255));
        break;
    }
    default:
        render_saw(cx, cy, s * 0.3f, g->t * 1.5f, RGB(30, 30, 40), COL_WHITE);
        break;
    }
}

static void title_render(void)
{
    Game *g = &g_game;
    Palette pal;
    menu_palette(&pal);
    float pulse = beat_pulse();
    demo_render(&g->demo, &pal);

    /* Vertical layout, with room for the title's bob (-4..+7 px) and the
     * selected button's pulse (up to 1.12x): title 26-105, subtitle to 125,
     * buttons 135-255, labels 264-282; the demo run plays below. */
    float bob = sinf(g->t * 2.0f) * 4.0f + pulse * 3.0f;
    font_draw_fancy(SCREEN_W / 2 + 4, 30 + bob + 5, 9.0f, RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 120), RGBA(0, 0, 0, 0), 0.0f,
                    ALIGN_CENTER, GAME_TITLE);
    font_draw_fancy(SCREEN_W / 2, 30 + bob, 9.0f, RGB(255, 250, 200), RGB(255, 170, 40), RGB(20, 10, 0), 4.0f,
                    ALIGN_CENTER, GAME_TITLE);
    font_draw(SCREEN_W / 2, 104 + bob, 2.0f, col_with_alpha(COL_WHITE, 0.85f), ALIGN_CENTER,
              "A RHYTHM PLATFORMER FOR " TARGET_NAME);

    static const Color cols[3] = {RGB(60, 190, 255), RGB(70, 220, 90), RGB(255, 150, 50)};
    static const char *labels[3] = {"GARAGE", "PLAY", "OPTIONS"};
    for (int i = 0; i < 3; i++) {
        float cx = SCREEN_W / 2 + (i - 1) * 170.0f;
        float size = i == 1 ? 100.0f : 74.0f;
        draw_button(cx, 195, size, g->menu_sel == i, cols[i], i);
        int sel = g->menu_sel == i;
        font_draw_fancy(cx, 266, 2.0f, sel ? COL_WHITE : RGB(210, 220, 235), sel ? RGB(255, 240, 160) : RGB(170, 180, 200),
                        RGB(0, 0, 0), 2.0f, ALIGN_CENTER, labels[i]);
    }
    font_draw(SCREEN_W / 2, SCREEN_H - 20, 2.0f, col_with_alpha(COL_WHITE, 0.7f + 0.3f * sinf(g->t * 4.0f)),
              ALIGN_CENTER, GLYPH_LEFT GLYPH_RIGHT " SELECT   " GLYPH_CROSS " OK");
    /* 5 px above the bottom edge (the font can come out taller on a pixel grid) */
    font_draw(SCREEN_W - 8, SCREEN_H - 5 - font_height(1.0f), 1.0f, col_with_alpha(COL_WHITE, 0.5f), ALIGN_RIGHT,
              g_version);
}

/* --- level select --------------------------------------------------- */

static void select_tick(void)
{
    Game *g = &g_game;
    int n = g_level_count;
    if (g->repeat & BTN_LEFT) {
        g->sel_level--;
        audio_sfx(SFX_MENU_MOVE);
    }
    if (g->repeat & BTN_RIGHT) {
        g->sel_level++;
        audio_sfx(SFX_MENU_MOVE);
    }
    /* wrap while keeping the scroll animation continuous */
    if (g->sel_level < 0) { g->sel_level += n; g->sel_scroll += n; }
    if (g->sel_level >= n) { g->sel_level -= n; g->sel_scroll -= n; }
    g->sel_scroll = lerpf(g->sel_scroll, (float)g->sel_level, 1.0f - expf(-TICK_DT * 12.0f));

    if (g->pressed & (BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_START);
        g->start_practice = 0;
        screen_go(SCR_PLAY);
    } else if (g->pressed & BTN_SQUARE) {
        audio_sfx(SFX_START);
        g->start_practice = 1;
        screen_go(SCR_PLAY);
    } else if (g->pressed & BTN_CIRCLE) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

static Color diff_color(int d)
{
    static const Color c[6] = {RGB(90, 220, 255), RGB(90, 255, 120), RGB(255, 220, 60),
                               RGB(255, 140, 40), RGB(255, 70, 160), RGB(255, 40, 40)};
    return c[clampi(d, 0, 5)];
}

static void draw_diff_badge(float cx, float cy, int d, float r)
{
    Color c = diff_color(d);
    draw_circle(cx, cy, r + 4, RGB(0, 0, 0));
    draw_circle_grad(cx, cy, r, col_lerp(c, COL_WHITE, 0.45f), c);
    /* simple face: two eyes and a mouth that gets angrier with difficulty */
    Color k = RGB(10, 10, 16);
    float ey = cy - r * 0.18f;
    float tilt = d * 0.06f * r;
    float exy1[8] = {cx - r * 0.55f, ey - r * 0.12f - tilt, cx - r * 0.15f, ey - r * 0.12f + tilt,
                     cx - r * 0.15f, ey + r * 0.16f, cx - r * 0.55f, ey + r * 0.16f};
    float exy2[8] = {cx + r * 0.15f, ey - r * 0.12f + tilt, cx + r * 0.55f, ey - r * 0.12f - tilt,
                     cx + r * 0.55f, ey + r * 0.16f, cx + r * 0.15f, ey + r * 0.16f};
    draw_poly(exy1, 4, k);
    draw_poly(exy2, 4, k);
    float my = cy + r * 0.38f;
    float curve = (2.5f - d) * 0.07f * r;
    draw_line(cx - r * 0.4f, my - curve, cx, my + curve, r * 0.14f, k);
    draw_line(cx, my + curve, cx + r * 0.4f, my - curve, r * 0.14f, k);
}

static void draw_level_card(int idx, float cx)
{
    Game *g = &g_game;
    LevelInfo info;
    level_info(idx, &info);
    float x0 = cx - 230, x1 = cx + 230;
    render_panel(x0, 74, x1, 300, RGBA(0, 0, 0, 150), RGBA(255, 255, 255, 140));
    draw_diff_badge(x0 + 62, 140, info.difficulty, 38);
    font_draw(x0 + 62, 188, 2.0f, diff_color(info.difficulty), ALIGN_CENTER, difficulty_name(info.difficulty));

    char buf[48];
    float tw = font_width(info.name, 4.0f);
    float scale = tw > 300 ? 3.0f : 4.0f;
    font_draw_fancy(x0 + 300, 104, scale, COL_WHITE, RGB(210, 225, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, info.name);
    snprintf(buf, sizeof(buf), "%d " GLYPH_STAR, info.stars);
    font_draw_fancy(x0 + 300, 148, 3.0f, RGB(255, 245, 160), RGB(255, 190, 40), RGB(0, 0, 0), 2.0f, ALIGN_CENTER, buf);

    int s = idx < SAVE_MAX_LEVELS ? idx : 0;
    uint8_t coins = g->save.coins[s];
    for (int i = 0; i < info.ncoins; i++) {
        float ccx = x0 + 300 + (i - (info.ncoins - 1) * 0.5f) * 40;
        if ((coins >> i) & 1) render_coin(ccx, 196, 13, 0.0f, 1.0f, 0);
        else {
            draw_circle(ccx, 196, 14, RGBA(0, 0, 0, 160));
            draw_ring(ccx, 196, 11, 14, RGBA(255, 255, 255, 60));
        }
    }

    font_draw(x0 + 30, 232, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "NORMAL");
    render_progress_bar(x0 + 140, 232, x1 - 80, 244, g->save.best[s] / 100.0f, RGB(90, 255, 120), RGB(210, 255, 210));
    snprintf(buf, sizeof(buf), "%d%%", g->save.best[s]);
    font_draw(x1 - 66, 232, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    font_draw(x0 + 30, 262, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "PRACTICE");
    render_progress_bar(x0 + 140, 262, x1 - 80, 274, g->save.best_practice[s] / 100.0f, RGB(80, 200, 255),
                        RGB(210, 240, 255));
    snprintf(buf, sizeof(buf), "%d%%", g->save.best_practice[s]);
    font_draw(x1 - 66, 262, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    if (g->save.best[s] >= 100) {
        font_draw_fancy(x1 - 20, 82, 2.0f, RGB(160, 255, 170), RGB(60, 220, 100), RGB(0, 0, 0), 2.0f, ALIGN_RIGHT,
                        "COMPLETE");
    }
}

static void select_render(void)
{
    Game *g = &g_game;
    int n = g_level_count;
    float sc = g->sel_scroll;
    int a = ((int)floorf(sc) % n + n) % n, b = (a + 1) % n;
    float f = sc - floorf(sc);
    LevelInfo ia, ib;
    level_info(a, &ia);
    level_info(b, &ib);
    Palette pal;
    palette_lerp(&pal, &g_palettes[clampi(ia.pal, 0, PALETTE_COUNT - 1)],
                 &g_palettes[clampi(ib.pal, 0, PALETTE_COUNT - 1)], f);
    draw_menu_backdrop(&pal, g->t * 2.0f + sc * 12.0f, beat_pulse());

    font_draw_fancy(SCREEN_W / 2, 24, 3.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER,
                    "SELECT LEVEL");
    for (int k = -1; k <= 1; k++) {
        int base = (int)floorf(sc) + k;
        for (int j = 0; j <= 1; j++) {
            int i = base + j;
            float off = (i - sc) * SCREEN_W;
            if (off < -SCREEN_W || off > SCREEN_W) continue;
            if (k != 0 && j == 0) continue;
            draw_level_card(((i % n) + n) % n, SCREEN_W / 2 + off);
        }
    }

    float ap = 6.0f * sinf(g->t * 5.0f);
    font_draw_fancy(UI_X(30) - ap, 170, 5.0f, COL_WHITE, RGB(200, 210, 230), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, GLYPH_LEFT);
    font_draw_fancy(UI_X(610) + ap, 170, 5.0f, COL_WHITE, RGB(200, 210, 230), RGB(0, 0, 0), 3.0f, ALIGN_CENTER,
                    GLYPH_RIGHT);

    for (int i = 0; i < n; i++) {
        float x = SCREEN_W / 2 + (i - (n - 1) * 0.5f) * 18;
        draw_circle(x, 318, i == g->sel_level ? 6.0f : 4.0f, i == g->sel_level ? COL_WHITE : RGBA(255, 255, 255, 90));
    }
    font_draw_fancy(SCREEN_W / 2, 336, 2.0f, COL_WHITE, RGB(220, 230, 255), RGB(0, 0, 0), 2.0f, ALIGN_CENTER,
                    GLYPH_CROSS " PLAY   " GLYPH_SQUARE " PRACTICE   " GLYPH_CIRCLE " BACK");
}

/* --- garage --------------------------------------------------------- */

static void garage_tick(void)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) { g->garage_row = (g->garage_row + 2) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_DOWN) { g->garage_row = (g->garage_row + 1) % 3; audio_sfx(SFX_MENU_MOVE); }
    int d = 0;
    if (g->repeat & BTN_LEFT) d = -1;
    if (g->repeat & BTN_RIGHT) d = 1;
    if (d) {
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
        switch (g->garage_row) {
        case 0: g->save.icon = (uint8_t)((g->save.icon + ICON_COUNT + d) % ICON_COUNT); break;
        case 1: g->save.col1 = (uint8_t)((g->save.col1 + PLAYER_COLOR_COUNT + d) % PLAYER_COLOR_COUNT); break;
        default: g->save.col2 = (uint8_t)((g->save.col2 + PLAYER_COLOR_COUNT + d) % PLAYER_COLOR_COUNT); break;
        }
    }
    if (g->pressed & (BTN_L1 | BTN_SQUARE)) g->garage_mode = (g->garage_mode + MODE_COUNT - 1) % MODE_COUNT;
    if (g->pressed & (BTN_R1 | BTN_TRIANGLE)) g->garage_mode = (g->garage_mode + 1) % MODE_COUNT;
    if (g->pressed & (BTN_CIRCLE | BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

static void garage_render(void)
{
    Game *g = &g_game;
    draw_menu_backdrop(&g_palettes[7], g->t * 1.5f, beat_pulse());
    font_draw_fancy(SCREEN_W / 2, 20, 4.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, "GARAGE");

    /* vehicle previews */
    render_panel(UI_X(60), 60, UI_X(580), 170, RGBA(0, 0, 0, 180), RGBA(255, 255, 255, 90));
    static const char *mnames[MODE_COUNT] = {"CUBE", "SHIP", "BALL", "UFO", "WAVE"};
    for (int m = 0; m < MODE_COUNT; m++) {
        float cx = UI_X(112 + m * 104);
        int sel = m == g->garage_mode;
        float size = sel ? 52.0f : 38.0f;
        float ang = m == MODE_BALL ? g->t * 3.0f : (m == MODE_WAVE ? -0.6f : 0.0f);
        if (sel) draw_glow(cx, 108, 60, col_with_alpha(c1_of(), 0.35f));
        icon_draw_mode(m, cx, 108, size, ang, 0, g->save.icon, c1_of(), c2_of());
        font_draw(cx, 148, 2.0f, sel ? COL_WHITE : RGB(150, 160, 180), ALIGN_CENTER, mnames[m]);
    }

    /* icon row */
    const char *rows[3] = {"ICON", "COLOR 1", "COLOR 2"};
    for (int r = 0; r < 3; r++) {
        float y = 200 + r * 72;
        int sel = g->garage_row == r;
        render_panel(UI_X(40), y - 6, UI_X(600), y + 58, RGBA(0, 0, 0, sel ? 210 : 170), sel ? RGB(120, 255, 150) : RGBA(255, 255, 255, 60));
        font_draw(UI_X(56), y + 2, 2.0f, sel ? COL_WHITE : RGB(160, 170, 190), ALIGN_LEFT, rows[r]);
        if (r == 0) {
            for (int i = 0; i < ICON_COUNT; i++) {
                float cx = UI_X(80 + i * 66), cy = y + 37;
                int on = g->save.icon == i;
                if (on) gfx_rect(cx - 20, cy - 20, cx + 20, cy + 20, RGB(120, 255, 150));
                icon_draw_cube(cx, cy, on ? 34 : 28, 0.0f, i, c1_of(), c2_of());
            }
            font_draw(UI_X(590), y + 2, 2.0f, RGB(255, 240, 160), ALIGN_RIGHT, g_icon_names[g->save.icon % ICON_COUNT]);
        } else {
            int cur = r == 1 ? g->save.col1 : g->save.col2;
            for (int i = 0; i < PLAYER_COLOR_COUNT; i++) {
                float cx = UI_X(70 + i * 38), cy = y + 36;
                int on = cur == i;
                if (on) gfx_rect(cx - 17, cy - 17, cx + 17, cy + 17, COL_WHITE);
                gfx_rect(cx - 14, cy - 14, cx + 14, cy + 14, RGB(0, 0, 0));
                gfx_rect(cx - 12, cy - 12, cx + 12, cy + 12, g_player_colors[i]);
            }
        }
    }
    font_draw_fancy(SCREEN_W / 2, SCREEN_H - 24, 2.0f, COL_WHITE, RGB(220, 230, 255), RGB(0, 0, 0), 2.0f, ALIGN_CENTER,
                    GLYPH_LEFT GLYPH_RIGHT " CHANGE   " BTN_NAME_L "/" BTN_NAME_R " PREVIEW   " GLYPH_CIRCLE " BACK");
}

/* --- options -------------------------------------------------------- */

enum { OPT_MUSIC = 0, OPT_SFX, OPT_DELAY, OPT_ERASE, OPT_BACK, OPT_COUNT };

static void options_tick(void)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) {
        g->options_sel = (g->options_sel + OPT_COUNT - 1) % OPT_COUNT;
        audio_sfx(SFX_MENU_MOVE);
        g->erase_confirm = 0;
    }
    if (g->repeat & BTN_DOWN) {
        g->options_sel = (g->options_sel + 1) % OPT_COUNT;
        audio_sfx(SFX_MENU_MOVE);
        g->erase_confirm = 0;
    }
    /* the metronome plays while the audio delay is being set */
    int song = g->options_sel == OPT_DELAY ? SONG_METRONOME : SONG_MENU;
    if (audio_current_song() != song) audio_play_song(song, 0.0f);

    int d = 0;
    if (g->repeat & BTN_LEFT) d = -1;
    if (g->repeat & BTN_RIGHT) d = 1;
    if (d && (g->options_sel == OPT_MUSIC || g->options_sel == OPT_SFX)) {
        uint8_t *v = g->options_sel == OPT_MUSIC ? &g->save.music_vol : &g->save.sfx_vol;
        *v = (uint8_t)clampi(*v + d, 0, 10);
        audio_set_volume(g->save.music_vol, g->save.sfx_vol);
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
    }
    if (d && g->options_sel == OPT_DELAY) {
        g->save.audio_delay = (int8_t)clampi(g->save.audio_delay + d, -SAVE_AUDIO_DELAY_MAX, SAVE_AUDIO_DELAY_MAX);
        audio_set_user_delay(g->save.audio_delay * 0.01f);
        g->save_dirty = 1;
    }
    if (g->erase_confirm) {
        g->erase_t -= TICK_DT;
        if (g->erase_t <= 0.0f) g->erase_confirm = 0;
    }
    if (g->pressed & BTN_CROSS) {
        if (g->options_sel == OPT_ERASE) {
            if (!g->erase_confirm) {
                g->erase_confirm = 1;
                g->erase_t = 3.0f;
                audio_sfx(SFX_MENU_SELECT);
            } else {
                /* progress goes, settings stay */
                uint8_t mv = g->save.music_vol, sv = g->save.sfx_vol;
                int8_t delay = g->save.audio_delay;
                save_defaults(&g->save);
                g->save.music_vol = mv;
                g->save.sfx_vol = sv;
                g->save.audio_delay = delay;
                save_store(&g->save);
                g->erase_confirm = 0;
                audio_sfx(SFX_DEATH);
            }
        } else if (g->options_sel == OPT_BACK) {
            audio_sfx(SFX_MENU_BACK);
            screen_go(SCR_TITLE);
        }
    }
    if (g->pressed & BTN_CIRCLE) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

/* Help for the audio delay: four lights flash on the beats the game thinks
 * are being heard; the player lines them up with the metronome's kick. */
static void draw_delay_help(void)
{
    render_panel(UI_X(60), 268, UI_X(580), 428, RGBA(0, 0, 0, 190), RGBA(255, 255, 255, 60));
    font_draw(UI_X(80), 282, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "AUDIO DELAY");
    font_draw(UI_X(80), 304, 2.0f, COL_WHITE, ALIGN_LEFT, "LISTEN TO THE KICK, WATCH THE LIGHTS.");
    font_draw(UI_X(80), 322, 2.0f, COL_WHITE, ALIGN_LEFT, "KICK AFTER THE FLASH:  RAISE IT");
    font_draw(UI_X(80), 340, 2.0f, COL_WHITE, ALIGN_LEFT, "KICK BEFORE THE FLASH: LOWER IT");
    float b = audio_current_song() == SONG_METRONOME ? audio_song_beat() : -1.0f;
    int lit = b >= 0.0f ? (int)b % 4 : -1;
    float glow = b >= 0.0f ? expf(-(b - floorf(b)) * 6.0f) : 0.0f;
    for (int k = 0; k < 4; k++) {
        float cx = SCREEN_W / 2 + (k - 1.5f) * 70.0f, cy = 392;
        float r = k == 0 ? 17.0f : 13.0f; /* the bar's first beat is the big one */
        draw_circle(cx, cy, r + 3, RGBA(0, 0, 0, 200));
        Color off = RGBA(255, 255, 255, 40), on = k == 0 ? RGB(255, 230, 90) : RGB(90, 255, 140);
        draw_circle(cx, cy, r, k == lit ? col_lerp(off, on, glow) : off);
        if (k == lit && glow > 0.05f) draw_glow(cx, cy, r * 3.0f, col_with_alpha(on, 0.5f * glow));
    }
}

static void options_render(void)
{
    Game *g = &g_game;
    draw_menu_backdrop(&g_palettes[1], g->t * 1.5f, beat_pulse());
    font_draw_fancy(SCREEN_W / 2, 18, 4.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, "OPTIONS");

    const char *labels[OPT_COUNT] = {"MUSIC", "SOUND FX", "AUDIO DELAY",
                                     g->erase_confirm ? "PRESS " GLYPH_CROSS " AGAIN" : "ERASE PROGRESS", "BACK"};
    for (int i = 0; i < OPT_COUNT; i++) {
        float y = 66 + i * 40;
        int sel = g->options_sel == i;
        render_panel(UI_X(110), y - 7, UI_X(530), y + 29, RGBA(0, 0, 0, sel ? 210 : 170), sel ? RGB(120, 255, 150) : RGBA(255, 255, 255, 60));
        Color tc = i == OPT_ERASE && g->erase_confirm ? RGB(255, 120, 120) : (sel ? COL_WHITE : RGB(170, 180, 200));
        font_draw(UI_X(130), y + 2, 3.0f, tc, ALIGN_LEFT, labels[i]);
        if (i == OPT_MUSIC || i == OPT_SFX) {
            int v = i == OPT_MUSIC ? g->save.music_vol : g->save.sfx_vol;
            for (int k = 0; k < 10; k++) {
                float x = UI_X(330 + k * 18);
                gfx_rect(x, y, x + 14, y + 22, k < v ? RGB(90, 255, 120) : RGBA(255, 255, 255, 50));
            }
        } else if (i == OPT_DELAY) {
            char val[24];
            snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %+d MS " GLYPH_RIGHT : "%+d MS", g->save.audio_delay * 10);
            font_draw(UI_X(426), y + 6, 2.0f, tc, ALIGN_CENTER, val);
        }
    }
    if (g->options_sel == OPT_DELAY) {
        draw_delay_help();
        return;
    }

    /* stats */
    char buf[64];
    int done = 0, coins = 0;
    for (int i = 0; i < g_level_count && i < SAVE_MAX_LEVELS; i++) {
        if (g->save.best[i] >= 100) done++;
        for (int k = 0; k < 3; k++) coins += (g->save.coins[i] >> k) & 1;
    }
    render_panel(UI_X(60), 268, UI_X(580), 428, RGBA(0, 0, 0, 190), RGBA(255, 255, 255, 60));
    font_draw(UI_X(80), 282, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "CONTROLS");
    font_draw(UI_X(80), 304, 2.0f, COL_WHITE, ALIGN_LEFT, GLYPH_CROSS " / " GLYPH_CIRCLE " / UP / " BTN_NAME_L " / " BTN_NAME_R "  JUMP, HOLD TO FLY");
    font_draw(UI_X(80), 322, 2.0f, COL_WHITE, ALIGN_LEFT, "START  PAUSE");
    font_draw(UI_X(80), 340, 2.0f, COL_WHITE, ALIGN_LEFT, "PRACTICE: " GLYPH_SQUARE " CHECKPOINT  " GLYPH_TRIANGLE " REMOVE");
    font_draw(UI_X(80), 368, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "STATS");
    snprintf(buf, sizeof(buf), "ATTEMPTS %u  JUMPS %u", (unsigned)g->save.total_attempts, (unsigned)g->save.total_jumps);
    font_draw(UI_X(80), 388, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    snprintf(buf, sizeof(buf), "LEVELS %d/%d  COINS %d", done, g_level_count, coins);
    font_draw(UI_X(80), 406, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
}

void menus_tick(void)
{
    switch (g_game.screen) {
    case SCR_TITLE: title_tick(); break;
    case SCR_SELECT: select_tick(); break;
    case SCR_GARAGE: garage_tick(); break;
    case SCR_OPTIONS: options_tick(); break;
    }
}

void menus_render(void)
{
    switch (g_game.screen) {
    case SCR_TITLE: title_render(); break;
    case SCR_SELECT: select_render(); break;
    case SCR_GARAGE: garage_render(); break;
    case SCR_OPTIONS: options_render(); break;
    }
    fx_draw(FX_SCREEN, 0, 0, (1.0f - g_game.alpha) * TICK_DT);
}
