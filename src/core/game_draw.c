/* The vector family's drawing of the frame and the menu screens (game.c
 * runs them). */
#include <stdio.h>

#include "game_internal.h"
#include "audio.h"
#include "draw.h"
#include "font.h"
#include "fx.h"
#include "icons.h"
#include "version.h"

void game_draw_init(void)
{
    draw_init();
    font_init();
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
    view_snap(&v);
    render_background(&v);
    render_ground(&v, 0.0f, CORRIDOR_H, 0.0f);
}

static Color c1_of(void) { return g_player_colors[g_game.save.col1 % PLAYER_COLOR_COUNT]; }
static Color c2_of(void) { return g_player_colors[g_game.save.col2 % PLAYER_COLOR_COUNT]; }

/* --- title ---------------------------------------------------------- */

void draw_button(float cx, float cy, float size, int selected, Color c, int kind)
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
    case -1: break;
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
    demo_render(&g->demo, &pal);

    /* Vertical layout, with room for the title's bob (-4..+4 px) and the
     * selected button's pulse (up to 1.12x): title 26-105, subtitle to 125,
     * buttons 135-255, labels 264-282; the demo run plays below. The bob
     * is a slow sine, not on the beat: text is drawn on whole pixels
     * (draw.h), so a jump on each beat came as a drop of 2 or 3 pixels in
     * one frame, a twitch every beat (on a TV's 448 lines, a stutter). */
    float bob = sinf(g->t * 2.0f) * 4.0f;
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

void draw_diff_badge(float cx, float cy, int d, float r)
{
    Color c = difficulty_color(d);
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

/* A level's name, difficulty, palette and coins for the level select, read
 * once: level_info reads them from the level's source (all of it, for the
 * coins), too slow for every card in every frame. */
static void select_info(int idx, LevelInfo *out)
{
    static LevelInfo info[SAVE_MAX_LEVELS];
    static uint8_t known[SAVE_MAX_LEVELS];
    if (idx >= SAVE_MAX_LEVELS) {
        level_info(idx, out);
        return;
    }
    if (!known[idx]) {
        level_info(idx, &info[idx]);
        known[idx] = 1;
    }
    *out = info[idx];
}

#define CARD_HALF_W 230

static void draw_level_card(int idx, float cx)
{
    Game *g = &g_game;
    LevelInfo info;
    select_info(idx, &info);
    float x0 = cx - CARD_HALF_W, x1 = cx + CARD_HALF_W;
    render_panel(x0, 74, x1, 300, RGBA(0, 0, 0, 150), RGBA(255, 255, 255, 140));
    draw_diff_badge(x0 + 62, 140, info.difficulty, 38);
    font_draw(x0 + 62, 188, 2.0f, difficulty_color(info.difficulty), ALIGN_CENTER, difficulty_name(info.difficulty));

    char buf[48];
    float tw = font_width(info.name, 4.0f);
    float scale = tw > 300 ? 3.0f : 4.0f;
    font_draw_fancy(x0 + 300, 104, scale, COL_WHITE, RGB(210, 225, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, info.name);
    snprintf(buf, sizeof(buf), "%d " GLYPH_STAR, info.stars);
    font_draw_fancy(x0 + 300, 148, 3.0f, RGB(255, 245, 160), RGB(255, 190, 40), RGB(0, 0, 0), 2.0f, ALIGN_CENTER, buf);

    int s = idx < SAVE_MAX_LEVELS ? idx : 0;
    uint8_t coins = g->save.progress.coins[s];
    for (int i = 0; i < info.ncoins; i++) {
        float ccx = x0 + 300 + (i - (info.ncoins - 1) * 0.5f) * 40;
        if ((coins >> i) & 1) render_coin(ccx, 196, 13, 0.0f, 1.0f, 0);
        else {
            draw_circle(ccx, 196, 14, RGBA(0, 0, 0, 160));
            draw_ring(ccx, 196, 11, 14, RGBA(255, 255, 255, 60));
        }
    }

    float ty = font_center_y(232, 244, 2.0f); /* text beside the bars */
    font_draw(x0 + 30, ty, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "NORMAL");
    render_progress_bar(x0 + 140, 232, x1 - 80, 244, g->save.progress.best[s] / 100.0f, RGB(90, 255, 120), RGB(210, 255, 210));
    snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best[s]);
    font_draw(x1 - 66, ty, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    ty = font_center_y(262, 274, 2.0f);
    font_draw(x0 + 30, ty, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "PRACTICE");
    render_progress_bar(x0 + 140, 262, x1 - 80, 274, g->save.progress.best_practice[s] / 100.0f, RGB(80, 200, 255),
                        RGB(210, 240, 255));
    snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best_practice[s]);
    font_draw(x1 - 66, ty, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    if (g->save.progress.best[s] >= 100) {
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
    select_info(a, &ia);
    select_info(b, &ib);
    Palette pal;
    palette_lerp(&pal, &g_palettes[clampi(ia.pal, 0, PALETTE_COUNT - 1)],
                 &g_palettes[clampi(ib.pal, 0, PALETTE_COUNT - 1)], f);
    draw_menu_backdrop(&pal, g->t * 2.0f + sc * 12.0f, beat_pulse());

    font_draw_fancy(SCREEN_W / 2, 24, 3.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER,
                    "SELECT LEVEL");
    /* the cards either side of the scroll position (a screen apart), each
     * drawn once and only while some of it is on screen: at rest, one of
     * them is a whole screen away (everything a card draws is inside its
     * panel) */
    for (int i = (int)floorf(sc); i <= (int)floorf(sc) + 1; i++) {
        float cx = SCREEN_W / 2 + (i - sc) * SCREEN_W;
        if (cx + CARD_HALF_W <= 0.0f || cx - CARD_HALF_W >= SCREEN_W) continue;
        draw_level_card(((i % n) + n) % n, cx);
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

static void garage_render(void)
{
    Game *g = &g_game;
    draw_menu_backdrop(&g_palettes[7], g->t * 1.5f, beat_pulse());
    font_draw_fancy(SCREEN_W / 2, 20, 4.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, "GARAGE");

    /* the icon and colours on every vehicle */
    render_panel(UI_X(60), 60, UI_X(580), 170, RGBA(0, 0, 0, 180), RGBA(255, 255, 255, 90));
    static const char *mnames[MODE_COUNT] = {"CUBE", "SHIP", "BALL", "UFO", "WAVE"};
    for (int m = 0; m < MODE_COUNT; m++) {
        float cx = UI_X(112 + m * 104);
        float ang = m == MODE_BALL ? g->t * 3.0f : (m == MODE_WAVE ? -0.6f : 0.0f);
        icon_draw_mode(m, cx, 108, 44.0f, ang, 0, g->save.icon, c1_of(), c2_of());
        font_draw(cx, 148, 2.0f, RGB(200, 210, 230), ALIGN_CENTER, mnames[m]);
    }

    /* icon row */
    const char *rows[3] = {"ICON", "COLOR 1", "COLOR 2"};
    for (int r = 0; r < 3; r++) {
        float y = 200 + r * 72;
        int sel = g->garage_row == r;
        render_panel(UI_X(40), y - 6, UI_X(600), y + 58, RGBA(0, 0, 0, sel ? 210 : 170), sel ? RGB(120, 255, 150) : RGBA(255, 255, 255, 60));
        font_draw(UI_X(56), y + 2, 2.0f, sel ? COL_WHITE : RGB(160, 170, 190), ALIGN_LEFT, rows[r]);
        if (r == 0) {
            /* (the chosen one framed as the colours are, clear of the
             * panel's edge) */
            for (int i = 0; i < ICON_COUNT; i++) {
                float cx = UI_X(80 + i * 66), cy = y + 36;
                if (g->save.icon == i) gfx_rect(cx - 17, cy - 17, cx + 17, cy + 17, RGB(120, 255, 150));
                icon_draw_cube(cx, cy, 28, 0.0f, i, c1_of(), c2_of());
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
                    GLYPH_LEFT GLYPH_RIGHT " CHANGE   " GLYPH_CIRCLE " BACK");
}

/* --- options -------------------------------------------------------- */

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

#if AUDIO_OUTPUT_OPTION
/* What OUTPUT does (instead of the stats while it is chosen). */
static void draw_output_help(void)
{
    render_panel(UI_X(60), 268, UI_X(580), 428, RGBA(0, 0, 0, 190), RGBA(255, 255, 255, 60));
    font_draw(UI_X(80), 282, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "OUTPUT");
    font_draw(UI_X(80), 304, 2.0f, COL_WHITE, ALIGN_LEFT, "SPEAKERS: MIXED FOR THE " TARGET_NAME "'S OWN SPEAKERS,");
    font_draw(UI_X(80), 322, 2.0f, COL_WHITE, ALIGN_LEFT, "THE BASS THEY CAN'T PLAY CUT, LOUDER.");
    font_draw(UI_X(80), 350, 2.0f, COL_WHITE, ALIGN_LEFT, "HEADPHONES: THE FULL MIX.");
#if AUDIO_OUTPUT_AUTO
    font_draw(UI_X(80), 378, 2.0f, COL_WHITE, ALIGN_LEFT, "AUTO: SPEAKERS, OR HEADPHONES WHEN");
    font_draw(UI_X(80), 396, 2.0f, COL_WHITE, ALIGN_LEFT, "THEY ARE PLUGGED IN.");
#endif
}
#endif

#if FLICKER_OPTION
/* What FLICKER FILTER does (instead of the stats while it is chosen). */
static void draw_flicker_help(void)
{
    render_panel(UI_X(60), 268, UI_X(580), 428, RGBA(0, 0, 0, 190), RGBA(255, 255, 255, 60));
    font_draw(UI_X(80), 282, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "FLICKER FILTER");
    font_draw(UI_X(80), 304, 2.0f, COL_WHITE, ALIGN_LEFT, "ON: EACH LINE OF THE PICTURE BLENDED");
    font_draw(UI_X(80), 322, 2.0f, COL_WHITE, ALIGN_LEFT, "WITH THE NEXT: CALMER EDGES AND THIN");
    font_draw(UI_X(80), 340, 2.0f, COL_WHITE, ALIGN_LEFT, "LINES ON A TUBE TV, A LITTLE SOFTER.");
    font_draw(UI_X(80), 368, 2.0f, COL_WHITE, ALIGN_LEFT, "OFF: THE SHARPEST PICTURE, FOR TVS AND");
    font_draw(UI_X(80), 386, 2.0f, COL_WHITE, ALIGN_LEFT, "EMULATORS THAT DEINTERLACE IT.");
}
#endif

/* the rows: 40 apart, 34 with a sixth (OUTPUT's, FLICKER FILTER's; above
 * the panel at 268) */
#define OPT_PITCH (OPT_COUNT > 5 ? 34 : 40)

static void options_render(void)
{
    Game *g = &g_game;
    draw_menu_backdrop(&g_palettes[1], g->t * 1.5f, beat_pulse());
    font_draw_fancy(SCREEN_W / 2, 18, 4.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, "OPTIONS");

    const char *labels[OPT_COUNT];
    labels[OPT_MUSIC] = "MUSIC";
    labels[OPT_SFX] = "SOUND FX";
#if AUDIO_OUTPUT_OPTION
    labels[OPT_OUTPUT] = "OUTPUT";
#endif
    labels[OPT_DELAY] = "AUDIO DELAY";
#if FLICKER_OPTION
    labels[OPT_FLICKER] = "FLICKER FILTER";
#endif
    labels[OPT_ERASE] = g->erase_confirm ? "PRESS " GLYPH_CROSS " AGAIN" : "ERASE PROGRESS";
    labels[OPT_BACK] = "BACK";
    for (int i = 0; i < OPT_COUNT; i++) {
        float top = 59 + i * OPT_PITCH, bot = top + OPT_PITCH - 4, y = top + (OPT_PITCH - 26) / 2;
        int sel = g->options_sel == i;
        render_panel(UI_X(110), top, UI_X(530), bot, RGBA(0, 0, 0, sel ? 210 : 170), sel ? RGB(120, 255, 150) : RGBA(255, 255, 255, 60));
        Color tc = i == OPT_ERASE && g->erase_confirm ? RGB(255, 120, 120) : (sel ? COL_WHITE : RGB(170, 180, 200));
        font_draw(UI_X(130), font_center_y(top, bot, 3.0f), 3.0f, tc, ALIGN_LEFT, labels[i]);
        if (i == OPT_MUSIC || i == OPT_SFX) {
            int v = i == OPT_MUSIC ? g->save.music_vol : g->save.sfx_vol;
            for (int k = 0; k < 10; k++) {
                float x = UI_X(330 + k * 18);
                gfx_rect(x, y, x + 14, y + 22, k < v ? RGB(90, 255, 120) : RGBA(255, 255, 255, 50));
            }
        } else if (i == OPT_DELAY) {
            char val[24];
            snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %+d MS " GLYPH_RIGHT : "%+d MS", g->save.audio_delay * 10);
            font_draw(UI_X(426), font_center_y(top, bot, 2.0f), 2.0f, tc, ALIGN_CENTER, val);
        }
#if AUDIO_OUTPUT_OPTION
        else if (i == OPT_OUTPUT) {
            const char *v = g->save.speaker == OUTPUT_AUTO ? "AUTO" : g->save.speaker == OUTPUT_SPEAKER ? "SPEAKERS" : "HEADPHONES";
            char val[24];
            snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %s " GLYPH_RIGHT : "%s", v);
            font_draw(UI_X(426), font_center_y(top, bot, 2.0f), 2.0f, tc, ALIGN_CENTER, val);
        }
#endif
#if FLICKER_OPTION
        else if (i == OPT_FLICKER) {
            const char *v = g->save.flicker ? "ON" : "OFF";
            char val[24];
            snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %s " GLYPH_RIGHT : "%s", v);
            font_draw(UI_X(440), font_center_y(top, bot, 2.0f), 2.0f, tc, ALIGN_CENTER, val);
        }
#endif
    }
    if (g->options_sel == OPT_DELAY) {
        draw_delay_help();
        return;
    }
#if AUDIO_OUTPUT_OPTION
    if (g->options_sel == OPT_OUTPUT) {
        draw_output_help();
        return;
    }
#endif
#if FLICKER_OPTION
    if (g->options_sel == OPT_FLICKER) {
        draw_flicker_help();
        return;
    }
#endif

    /* stats */
    char buf[64];
    int done = 0, coins = 0;
    for (int i = 0; i < g_level_count && i < SAVE_MAX_LEVELS; i++) {
        if (g->save.progress.best[i] >= 100) done++;
        for (int k = 0; k < 3; k++) coins += (g->save.progress.coins[i] >> k) & 1;
    }
    render_panel(UI_X(60), 268, UI_X(580), 428, RGBA(0, 0, 0, 190), RGBA(255, 255, 255, 60));
    font_draw(UI_X(80), 282, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "CONTROLS");
    font_draw(UI_X(80), 304, 2.0f, COL_WHITE, ALIGN_LEFT, GLYPH_CROSS " / " GLYPH_CIRCLE " / UP / " BTN_NAME_L " / " BTN_NAME_R "  JUMP, HOLD TO FLY");
    font_draw(UI_X(80), 322, 2.0f, COL_WHITE, ALIGN_LEFT, "START  PAUSE");
    font_draw(UI_X(80), 340, 2.0f, COL_WHITE, ALIGN_LEFT, "PRACTICE: " GLYPH_SQUARE " CHECKPOINT  " GLYPH_TRIANGLE " REMOVE");
    font_draw(UI_X(80), 368, 2.0f, RGB(255, 240, 160), ALIGN_LEFT, "STATS");
    snprintf(buf, sizeof(buf), "ATTEMPTS %u  JUMPS %u", (unsigned)g->save.progress.total_attempts, (unsigned)g->save.progress.total_jumps);
    font_draw(UI_X(80), 388, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    snprintf(buf, sizeof(buf), "LEVELS %d/%d  COINS %d", done, g_level_count, coins);
    font_draw(UI_X(80), 406, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
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
