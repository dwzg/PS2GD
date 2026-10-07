/*
 * The play screen's overlays on the GBA (hud.h), laid out as play_draw.c
 * lays them out on a 640x448 screen, scaled to 240x160 (x * 3/8,
 * y * 5/14) and moved where the GBA's wider letters need it.
 *
 * BG0 holds what stays put, drawn again only when it changes: the
 * progress bar and its percentage, PRACTICE, the practice hint, the pause
 * menu and the results. A tile of BG0 shows one palette bank (ui.h), so
 * text in different colours keeps to tiles of its own. The texts that
 * grow (NEW BEST, LEVEL COMPLETE) and the attempt counter, which moves
 * with the world, are sprites. The pause menu and the results take frames
 * to draw: they are drawn out of sight (ui_hold) and shown whole, and so
 * is a move of their choice.
 */
#include <stdio.h>
#include <string.h>

#include "hud.h"
#include "ui.h"
#include "video.h"
#include "sprites.h"
#include "world.h"
#include "gen.h"
#include "save.h"
#include "frame.h"

#define MX(x) ((int)((x) * 3) / 8)
#define MY(y) ((int)((y) * 5) / 14)

/* the texts' sprite tiles */
#define H_ATTEMPT OBJ_TEXT_TILE          /* "PRACTICE 999" at scale 2: 5 pieces */
#define H_BEST (OBJ_TEXT_TILE + 40)      /* "NEW BEST 100%": 5 */
#define H_COMPLETE (OBJ_TEXT_TILE + 80)  /* "LEVEL COMPLETE!": 6 */

/* OBJ_PAL_TEXT: 4..9 white to pale blue (the last two rows alike), 3 its
 * outline; 1, 2, 11..15 gold, 10 its outline (NEW BEST's fades alone) */
#define GOLD_K 10
static const TextStyle ST_WHITE = {{4, 5, 6, 7, 8, 9, 9}, 3};
static const TextStyle ST_GOLD = {{1, 2, 11, 12, 13, 14, 15}, GOLD_K};
static const uint8_t GOLD_IDX[7] = {1, 2, 11, 12, 13, 14, 15};
static uint16_t s_gold[7]; /* the gold's colours, to fade NEW BEST from */

static ObjText s_attempt_t, s_best_t, s_complete_t;
static int s_attempt_key, s_best_val;
/* the scanlines ATTEMPT n's text takes to draw (about 45), and the rest of
 * the frame after it; NEW BEST's (about 70, and the frame's last things) */
#define ATTEMPT_LINES 110
#define BEST_LINES 100

/* what BG0 shows (-1: to draw) */
enum { M_RUN, M_PAUSE, M_RESULTS };
static struct {
    int mode, bar, pct, practice, hint, results;
    int item[4]; /* the overlay's items as drawn: 1 chosen, 0 not, -1 not yet */
} s_h;
/* the overlay (the pause menu, the results) is on the screen: once it is
 * drawn and packed whole (until then BG0 is held, hud_begin) */
static int s_shown;

static void gradient(uint16_t *pal, const uint8_t *idx, Color top, Color bot)
{
    int r;
    for (r = 0; r < 7; r++) pal[idx[r]] = world_rgb15(col_lerp(top, bot, r / 6.0f));
}

void hud_enter(void)
{
    uint16_t *pal = g_pal_obj + OBJ_PAL_TEXT * 16;
    gradient(pal, ST_WHITE.row, COL_WHITE, RGB(210, 225, 255));
    gradient(pal, GOLD_IDX, RGB(255, 255, 165), RGB(255, 170, 35));
    pal[3] = pal[GOLD_K] = 0;
    {
        int r;
        for (r = 0; r < 7; r++) s_gold[r] = pal[GOLD_IDX[r]];
    }
    ui_clear();
    memset(&s_h, 0xFF, sizeof(s_h));
    s_h.mode = M_RUN; /* (cleared: hud_begin need not clear again) */
    s_shown = 1;
    s_attempt_key = -1;
    s_best_val = -1;
    s_complete_t.pieces = 0;
}

/* ------------------------------------------------------------------ */
/* Sprites                                                             */
/* ------------------------------------------------------------------ */

/* a text that pops in: ease_out_back over `grow` seconds from t = 0 */
static int pop_k(float t, float grow)
{
    if (t >= grow) return 256;
    return (int)(ease_out_back(clampf(t / grow, 0.0f, 1.0f)) * 256.0f);
}

void hud_sprites_front(const PlayState *ps)
{
    /* (under BG0: the pause menu covers them, as on the other platforms) */
    spr_prio(1);
    if (ps->best_popup_t > 0.0f && s_best_val == ps->best_popup_val) {
        /* NEW BEST: pops in, then fades out (towards the sky behind it);
         * its text drawn at the end of a frame with the time for it
         * (hud_draw: not the death's, which has much else to do), shown
         * from the next one (it pops in from nothing) */
        float t = 2.0f - ps->best_popup_t, a = clampf(ps->best_popup_t / 0.4f, 0.0f, 1.0f);
        int y = MY(104) - 8, r;
        {
            Color sky = world_backdrop(y + 8);
            uint16_t *pal = g_pal_obj + OBJ_PAL_TEXT * 16;
            for (r = 0; r < 7; r++) {
                uint16_t c = s_gold[r];
                Color from = RGB((c & 31) << 3, ((c >> 5) & 31) << 3, ((c >> 10) & 31) << 3);
                pal[GOLD_IDX[r]] = world_rgb15(col_lerp(sky, from, a));
            }
            pal[GOLD_K] = world_rgb15(col_lerp(sky, RGB(0, 0, 0), a));
        }
        ui_obj_text_show(&s_best_t, 120, y, OBJ_PAL_TEXT, pop_k(t, 0.35f));
    } else if (s_best_val >= 0 && ps->best_popup_t <= 0.0f) {
        /* (the colours back for LEVEL COMPLETE) */
        int r;
        for (r = 0; r < 7; r++) g_pal_obj[OBJ_PAL_TEXT * 16 + GOLD_IDX[r]] = s_gold[r];
        g_pal_obj[OBJ_PAL_TEXT * 16 + GOLD_K] = 0;
        s_best_val = -1;
    }
    if (ps->phase == PH_COMPLETE && ps->phase_t >= 0.4f) {
        if (!s_complete_t.pieces) ui_obj_text(&s_complete_t, H_COMPLETE, 2, &ST_GOLD, "LEVEL COMPLETE!");
        /* (4 pixels under PRACTICE, which BG0 shows at 16..24, and over
         * the results' panel, from 46) */
        ui_obj_text_show(&s_complete_t, 120, MY(90) - 4, OBJ_PAL_TEXT, pop_k(ps->phase_t - 0.4f, 0.3f));
    }
    spr_prio(1);
}

void hud_sprites_world(const PlayState *ps)
{
    /* the attempt counter painted into the world near the start, behind
     * the blocks (the ground's priority) */
    int key = ps->attempt * 2 + ps->practice, x = 3 * BLOCK_PIX - g_cam_px;
    if (x < -160 || x >= SCR_W) return;
    if (key != s_attempt_key) {
        /* (drawn when the frame has the time: a respawn's frame has the
         * world's tiles all again; until then, not shown) */
        if (frame_lines_left() <= ATTEMPT_LINES) return;
        char buf[32];
        s_attempt_key = key;
        snprintf(buf, sizeof(buf), ps->practice ? "PRACTICE %d" : "ATTEMPT %d", ps->attempt);
        ui_obj_text(&s_attempt_t, H_ATTEMPT, 2, &ST_WHITE, buf);
    }
    spr_prio(2);
    ui_obj_text_show(&s_attempt_t, x + s_attempt_t.w / 2, (int)(-6.2f * BLOCK_PIX) - g_cam_py, OBJ_PAL_TEXT, 256);
    spr_prio(1);
}

/* ------------------------------------------------------------------ */
/* BG0                                                                 */
/* ------------------------------------------------------------------ */

#define BAR_X0 MX(180)
#define BAR_X1 MX(440)
#define BAR_Y0 5
#define BAR_Y1 9

static void draw_bar(int frac)
{
    ui_bar(BAR_X0, BAR_Y0, BAR_X1, BAR_Y1, frac, UB_BAR);
}

static void draw_pct(int pct)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    ui_erase(BAR_X1 + 4, 1, 32, 11);
    ui_text(BAR_X1 + 5, 3, 1, UI_LEFT, UB_WHITE, 1, 0, buf);
}

/* draw_pause's panel: the level's name, the bests, the menu */
#define P_X0 MX(120)
#define P_X1 MX(520)
#define P_Y0 MY(60)
#define P_Y1 MY(400)

/* part k of it (a frame draws as many parts as it has time for: the
 * panel and its text take 170 scanlines, the items 110 and packing it all
 * 170, in mGBA: three frames' time and a bit; it is shown once all done) */
#define PAUSE_PARTS 5
static void draw_pause_part(const PlayState *ps, int k)
{
    const Game *g = &g_game;
    int idx = ps->level_idx < SAVE_MAX_LEVELS ? ps->level_idx : 0;
    char buf[32];
    switch (k) {
    case 0:
        ui_panel(P_X0, P_Y0, P_X1, P_Y1, UI(UB_WHITE, UI_FILL), UI(UB_WHITE, UI_EDGE));
        break;
    case 1:
        /* (every level's name the same size: see menus.c's cards) */
        ui_text(120, 28, 1, UI_CENTER, UB_WHITE, 1, 0, ps->info.name);
        break;
    case 2:
        ui_text(52, 45, 1, UI_LEFT, UB_WHITE, 0, UI_TEXT, "NORMAL");
        ui_bar(104, 46, 164, 50, g->save.progress.best[idx] * 256 / 100, UB_BAR);
        snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best[idx]);
        ui_text(168, 45, 1, UI_LEFT, UB_WHITE, 0, UI_G0, buf);
        break;
    case 3:
        ui_text(52, 54, 1, UI_LEFT, UB_WHITE, 0, UI_TEXT, "PRACTICE");
        ui_bar(104, 55, 164, 59, g->save.progress.best_practice[idx] * 256 / 100, UB_BAR + 16);
        snprintf(buf, sizeof(buf), "%d%%", g->save.progress.best_practice[idx]);
        ui_text(168, 54, 1, UI_LEFT, UB_WHITE, 0, UI_G0, buf);
        break;
    default:
        snprintf(buf, sizeof(buf), "ATTEMPT %d", ps->attempt);
        ui_text(120, MY(368), 1, UI_CENTER, UB_WHITE, 0, UI_DIM, buf);
        break;
    }
}

/* a part of an overlay is drawn while the frame has this many lines left
 * (frame.h): its panel (the pause menu's takes 80), any other part (up to
 * 30) or an item (up to 45, measured in mGBA) */
#define PANEL_LINES 100
#define PART_LINES 60
#define ITEM_LINES 64
#define part_lines(k) ((k) ? PART_LINES : PANEL_LINES)
/* an overlay drawn whole is shown in a frame that packs what is left of
 * it, at most this many cells (about 0.65 scanlines each) */
#define SHOW_CELLS 64

/* the chosen item's panel and the items' text: centred at (cx, y); a
 * former choice's panel erased (an item drawn for the first time is on
 * the panel's inside already) */
static void draw_item(int cx, int y, int half_w, int sel, int erase, const char *s)
{
    if (sel) ui_panel(cx - half_w, y - 3, cx + half_w, y + 10, UI(UB_GREEN, UI_DIM), UI(UB_GREEN, UI_TEXT));
    else if (erase) ui_fill(cx - half_w, y - 3, 2 * half_w, 13, UI(UB_WHITE, UI_FILL));
    ui_text(cx, y, 1, UI_CENTER, sel ? UB_GREEN : UB_GRAY, 1, 0, s);
}

static void draw_pause_item(const PlayState *ps, int i, int sel, int erase)
{
    const char *items[4] = {"RESUME", "RESTART", ps->practice ? "NORMAL MODE" : "PRACTICE MODE", "EXIT LEVEL"};
    draw_item(120, MY(196 + i * 42), MX(150), sel, erase, items[i]);
}

/* draw_results: the panel, its text and the missing coins (the coins
 * are sprites: results_sprites) */
#define R_X0 MX(140)
#define R_X1 MX(500)
#define R_Y0 MY(130)
#define R_Y1 MY(380)
#define R_COIN_Y MY(268)

/* part k of it (as the pause panel's) */
#define RESULTS_PARTS 6
static void draw_results_part(const PlayState *ps, int k)
{
    const Game *g = &g_game;
    uint8_t have = ps->level_idx < SAVE_MAX_LEVELS ? g->save.progress.coins[ps->level_idx] : 0;
    int secs = (int)ps->time_session, i, n = ps->L->ncoins;
    char buf[32];
    switch (k) {
    case 0:
        ui_panel(R_X0, R_Y0, R_X1, R_Y1, UI(UB_WHITE, UI_FILL), UI(UB_WHITE, UI_EDGE));
        break;
    case 1:
        snprintf(buf, sizeof(buf), "ATTEMPTS: %d", ps->attempt);
        ui_text(120, MY(152), 1, UI_CENTER, UB_WHITE, 0, UI_G0, buf);
        break;
    case 2:
        snprintf(buf, sizeof(buf), "JUMPS: %d", ps->jumps_session);
        ui_text(120, MY(184), 1, UI_CENTER, UB_WHITE, 0, UI_G0, buf);
        break;
    case 3:
        snprintf(buf, sizeof(buf), "TIME: %d:%02d", secs / 60, secs % 60);
        ui_text(120, MY(216), 1, UI_CENTER, UB_WHITE, 0, UI_G0, buf);
        break;
    case 4:
        for (i = 0; i < n; i++)
            if (!((have >> i) & 1)) ui_disc(120 + (2 * i - (n - 1)) * MX(50) / 2, R_COIN_Y, 5, UI(UB_WHITE, UI_K));
        break;
    default:
        if (ps->practice) ui_text(120, MY(292), 1, UI_CENTER, UB_GREEN, 0, UI_TEXT, "PRACTICE RUN");
        else if (ps->new_best) ui_text(120, MY(292), 1, UI_CENTER, UB_GOLD, 0, UI_X1, "FIRST CLEAR!");
        break;
    }
}

static void draw_results_item(const PlayState *ps, int i, int sel, int erase)
{
    draw_item(120 + (i ? MX(90) : -MX(90)), MY(328), MX(75), sel, erase, i ? "REPLAY" : "MENU");
}

/* The overlay's items whose look changed (all of them at first, the two of
 * a move), as the frame has time: once the overlay is shown, a move is held
 * back until drawn and packed whole (ui_hold), so that the choice moves in
 * one frame. */
static void draw_items(const PlayState *ps, int n, int sel, void (*draw)(const PlayState *, int, int, int))
{
    int i, todo = 0;
    for (i = 0; i < n; i++) todo += s_h.item[i] != (i == sel);
    if (!todo) return;
    if (s_shown) ui_hold(0);
    for (i = 0; i < n; i++) {
        int on = i == sel;
        if (s_h.item[i] == on) continue;
        if (frame_lines_left() <= ITEM_LINES) return; /* (the rest next frame, still held) */
        draw(ps, i, on, s_h.item[i] >= 0);
        s_h.item[i] = on;
    }
    if (s_shown) ui_hold(1);
}

/* the overlay drawn whole, its items as chosen */
static int overlay_drawn(const PlayState *ps)
{
    int pause = s_h.mode == M_PAUSE, n = pause ? 4 : 2, sel = pause ? ps->pause_sel : ps->results_sel, i;
    if (s_h.results < (pause ? PAUSE_PARTS : RESULTS_PARTS)) return 0;
    for (i = 0; i < n; i++)
        if (s_h.item[i] != (i == sel)) return 0;
    return 1;
}

static void results_sprites(const PlayState *ps)
{
    const Game *g = &g_game;
    uint8_t have = ps->level_idx < SAVE_MAX_LEVELS ? g->save.progress.coins[ps->level_idx] : 0;
    int i, n = ps->L->ncoins;
    spr_prio(0);
    for (i = 0; i < n; i++) {
        /* render_coin: turning, the ones just got pulsing */
        int cx = 120 + (2 * i - (n - 1)) * MX(50) / 2, fresh = (ps->coins_gained >> i) & 1;
        float sc = fresh ? 1.0f + 0.2f * tsinf(g->t * 8.0f) : 1.0f;
        int kx = (int)(tcosf(g->t * 3.0f + (float)i) * sc * 256.0f), ky = (int)(sc * 256.0f);
        if (!((have >> i) & 1)) continue;
        if (kx > -16 && kx < 16) kx = kx < 0 ? -16 : 16;
        spr_aff(cx, R_COIN_Y, 16, 16, SQ16, OT_COIN, OBJ_PAL_SPEED, video_aff(0, kx, ky), 1, 0);
    }
    spr_prio(1);
}

void hud_begin(const PlayState *ps)
{
    int mode = ps->paused ? M_PAUSE : (ps->phase == PH_COMPLETE && ps->phase_t >= 1.6f ? M_RESULTS : M_RUN);
    if (mode != s_h.mode) {
        if (s_h.mode == M_PAUSE && mode == M_RUN) {
            /* back to the run (with its song, the frame on time): the
             * pause panel's rows go, the bar and its percentage above
             * them stay; PRACTICE, in those rows, is drawn again */
            ui_clear_rows(P_Y0, P_Y1);
            s_h.practice = 0;
        } else if (s_h.mode == M_RUN) {
            /* the pause menu or the results over the run: all but the bar
             * and its percentage (the rows above PRACTICE's) */
            ui_clear_rows(16, SCR_H);
            s_h.practice = s_h.hint = 0;
        } else {
            /* a new overlay: all of BG0 again */
            ui_clear();
            memset(&s_h, 0xFF, sizeof(s_h));
        }
        s_h.results = -1;
        memset(s_h.item, 0xFF, sizeof(s_h.item));
        s_h.mode = mode;
        /* an overlay is drawn out of sight, the screen as it was (the
         * run, stopped), and shown whole: its panel, its text and the
         * pause's darkening in one frame */
        s_shown = mode == M_RUN;
        if (s_shown) ui_show(0);
        else ui_hold(0);
    } else if (!s_shown && overlay_drawn(ps) && ui_pending() <= SHOW_CELLS) {
        /* (all of it drawn, and packed but for a few cells, which this
         * frame packs: shown in it, decided before its sprites) */
        s_shown = 1;
        ui_show(1);
    }
}

int hud_pause_shown(void)
{
    return s_h.mode == M_PAUSE && s_shown;
}

void hud_draw(const PlayState *ps)
{
    float frac = ps->L ? sim_x(&ps->p) / ps->L->end_x : 0.0f;
    int mode = s_h.mode;
    int bar, pct, hint, shown;
    if (ps->phase == PH_COMPLETE) frac = 1.0f;
    frac = clampf(frac, 0.0f, 1.0f);
    /* draw_hud */
    bar = (int)(frac * (BAR_X1 - BAR_X0));
    if (bar != s_h.bar) {
        s_h.bar = bar;
        draw_bar(bar * 256 / (BAR_X1 - BAR_X0));
    }
    pct = clampi((int)(frac * 100.0f), 0, 100);
    if (pct != s_h.pct) {
        s_h.pct = pct;
        draw_pct(pct);
    }
    /* (PRACTICE not under the pause menu, whose panel it would stick out
     * of; s_h.practice and s_h.hint: shown, so erased only if they were.
     * Drawn as the frame has time: not in a run's first, which draws all
     * the level's tiles in view) */
    shown = ps->practice && mode != M_PAUSE;
    if (shown != s_h.practice && (!shown || frame_lines_left() > PART_LINES)) {
        if (s_h.practice == 1) ui_erase(0, 16, SCR_W, 9);
        s_h.practice = shown;
        if (shown) ui_text(120, 17, 1, UI_CENTER, UB_GREEN, 1, 0, "PRACTICE");
    }
    hint = ps->practice && ps->attempt_time < 5.0f && ps->attempt <= 2 && mode == M_RUN;
    if (hint != s_h.hint && (!hint || frame_lines_left() > PART_LINES)) {
        if (s_h.hint == 1) ui_erase(0, MY(422) - 1, SCR_W, 9);
        s_h.hint = hint;
        if (hint) ui_text(120, MY(422), 1, UI_CENTER, UB_HINT, 0, UI_TEXT, "B CHECKPOINT   SELECT REMOVE");
    }
    if (hint) ui_hint_level((int)(clampf(5.0f - ps->attempt_time, 0.0f, 1.0f) * 16.0f), world_ground());

    if (mode == M_PAUSE) {
        /* (s_h.results: the parts drawn; -1 none) */
        if (s_h.results < 0) s_h.results = 0;
        while (s_h.results < PAUSE_PARTS && frame_lines_left() > part_lines(s_h.results))
            draw_pause_part(ps, s_h.results++);
        if (s_h.results == PAUSE_PARTS) draw_items(ps, 4, ps->pause_sel, draw_pause_item);
        if (s_shown) {
            /* draw_pause's dimming: all of it but the panel (the
             * see-through sprites are hidden: sprites_run) */
            g_vid.dispcnt |= DCNT_WIN0;
            g_vid.win0h = (uint16_t)(P_X0 << 8 | P_X1);
            g_vid.win0v = (uint16_t)(P_Y0 << 8 | P_Y1);
            g_vid.winin = 0x1F;
            g_vid.winout = 0x3F;
            g_vid.bldcnt = BLD_ALL | BLD_BLACK;
            g_vid.bldy = 9; /* RGBA(0, 0, 0, 150) */
        }
    } else if (mode == M_RESULTS) {
        if (s_h.results < 0) s_h.results = 0;
        while (s_h.results < RESULTS_PARTS && frame_lines_left() > part_lines(s_h.results))
            draw_results_part(ps, s_h.results++);
        if (s_h.results == RESULTS_PARTS) draw_items(ps, 2, ps->results_sel, draw_results_item);
        if (s_shown) results_sprites(ps);
    }
    if (ps->best_popup_t > 0.0f && s_best_val != ps->best_popup_val && frame_lines_left() > BEST_LINES) {
        /* NEW BEST's text, for the next frames (hud_sprites_front) */
        char buf[32];
        s_best_val = ps->best_popup_val;
        snprintf(buf, sizeof(buf), "NEW BEST %d%%", ps->best_popup_val);
        ui_obj_text(&s_best_t, H_BEST, 2, &ST_GOLD, buf);
    }
}
