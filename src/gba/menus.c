/*
 * The menus on the GBA: the core's title, level select, garage and options
 * screens (game.c runs them), drawn as game_draw.c lays them out on a
 * 640x448 screen, scaled to the GBA's 240x160 (x * 3/8, y * 5/14).
 */
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "menus.h"
#include "sprites.h"
#include "ui.h"
#include "video.h"
#include "frame.h"
#include "world.h"
#include "gen.h"
#include "version.h"
#include "save.h"
#include "font.h"
#include "audio.h"

/* a 640x448 layout position on the GBA's screen */
#define MX(x) ((int)((x) * 3) / 8)
#define MY(y) ((int)((y) * 5) / 14)

static int s_drawn_sel = -1;
static int s_title_part;

/* a menu draws a part of a screen while its frame has this many lines left
 * (frame.h): for the part, about 40, and what the frame does after (the
 * world behind, the fade, the packing of what was drawn: about 75) */
#define MENU_PART_LINES 150
/* a row of the garage or the options drawn again, while the frame has
 * this many lines left: the row, 40 to 65 lines (the most on a surface
 * just emptied, as a screen opens), and what the frame does after it, as
 * for a part (a move changes two rows, which then change together) */
#define MENU_ROW_LINES 140
/* the title's texts (its buttons' labels, its hint: outlined, about 65
 * lines each) are drawn after the run behind it (45 lines, 65 as the
 * title opens, its camera jumping to where the music is) and the run's
 * sprites, while the frame has this many lines left: then only the
 * packing and the fade's colours (about 15) come after them */
#define TITLE_TEXT_LINES 95

/* ------------------------------------------------------------------ */
/* Sets of sprites a screen loads                                      */
/* ------------------------------------------------------------------ */

static void set_load(const uint32_t *tiles, int from, int n, int to)
{
    /* tiles from..from+n of a set (counted from OBJ_TEXT_TILE) to VRAM tile
     * `to`, in the vertical blank (what shows them is in the same frame) */
    video_queue(OBJ_TILES + to * 8, tiles + (from - OBJ_TEXT_TILE) * 8, (uint32_t)n * 8);
}

/* ------------------------------------------------------------------ */
/* Title                                                               */
/* ------------------------------------------------------------------ */

/* the title set in VRAM: the logo and the line under it, then a slot per
 * button (holding its look chosen or not) and the options button's saw */
#define T_LOGO OBJ_TEXT_TILE
#define T_SUB (OBJ_TEXT_TILE + 96)
#define T_GARAGE (OBJ_TEXT_TILE + 120)
#define T_PLAY (OBJ_TEXT_TILE + 136)
#define T_OPTIONS (OBJ_TEXT_TILE + 200)
#define T_SAW (OBJ_TEXT_TILE + 216)

static int s_btn_loaded[3];
static int s_cog_frame; /* (the frame in T_SAW: title_buttons) */

static void title_enter(void)
{
    int i;
    set_load(g_set_title, ST_LOGO, 96, T_LOGO);
    set_load(g_set_title, ST_SUBTITLE, 24, T_SUB);
    set_load(g_set_title, ST_BTN_SAW, 4, T_SAW);
    memcpy(g_pal_obj + SET_title_BANK * 16, g_set_title_pal, SET_title_BANKS * 32);
    for (i = 0; i < 3; i++) s_btn_loaded[i] = -1;
    s_cog_frame = 0; /* (set_load put frame 0 there) */
    s_drawn_sel = -1;
    s_title_part = 0; /* (the texts in the next frames: title_draw) */
}

/*
 * The title's texts that stay put, a part at a time: the hint, centred,
 * and in the corner the version, on one line in the ground under the
 * demo's run, as on the PC. Above the ground's line the hint would cover
 * the run; under it only the player's glow reaches, see-through, a few
 * pixels into the hint's first letter. It is outlined: at its faintest
 * its pulse is close to the ground's colour.
 */
#define TITLE_PARTS 2
static const char s_title_hint[] = GLYPH_LEFT GLYPH_RIGHT " SELECT   A OK";

/*
 * The version, shortened to the n letters the line has left: git
 * describe's v1.2.0 as it is, a build past a tag (v1.2.0-1-g354117c) by
 * its commit (354117c), as the Game Boy Color's title does, and changed
 * files (-dirty) as a * after it. A tag too long even then loses its v,
 * then its end and any *, marked by a +.
 */
static void title_version(char *out, int n)
{
    const char *v = g_version, *p, *hash = NULL;
    int len = (int)strlen(v), dirty = len >= 6 && !strcmp(v + len - 6, "-dirty"), k;
    if (dirty) len -= 6;
    /* the commit after the last "-g", if only hex digits follow it */
    for (p = v; (p = strstr(p, "-g")) != NULL && p < v + len; p += 2) {
        for (k = 2; p + k < v + len && isxdigit((unsigned char)p[k]); k++)
            ;
        if (k > 2 && p + k == v + len) hash = p + 2;
    }
    if (len + dirty > n && hash) {
        len -= (int)(hash - v);
        v = hash;
    }
    if (len + dirty > n && (*v == 'v' || *v == 'V')) {
        v++;
        len--;
    }
    if (len + dirty > n) {
        len = maxi(n - 1, 0);
        dirty = 0;
        memcpy(out, v, (size_t)len);
        out[len++] = '+';
    } else {
        memcpy(out, v, (size_t)len);
    }
    if (dirty) out[len++] = '*';
    out[len] = 0;
}

static void title_part(int part)
{
    if (part == 0) {
        TextStyle st = ui_style(UB_HINT, 0, UI_TEXT);
        st.outline = UI(UB_HINT, UI_K);
        ui_text_st(SCR_W / 2, SCR_H - 8, 1, UI_CENTER, &st, s_title_hint);
    } else {
        /* right-aligned, from a letter's width past the hint's outline (a
         * pixel past its last letter) and never in a tile of the hint's (a
         * tile shows one palette bank); n letters are 6n - 1 pixels wide */
        int w = ui_text_w(s_title_hint, 1), end = SCR_W / 2 - w / 2 + w;
        int from = maxi(end + 7, (end / 8 + 1) * 8);
        char ver[SCR_W / 6 + 1];
        title_version(ver, mini((SCR_W - from) / 6, SCR_W / 6));
        ui_text(SCR_W - 2, SCR_H - 8, 1, UI_RIGHT, UB_GRAY, 0, UI_DIM, ver);
    }
}

static void title_buttons(void)
{
    static const int rom[3][2] = {{ST_BTN_GARAGE, ST_BTN_GARAGE_SEL}, {ST_BTN_PLAY, ST_BTN_PLAY_SEL},
                                  {ST_BTN_OPTIONS, ST_BTN_OPTIONS_SEL}};
    static const int slot[3] = {T_GARAGE, T_PLAY, T_OPTIONS};
    const Game *g = &g_game;
    int i;
    for (i = 0; i < 3; i++) {
        /* draw_button: the chosen one 1.08 times as big (its own picture:
         * not pulsing, which scaled by the hardware would make its edges
         * waver a pixel) */
        int sel = g->menu_sel == i, n = i == 1 ? 64 : 16;
        int cx = 120 + (i - 1) * 64, cy = MY(195);
        if (s_btn_loaded[i] != sel) {
            set_load(g_set_title, rom[i][sel], n, slot[i]);
            s_btn_loaded[i] = sel;
        }
        if (i == 0) {
            /* the garage button shows the player's cube, as it is */
            spr(cx - 8, cy - 8, SQ16, OBJ_PLAYER_TILE + PT_CUBE_STILL, OBJ_PAL_PLAYER, 0, 0);
        }
        if (i == 2) {
            /* the options button's cog, turning (its frames: art.h) */
            int f = spr_frame_turn(g->t * 1.5f, PI / 6.0f, COG_FRAMES);
            if (f != s_cog_frame && video_queue(OBJ_TILES + T_SAW * 8, g_cog_frames + f * 4 * 8, 4 * 8)) s_cog_frame = f;
            spr(cx - 8, cy - 8, SQ16, T_SAW, OBJ_PAL_PORTAL + 2, 0, 0);
        }
        if (i == 1) spr(cx - 32, cy - 32, SQ64, slot[i], OBJ_PAL_PORTAL + 1, 0, 0);
        else spr(cx - 16, cy - 16, SQ32, slot[i], OBJ_PAL_PORTAL + i, 0, 0);
    }
}

static void title_text(void)
{
    static const char *labels[3] = {"GARAGE", "PLAY", "OPTIONS"};
    const Game *g = &g_game;
    int i;
    if (s_drawn_sel == g->menu_sel || frame_lines_left() <= TITLE_TEXT_LINES) return;
    s_drawn_sel = g->menu_sel;
    ui_erase(0, MY(258), SCR_W, 12);
    for (i = 0; i < 3; i++) {
        int sel = g->menu_sel == i;
        ui_text(120 + (i - 1) * 64, MY(266), 1, UI_CENTER, sel ? UB_SEL : UB_GRAY, 1, 0, labels[i]);
    }
}

/*
 * draw_title's bob, in whole pixels: up 2 and down 2 a pixel at a time,
 * a step every eighth of the PC's sine (its period, pi seconds, here 0.39
 * seconds a step). A sprite moves only by whole pixels, so the PC's
 * sine, its 4 pixels here 1.4, came in steps of uneven length: a
 * stutter. Steps of even length read as a bob.
 */
static int title_bob(void)
{
    static const int8_t step[8] = {0, 1, 2, 1, 0, -1, -2, -1};
    return step[(int)(g_game.t * (8.0f / PI)) & 7];
}

static void title_draw(void)
{
    /* the logo and the line under it bob */
    int bob = title_bob();
    int k;
    spr_prio(0);
    for (k = 0; k < 3; k++) spr(24 + k * 64, MY(30) - 5 + bob, W64x32, T_LOGO + k * 32, OBJ_PAL_TEXT, 0, 0);
    for (k = 0; k < 6; k++) spr(24 + k * 32, MY(104) + bob, W32x8, T_SUB + k * 4, OBJ_PAL_TEXT, 0, 0);
    title_buttons();
    spr_prio(1);
}

/* the title's texts, after the run behind it (TITLE_TEXT_LINES) */
static void title_draw_text(void)
{
    const Game *g = &g_game;
    title_text();
    while (s_title_part < TITLE_PARTS && frame_lines_left() > TITLE_TEXT_LINES) title_part(s_title_part++);
    ui_hint_level((int)((0.7f + 0.3f * tsinf(g->t * 4.0f)) * 16.0f), world_ground());
}


/* ------------------------------------------------------------------ */
/* Level select                                                        */
/* ------------------------------------------------------------------ */

/*
 * The cards are drawn into BG0, the level in view and the one it slides
 * towards: the card at position p (sel_scroll's integer steps, not
 * wrapped) in the half p & 1 of BG0's 512 pixels, centred at 120 there,
 * and BG0 scrolled to slide them, 256 pixels from one to the next (the
 * PC's 640: the screen's width). So a card is drawn once as it comes into
 * view, not every frame; what stays put (the title, the dots, the hint)
 * is in sprites.
 */
#define S_FACE OBJ_TEXT_TILE /* the six faces, 16 tiles each */
#define S_ARROW_L (OBJ_TEXT_TILE + 96)
#define S_ARROW_R (OBJ_TEXT_TILE + 100)
#define S_SLOT (OBJ_TEXT_TILE + 104)
#define S_DOTS (OBJ_TEXT_TILE + SET_select_TILES) /* the page dots, big and small */
#define S_TITLE (S_DOTS + 2)
#define S_HINT (S_TITLE + 24)
#define CARD_STEP 256

static LevelInfo s_info[PROGRESS_LEVELS];
static int s_page[2]; /* the level drawn in each half of BG0, or -1 */
static int s_part[2]; /* how much of it: card_page */
static ObjText s_title_t, s_hint_t;
static uint32_t s_dot_tiles[2][8] ALIGN4;

static void select_enter(void)
{
    int i, y;
    set_load(g_set_select, ST_FACE, SET_select_TILES, OBJ_TEXT_TILE);
    memcpy(g_pal_obj + SET_select_BANK * 16, g_set_select_pal, SET_select_BANKS * 32);
    /* text in sprites in the white text's colours */
    memcpy(g_pal_obj + OBJ_PAL_TEXT * 16, g_pal_bg + UB_WHITE * 16, 32);
    s_page[0] = s_page[1] = -1;
    /* (the texts in sprites in the next frames: select_draw) */
    s_title_t.pieces = s_hint_t.pieces = 0;
    /* the dots: the chosen level's 5 pixels across, white; the others' 3, gray */
    memset(s_dot_tiles, 0, sizeof(s_dot_tiles));
    for (y = 0; y < 5; y++) {
        int w = y == 0 || y == 4 ? 3 : 5;
        for (i = 0; i < w; i++) s_dot_tiles[0][y + 2] |= (uint32_t)UI_X1 << ((4 - w / 2 + i) * 4);
    }
    for (y = 0; y < 3; y++) {
        int w = y == 1 ? 3 : 1;
        for (i = 0; i < w; i++) s_dot_tiles[1][y + 3] |= (uint32_t)UI_X3 << ((4 - w / 2 + i) * 4);
    }
    video_queue(OBJ_TILES + S_DOTS * 8, s_dot_tiles, 16);
}

/* draw_level_card, the card's centre at cx, in CARD_PARTS parts: its
 * panel; its name and stars; its difficulty and best; its practice best */
#define CARD_PARTS 4
static void card_text(int idx, int cx, int part)
{
    const Game *g = &g_game;
    const LevelInfo *in = &s_info[idx];
    int x0 = cx - MX(230), x1 = cx + MX(230), y0 = MY(74), y1 = MY(300);
    int s = idx < SAVE_MAX_LEVELS ? idx : 0;
    char buf[16];
    if (part == 0) {
        ui_panel(x0, y0, x1, y1, 0, UI(UB_WHITE, UI_EDGE));
        return;
    }
    if (part == 1) {
        /* the name (the PC: scale 4, 3 when long; the GBA's letters are 7
         * or 14 pixels high, and 14 would make a short name twice as big
         * as the others: all are 7, the PC's 3 at this screen's size), in
         * a row of tiles of its own: COMPLETE (green) is in the row above,
         * the stars (gold) in the one below */
        ui_text(x0 + MX(300), MY(104) + 4, 1, UI_CENTER, UB_WHITE, 1, 0, in->name);
        snprintf(buf, sizeof(buf), "%d \x05", in->stars);
        ui_text(x0 + MX(300), MY(148), 1, UI_CENTER, UB_GOLD, 1, 0, buf);
        return;
    }
    if (part == 2) {
        ui_text(x0 + MX(62), MY(188), 1, UI_CENTER, UB_DIFF, 0, (uint8_t)(UI_G0 + clampi(in->difficulty, 0, 5)),
                difficulty_name(in->difficulty));
        /* the bests beside their bars (the GBA's letters are wider for the
         * card than the PC's: the labels and figures closer to the edges) */
        ui_text(x0 + 6, MY(238) - 3, 1, UI_LEFT, UB_WHITE, 0, 0, "NORMAL");
        ui_bar(x0 + 57, MY(232), x1 - 30, MY(244), g->save.progress.best[s] * 256 / 100, UB_BAR);
        snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best[s]);
        ui_text(x1 - 4, MY(238) - 3, 1, UI_RIGHT, UB_WHITE, 0, UI_X1, buf);
        return;
    }
    ui_text(x0 + 6, MY(268) - 3, 1, UI_LEFT, UB_WHITE, 0, 0, "PRACTICE");
    ui_bar(x0 + 57, MY(262), x1 - 30, MY(274), g->save.progress.best_practice[s] * 256 / 100, UB_BAR + 16);
    snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best_practice[s]);
    ui_text(x1 - 4, MY(268) - 3, 1, UI_RIGHT, UB_WHITE, 0, UI_X1, buf);
    if (g->save.progress.best[s] >= 100) ui_text(x1 - 4, y0 + 3, 1, UI_RIGHT, UB_GREEN, 1, 0, "COMPLETE");
}

/* the card at position p in its half of BG0, a part of it (its panel
 * covers the one drawn there before); returns 1 if it drew one. A card
 * coming into view is out of sight in the slide's first frame. */
static int card_page(int p)
{
    int n = g_level_count, page = p & 1, idx = ((p % n) + n) % n;
    if (s_page[page] != idx) {
        s_page[page] = idx;
        s_part[page] = 0;
    }
    if (s_part[page] >= CARD_PARTS) return 0;
    card_text(idx, page * CARD_STEP + 120, s_part[page]++);
    return 1;
}

static void card_sprites(int idx, int cx)
{
    const Game *g = &g_game;
    const LevelInfo *in = &s_info[idx];
    int x0 = cx - MX(230), i;
    uint8_t coins = g->save.progress.coins[idx < SAVE_MAX_LEVELS ? idx : 0];
    int d = clampi(in->difficulty, 0, 5);
    /* the dimming behind it, asked for every frame */
    ui_dim(x0, MY(74), cx + MX(230), MY(300), 9); /* RGBA(0, 0, 0, 150) */
    spr(x0 + MX(62) - 16, MY(140) - 16, SQ32, S_FACE + d * 16, OBJ_PAL_PORTAL + d, 0, 0);
    for (i = 0; i < in->ncoins; i++) {
        int x = x0 + MX(300) + (i * 2 - (in->ncoins - 1)) * MX(40) / 2;
        if ((coins >> i) & 1) spr(x - 8, MY(196) - 8, SQ16, OT_COIN, OBJ_PAL_SPEED, 0, 0);
        else spr(x - 8, MY(196) - 8, SQ16, S_SLOT, OBJ_PAL_PORTAL + 6, 0, 0);
    }
}

static void select_draw(void)
{
    const Game *g = &g_game;
    int n = g_level_count, i;
    float sc = g->sel_scroll;
    int base = (int)floorf(sc);
    int off = (int)floorf((sc - (float)base) * CARD_STEP + 0.5f);
    int ap = (int)floorf(6.0f * tsinf(g->t * 5.0f) * 0.375f + 0.5f);
    int a = ((base % n) + n) % n, b = (a + 1) % n;
    Palette pal;
    palette_lerp(&pal, &g_palettes[clampi(s_info[a].pal, 0, PALETTE_COUNT - 1)],
                 &g_palettes[clampi(s_info[b].pal, 0, PALETTE_COUNT - 1)], sc - floorf(sc));
    /* select_render's cards: the one in view and, while it slides, the
     * next; when still, the one to the right is drawn ahead; and after the
     * screen opens, the texts that stay put: a part at a time, as many as
     * the frame has time for (the world, the fade and the packing of what
     * was drawn come after this) */
    {
        int drew = 1;
        while (drew && frame_lines_left() > MENU_PART_LINES) {
            drew = card_page(base);
            if (drew && off == 0) continue;
            if (frame_lines_left() <= MENU_PART_LINES) break;
            if (!s_title_t.pieces) {
                TextStyle st = ui_style(UB_WHITE, 1, 0);
                ui_obj_text(&s_title_t, S_TITLE, 1, &st, "SELECT LEVEL");
                drew = 1;
            } else if (!s_hint_t.pieces) {
                TextStyle st = ui_style(UB_WHITE, 1, 0);
                ui_obj_text(&s_hint_t, S_HINT, 1, &st, "A PLAY  SELECT PRACTICE  B BACK");
                drew = 1;
            } else {
                drew |= card_page(base + 1);
            }
        }
    }
    ui_scroll((base & 1) * CARD_STEP + off);
    spr_prio(0);
    card_sprites(a, 120 - off);
    if (off > 0) card_sprites(b, 120 + CARD_STEP - off);
    spr(MX(30) - 8 - ap, MY(187) - 8, SQ16, S_ARROW_L, OBJ_PAL_PORTAL + 6, 0, 0);
    spr(MX(610) - 8 + ap, MY(187) - 8, SQ16, S_ARROW_R, OBJ_PAL_PORTAL + 6, 0, 0);
    ui_obj_text_show(&s_title_t, 120, MY(24), OBJ_PAL_TEXT, 256);
    ui_obj_text_show(&s_hint_t, 120, MY(336), OBJ_PAL_TEXT, 256);
    for (i = 0; i < n; i++)
        spr(120 - (n - 1) * 7 / 2 + i * 7 - 4, MY(318) - 4, SQ8, S_DOTS + (i != g->sel_level), OBJ_PAL_TEXT, 0, 0);
    spr_prio(1);
    draw_menu_world(&pal, g->t * 2.0f + sc * 12.0f);
}

/* ------------------------------------------------------------------ */
/* Garage                                                              */
/* ------------------------------------------------------------------ */

/*
 * garage_render's layout on the GBA's screen: the vehicles in a panel, then
 * a row for the icon and one for each colour, a label on the left and the
 * choices beside it. A tile shows one palette bank: everything in the
 * chosen row is in the green bank, so its green edge can share tiles with
 * the rest of it; the other rows' edges are in a colour every bank has.
 */
#define G_ICONS OBJ_TEXT_TILE           /* the eight icons' cubes, 4 tiles each */
#define G_SWATCH (OBJ_TEXT_TILE + 32)   /* a tile of each colour (1..14) */
#define G_VEHICLES (OBJ_TEXT_TILE + 48) /* the five vehicles (art.h: GF_*), 16 tiles each */
#define OBJ_PAL_SWATCH OBJ_PAL_PORTAL   /* the colours (1..14) */
#define G_ROW_Y(r) (62 + (r) * 26)
#define G_ROW_H 24
#define G_SWATCH_X(i) (73 + (i) * 11)
#define G_ICON_X(i) (73 + (i) * 39 / 2)

static uint32_t s_swatch_tiles[PLAYER_COLOR_COUNT][8] ALIGN4;
static int s_g_drawn[3]; /* what each row was drawn with: chosen, and its choice */
static int s_g_part;     /* the parts that stay put drawn: garage_part */
static int s_g_icon;     /* the icon the vehicles are in, and the ball's frame, in VRAM */
static int s_g_ball;

static void garage_enter(void)
{
    int i, y;
    /* the icons' cubes, and a tile of each colour */
    video_queue(OBJ_TILES + G_ICONS * 8, g_player_tiles, 4 * 8);
    for (i = 1; i < ICON_COUNT; i++)
        video_queue(OBJ_TILES + (G_ICONS + i * 4) * 8, g_player_tiles + (i * PLAYER_TILES + PT_CUBE) * 8, 4 * 8);
    for (i = 0; i < PLAYER_COLOR_COUNT; i++) {
        for (y = 0; y < 8; y++) s_swatch_tiles[i][y] = (uint32_t)(i + 1) * 0x11111111u;
        g_pal_obj[OBJ_PAL_SWATCH * 16 + 1 + i] = world_rgb15(g_player_colors[i]);
    }
    video_queue(OBJ_TILES + G_SWATCH * 8, s_swatch_tiles, PLAYER_COLOR_COUNT * 8);
    for (i = 0; i < 3; i++) s_g_drawn[i] = -1;
    s_g_icon = s_g_ball = -1;
    s_g_part = 0; /* (the rest in the next frames: garage_draw) */
}

/* what stays put, a part at a time: the title; the panel of the icon and
 * colours on every vehicle, and their names; the hint */
#define GARAGE_PARTS 3
static void garage_part(int part)
{
    static const char *mnames[MODE_COUNT] = {"CUBE", "SHIP", "BALL", "UFO", "WAVE"};
    int m;
    if (part == 0) {
        ui_text(120, 1, 2, UI_CENTER, UB_WHITE, 1, 0, "GARAGE");
    } else if (part == 1) {
        ui_panel(MX(60), 18, MX(580), 58, 0, UI(UB_WHITE, UI_EDGE));
        for (m = 0; m < MODE_COUNT; m++) ui_text(MX(112 + m * 104), 47, 1, UI_CENTER, UB_GRAY, 0, UI_X1, mnames[m]);
    } else {
        ui_text(120, SCR_H - 9, 1, UI_CENTER, UB_WHITE, 0, UI_TEXT, GLYPH_LEFT GLYPH_RIGHT " CHANGE   B BACK");
    }
}

static void garage_row(int r, int sel, int choice)
{
    static const char *rows[3] = {"ICON", "COLOR 1", "COLOR 2"};
    int y0 = G_ROW_Y(r), cy = y0 + G_ROW_H / 2;
    ui_panel(MX(40), y0, MX(600), y0 + G_ROW_H, 0, sel ? UI(UB_GREEN, UI_TEXT) : UI(UB_WHITE, UI_EDGE));
    if (r == 0) {
        /* the label and, under it, the icon's name */
        ui_text(MX(56), y0 + 4, 1, UI_LEFT, sel ? UB_GREEN : UB_GOLD, 0, sel ? UI_G0 : UI_X3, rows[r]);
        ui_text(MX(56), y0 + 13, 1, UI_LEFT, sel ? UB_GREEN : UB_GOLD, 0, sel ? UI_X3 : UI_TEXT,
                g_icon_names[choice % ICON_COUNT]);
        /* the chosen one framed (the cubes are sprites) */
        ui_fill(G_ICON_X(choice) - 8, cy - 8, 16, 16, UI(UB_GREEN, UI_TEXT));
        ui_fill(G_ICON_X(choice) - 7, cy - 7, 14, 14, UI(UB_GREEN, UI_K));
    } else {
        ui_text(MX(56), cy - 3, 1, UI_LEFT, sel ? UB_GREEN : UB_GOLD, 0, sel ? UI_G0 : UI_X3, rows[r]);
        /* the chosen one framed, and each colour's black square (the
         * colours are sprites) */
        ui_fill(G_SWATCH_X(choice) - 6, cy - 6, 12, 12, UI(UB_GREEN, UI_G0));
        ui_fill_n(G_SWATCH_X(0) - 5, cy - 5, 10, 10, G_SWATCH_X(1) - G_SWATCH_X(0), PLAYER_COLOR_COUNT,
                  UI(UB_GREEN, UI_K));
    }
}

static void garage_draw(void)
{
    const Game *g = &g_game;
    int choice[3] = {g->save.icon % ICON_COUNT, g->save.col1 % PLAYER_COLOR_COUNT, g->save.col2 % PLAYER_COLOR_COUNT};
    const uint32_t *veh = g_garage_frames + (uint32_t)choice[0] * GF_PICS * 16 * 8;
    int r, i, m, k, n = 0;
    /* a row drawn again when what it shows changes, two a frame (the
     * chosen one first): a move changes two, which then change together;
     * then what stays put (all of it a part at a time after the screen
     * opens, as the frames have time). While the screen opens, one row a
     * frame: its first frame also empties the surface, draws the world
     * behind it anew and fades the colours, and with two rows (about 60
     * scanlines each on a surface just emptied) it ran long when the
     * garage was opened again, or after the options or the level select */
    for (k = 0; k < 3 && n < (s_g_part < GARAGE_PARTS ? 1 : 2) && frame_lines_left() > MENU_ROW_LINES; k++) {
        r = (g->garage_row + k) % 3;
        if (choice[r] * 2 + (g->garage_row == r) != s_g_drawn[r]) {
            s_g_drawn[r] = choice[r] * 2 + (g->garage_row == r);
            garage_row(r, g->garage_row == r, choice[r]);
            n++;
        }
    }
    while (s_g_part < GARAGE_PARTS && frame_lines_left() > MENU_PART_LINES) garage_part(s_g_part++);
    /* the panels are see-through: the backdrop dimmed behind them */
    ui_dim(MX(60), 18, MX(580), 58, 11);
    ui_dim(MX(40), G_ROW_Y(0), MX(600), G_ROW_Y(2) + G_ROW_H, 11);
    spr_prio(0);
    /* the vehicles in the chosen icon, at the garage's size (the ball
     * turning: g->t * 3, its frame of the moment in its place) */
    if (choice[0] != s_g_icon && video_queue(OBJ_TILES + G_VEHICLES * 8, veh, GF_STILL * 16 * 8)) {
        s_g_icon = choice[0];
        s_g_ball = -1;
    }
    k = spr_frame_turn(g->t * 3.0f, 2.0f * PI, GF_BALL);
    if (k != s_g_ball &&
        video_queue(OBJ_TILES + (G_VEHICLES + MODE_BALL * 16) * 8, veh + (GF_STILL + k) * 16 * 8, 16 * 8))
        s_g_ball = k;
    for (m = 0; m < MODE_COUNT; m++)
        spr(MX(112 + m * 104) - 16, 32 - 16, SQ32, G_VEHICLES + m * 16, OBJ_PAL_PLAYER, 0, 0);
    for (i = 0; i < ICON_COUNT; i++)
        spr(G_ICON_X(i) - 8, G_ROW_Y(0) + G_ROW_H / 2 - 8, SQ16, G_ICONS + i * 4, OBJ_PAL_PLAYER, 0, 0);
    for (r = 1; r < 3; r++)
        for (i = 0; i < PLAYER_COLOR_COUNT; i++)
            spr(G_SWATCH_X(i) - 4, G_ROW_Y(r) + G_ROW_H / 2 - 4, SQ8, G_SWATCH + i, OBJ_PAL_SWATCH, 0, 0);
    spr_prio(1);
    draw_menu_world(&g_palettes[7], g->t * 1.5f);
}

/* ------------------------------------------------------------------ */
/* Options                                                             */
/* ------------------------------------------------------------------ */

/* (a row's text and bars on a line of tiles of their own, its edges on the
 * lines above and below: see the garage) */
#define O_ROW_Y(i) (21 + (i) * 16)
#define O_ROW_H 14
#define O_PANEL_Y (O_ROW_Y(OPT_COUNT - 1) + O_ROW_H + 2)
static int s_o_drawn[OPT_COUNT];  /* what was drawn: see options_draw */
static uint32_t s_o_panel;          /* (the panel: 1 the delay's help, 4 the output's, else the stats' key) */
static int s_o_part;                /* how much of the panel: options_panel */
static int s_o_title;

static void options_enter(void)
{
    int i;
    for (i = 0; i < OPT_COUNT; i++) s_o_drawn[i] = -1;
    s_o_panel = 0;
    s_o_title = 0; /* (in the next frames: options_draw) */
}

static void options_row(int i, int sel)
{
    const Game *g = &g_game;
    static const char *labels[OPT_COUNT] = {"MUSIC", "SOUND FX", "OUTPUT", "AUDIO DELAY", "ERASE PROGRESS", "BACK"};
    int y0 = O_ROW_Y(i), y1 = y0 + O_ROW_H, k, red = i == OPT_ERASE && g->erase_confirm;
    /* the row's text in its panel's bank (as in the garage) */
    int bank = red ? UB_RED : (sel ? UB_GREEN : UB_GOLD);
    uint8_t tc = red ? UI_TEXT : (sel ? UI_G0 : UI_X3);
    ui_panel(MX(110), y0, MX(530), y1, 0, sel ? UI(UB_GREEN, UI_TEXT) : UI(UB_WHITE, UI_EDGE));
    ui_text(MX(130), y0 + 3, 1, UI_LEFT, bank, 0, tc, red ? "PRESS A AGAIN" : labels[i]);
    if (i == OPT_MUSIC || i == OPT_SFX) {
        int v = i == OPT_MUSIC ? g->save.music_vol : g->save.sfx_vol;
        for (k = 0; k < 10; k++) {
            int x = MX(330 + k * 18);
            ui_fill(x, y0 + 3, 5, 7, k < v ? UI(UB_BAR, 4) : UI(UB_BAR, 14));
        }
    } else if (i == OPT_DELAY) {
        char val[24];
        snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %+d MS " GLYPH_RIGHT : "%+d MS", g->save.audio_delay * 10);
        ui_text(MX(426), y0 + 3, 1, UI_CENTER, bank, 0, tc, val);
    } else if (i == OPT_OUTPUT) {
        const char *v = g->save.speaker ? "SPEAKER" : "HEADPHONES";
        char val[24];
        snprintf(val, sizeof(val), sel ? GLYPH_LEFT " %s " GLYPH_RIGHT : "%s", v);
        ui_text(MX(426), y0 + 3, 1, UI_CENTER, bank, 0, tc, val);
    }
}

/* the levels finished and the coins got */
static void stats_counts(int *done, int *coins)
{
    const Game *g = &g_game;
    int i, k;
    *done = *coins = 0;
    for (i = 0; i < g_level_count && i < SAVE_MAX_LEVELS; i++) {
        if (g->save.progress.best[i] >= 100) (*done)++;
        for (k = 0; k < 3; k++) *coins += (g->save.progress.coins[i] >> k) & 1;
    }
}

/* what the panel shows (P_STATS, P_DELAY: the audio delay's help,
 * P_OUTPUT: the output's), and its key, to see when it changes (never 0;
 * 1 and 4 are the helps', as the stats' has bit 1 set) */
enum { P_STATS, P_DELAY, P_OUTPUT };
static uint32_t panel_key(int kind)
{
    const Progress *pr = &g_game.save.progress;
    int done, coins;
    if (kind != P_STATS) return kind == P_DELAY ? 1u : 4u;
    stats_counts(&done, &coins);
    return ((pr->total_attempts * 2654435761u) ^ (pr->total_jumps * 40503u) ^ (uint32_t)(done << 8 | coins)) | 2u;
}

/* a count in at most 6 characters */
static void count_text(char *buf, size_t n, uint32_t v)
{
    if (v < 1000000u) snprintf(buf, n, "%u", (unsigned)v);
    else if (v < 10000000u) snprintf(buf, n, "%u.%uM", (unsigned)(v / 1000000u), (unsigned)(v / 100000u % 10u));
    else snprintf(buf, n, "%uM", (unsigned)(v / 1000000u));
}

/* the panel under the rows: the controls and stats, or the audio delay's
 * or the output's help; in OPTIONS_PANEL_PARTS parts (a line of text each:
 * four lines, under the six rows) */
#define OPTIONS_PANEL_PARTS 5
static void options_panel(int kind, int part)
{
    const Game *g = &g_game;
    int x = MX(80), y = O_PANEL_Y + 3, k;
    char buf[64];
    if (part == 0) {
        ui_erase(0, O_PANEL_Y, SCR_W, SCR_H - O_PANEL_Y);
        ui_panel(MX(60), O_PANEL_Y, MX(580), SCR_H - 4, 0, UI(UB_WHITE, UI_EDGE));
        return;
    }
    if (kind == P_DELAY) {
        /* (the row says what it is: the help is how) */
        static const char *const help[2] = {"KICK AFTER FLASH:  RAISE IT", "KICK BEFORE FLASH: LOWER IT"};
        if (part <= 2) {
            ui_text(x, y + (part - 1) * 8, 1, UI_LEFT, UB_WHITE, 0, UI_G0, help[part - 1]);
        } else if (part == 3) {
            /* the lights: their colours are set each frame (options_draw) */
            for (k = 0; k < 4; k++) {
                int cx = 120 + (2 * k - 3) * 13, r = k == 0 ? 6 : 5;
                ui_disc(cx, y + 24, r + 1, UI(UB_LIGHT, UI_K));
                ui_disc(cx, y + 24, r, UI(UB_LIGHT, 4 + k));
            }
        }
        return;
    }
    if (kind == P_OUTPUT) {
        static const char *const help[4] = {"SPEAKER: MIXED FOR THE GBA'S", "OWN SPEAKER: MONO, LOUDER,",
                                            "LESS BASS.", "HEADPHONES: FULL STEREO MIX."};
        ui_text(x, y + (part - 1) * 8, 1, UI_LEFT, UB_WHITE, 0, UI_G0, help[part - 1]);
        return;
    }
    /* (a line of tiles each: the gold and the white text in banks of their
     * own; the counts, at most 29 letters, have a line of their own the
     * panel's width) */
    if (part == 1) {
        ui_text(x, y, 1, UI_LEFT, UB_WHITE, 0, UI_G0, "A UP L R  JUMP, HOLD TO FLY");
    } else if (part == 2) {
        ui_text(x, y + 8, 1, UI_LEFT, UB_WHITE, 0, UI_G0, "PRACTICE: B SET  SELECT REMOVE");
    } else if (part == 3) {
        int done, coins;
        stats_counts(&done, &coins);
        ui_text(x, y + 16, 1, UI_LEFT, UB_GOLD, 0, UI_TEXT, "STATS");
        snprintf(buf, sizeof(buf), "LEVELS %d/%d  COINS %d", done, g_level_count, coins);
        ui_text(x + 36, y + 16, 1, UI_LEFT, UB_WHITE, 0, UI_G0, buf);
    } else {
        char a[12], j[12];
        count_text(a, sizeof(a), g->save.progress.total_attempts);
        count_text(j, sizeof(j), g->save.progress.total_jumps);
        snprintf(buf, sizeof(buf), "ATTEMPTS %s  JUMPS %s", a, j);
        ui_text(x, y + 24, 1, UI_LEFT, UB_WHITE, 0, UI_G0, buf);
    }
}

static void options_draw(void)
{
    const Game *g = &g_game;
    int i, k, n = 0, delay = g->options_sel == OPT_DELAY;
    int kind = delay ? P_DELAY : g->options_sel == OPT_OUTPUT ? P_OUTPUT : P_STATS;
    /* a row drawn again when what it shows changes (two a frame, the
     * chosen one first: a move changes two, which then change together),
     * then the panel under them, a part at a time (drawn again when what
     * it shows changes: the stats too, erasing the progress), and after
     * the screen opens the title, as the frames have time */
    for (k = 0; k < OPT_COUNT && n < 2 && frame_lines_left() > MENU_ROW_LINES; k++) {
        int key, sel;
        i = (g->options_sel + k) % OPT_COUNT;
        sel = g->options_sel == i;
        key = sel;
        if (i == OPT_MUSIC) key += g->save.music_vol * 2;
        if (i == OPT_SFX) key += g->save.sfx_vol * 2;
        if (i == OPT_DELAY) key += (g->save.audio_delay + 128) * 2;
        if (i == OPT_OUTPUT) key += g->save.speaker * 2;
        if (i == OPT_ERASE) key += g->erase_confirm * 2;
        if (key != s_o_drawn[i]) {
            s_o_drawn[i] = key;
            options_row(i, sel);
            n++;
        }
    }
    {
        uint32_t pk = panel_key(kind);
        if (pk != s_o_panel) {
            s_o_panel = pk;
            s_o_part = 0;
        }
        while (s_o_part < OPTIONS_PANEL_PARTS && frame_lines_left() > MENU_PART_LINES) options_panel(kind, s_o_part++);
    }
    if (!s_o_title && frame_lines_left() > MENU_PART_LINES) {
        ui_text(120, 1, 2, UI_CENTER, UB_WHITE, 1, 0, "OPTIONS");
        s_o_title = 1;
    }
    if (delay) {
        /* draw_delay_help's lights: the one on the beat heard flashes */
        float b = audio_current_song() == SONG_METRONOME ? audio_song_beat() : -1.0f;
        int lit = b >= 0.0f ? (int)b % 4 : -1, k;
        float glow = b >= 0.0f ? expf(-(b - floorf(b)) * 6.0f) : 0.0f;
        for (k = 0; k < 4; k++) {
            Color off = RGB(45, 48, 58), on = k == 0 ? RGB(255, 230, 90) : RGB(90, 255, 140);
            g_pal_bg[UB_LIGHT * 16 + 4 + k] = world_rgb15(k == lit ? col_lerp(off, on, glow) : off);
        }
    }
    ui_dim(MX(110), O_ROW_Y(0), MX(530), O_ROW_Y(OPT_COUNT - 1) + O_ROW_H, 11);
    ui_dim(MX(60), O_PANEL_Y, MX(580), SCR_H - 4, 12);
    draw_menu_world(&g_palettes[1], g->t * 1.5f);
}

/* ------------------------------------------------------------------ */

void menus_init(void)
{
    int i;
    /* (reading a level's header and counting its coins takes most of two
     * frames: done once, before the title shows) */
    for (i = 0; i < g_level_count && i < PROGRESS_LEVELS; i++) level_info(i, &s_info[i]);
}

void menus_enter(int screen)
{
    ui_clear();
    sprites_palettes();
    if (screen == SCR_TITLE) title_enter();
    else if (screen == SCR_SELECT) select_enter();
    else if (screen == SCR_GARAGE) garage_enter();
    else if (screen == SCR_OPTIONS) options_enter();
}

void menus_draw_text(int screen)
{
    if (screen == SCR_TITLE) title_draw_text();
}

void menus_draw(int screen)
{
    if (screen == SCR_TITLE) title_draw();
    else if (screen == SCR_SELECT) select_draw();
    else if (screen == SCR_GARAGE) garage_draw();
    else if (screen == SCR_OPTIONS) options_draw();
}
