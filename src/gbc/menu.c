/*
 * Title screen and level select (in a ROM bank of their own: they must not
 * switch banks themselves, video.c does that for them).
 */
#pragma bank 15
#include <string.h>

#include "gbc.h"

static const char *const DIFF_NAMES[] = {"EASY", "NORMAL", "HARD", "HARDER", "INSANE", "DEMON"};

/* fmt_uint for 32 bits */
static char *fmt_ulong(char *buf, uint32_t v)
{
    static const uint32_t POW[9] = {1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
                                    10000UL,      1000UL,      100UL,      10UL};
    uint8_t i, n = 0;
    if (v <= 65535UL) return fmt_uint(buf, (uint16_t)v);
    for (i = 0; i < 9; i++) {
        char d = '0';
        while (v >= POW[i]) {
            v -= POW[i];
            d++;
        }
        if (d != '0' || n) buf[n++] = d;
    }
    buf[n++] = (char)('0' + (uint8_t)v);
    buf[n] = 0;
    return buf;
}

static void center_bkg(uint8_t y, const char *s, uint8_t pal)
{
    text_bkg((uint8_t)((20 - strlen(s)) / 2), y, s, pal);
}

static void screen_begin(uint8_t screen)
{
    uint8_t i;
    g_screen = screen;
    video_blank();
    for (i = 0; i < 40; i++) hide_sprite(i);
    HIDE_WIN;
    g_hud_split = 0;
    g_scx = 0;
    bkg_clear();
}

/* --- garage --- */

/* the rows: icon, colour 1, colour 2 (label row, choices one below) */
static const uint8_t GARAGE_Y[3] = {6, 10, 14};

/* x of choice i of a row, in tiles */
static uint8_t choice_x(uint8_t row, uint8_t i)
{
    return row ? (uint8_t)(3 + i) : (uint8_t)(3 + 2 * i);
}

static void garage_draw(void)
{
    uint8_t i;
    char buf[21];
    for (i = 0; i < 3; i++) tile_bkg(0, GARAGE_Y[i], UT_BLANK, PAL_TEXT | 0x08);
    /* the icon's name after its label */
    strcpy(buf, gbc_icon_names[g_save.icon % ICON_COUNT]);
    while (strlen(buf) < 8) strcat(buf, " ");
    text_bkg(11, GARAGE_Y[0], buf, PAL_TEXT);
}

/* the cursor boxes around the chosen icon and colours (sprites 10..12) */
static void garage_cursors(uint8_t row)
{
    uint8_t i, k;
    for (i = 0; i < 3; i++) {
        k = i == 0 ? g_save.icon : (i == 1 ? g_save.col1 : g_save.col2);
        set_sprite_tile(10 + i, ST_BOX);
        set_sprite_prop(10 + i, OPAL_FX | 0x08);
        move_sprite(10 + i, (uint8_t)(choice_x(i, k) * 8 + 8), (uint8_t)((GARAGE_Y[i] + 1) * 8 + 4 + 16));
    }
    tile_bkg(0, GARAGE_Y[row], UT_DIAMOND, PAL_TEXT | 0x08);
}

/* a 16x16 frame of the player's (tile) at x, row 3, in sprites k, k + 1
 * (SDCC 4.2 warns of an overflow in the calls below that its code doesn't
 * have: it passes each value as written) */
#pragma disable_warning 158
static void vehicle(uint8_t k, uint8_t tile, uint8_t x)
{
    set_sprite_tile(k, tile);
    set_sprite_prop(k, OPAL_PLAYER | 0x08);
    move_sprite(k, x, 24 + 16);
    set_sprite_tile(k + 1, tile + 2);
    set_sprite_prop(k + 1, OPAL_PLAYER | 0x08);
    move_sprite(k + 1, x + 8, 24 + 16);
}

/* The vehicles in the player's icon and colours: cube, ship, ball, UFO,
 * wave (sprites 0..9). */
static void garage_vehicles(void)
{
    vehicle(0, ST_CUBE, 20);
    vehicle(2, ST_SHIP + 4 * 3, 50);
    vehicle(4, ST_BALL, 80);
    vehicle(6, ST_UFO + 4, 110);
    vehicle(8, ST_WAVE + 4, 140);
}

void garage_screen(void) BANKED
{
    uint8_t row = 0, i;
    screen_begin(SCR_GARAGE);
    center_bkg(1, "GARAGE", PAL_TEXT);
    text_bkg(2, GARAGE_Y[0], "ICON", PAL_TEXT);
    text_bkg(2, GARAGE_Y[1], "COLOR 1", PAL_TEXT);
    text_bkg(2, GARAGE_Y[2], "COLOR 2", PAL_TEXT);
    /* the choices half a tile under their labels: each in two tiles */
    for (i = 0; i < ICON_COUNT; i++) {
        tile_bkg(choice_x(0, i), GARAGE_Y[0] + 1, UT_ICON + 2 * i, 0 | 0x08);
        tile_bkg(choice_x(0, i), GARAGE_Y[0] + 2, UT_ICON + 2 * i + 1, 0 | 0x08);
    }
    for (i = 0; i < PLAYER_COLOR_COUNT; i++) {
        /* colour i is colour i % 3 + 1 of palette i / 3 + 1 */
        uint8_t t = UT_SOLID + 2 * (i % 3), a = (uint8_t)((1 + i / 3) | 0x08);
        tile_bkg(choice_x(1, i), GARAGE_Y[1] + 1, t, a);
        tile_bkg(choice_x(1, i), GARAGE_Y[1] + 2, t + 1, a);
        tile_bkg(choice_x(2, i), GARAGE_Y[2] + 1, t, a);
        tile_bkg(choice_x(2, i), GARAGE_Y[2] + 2, t + 1, a);
    }
    tile_bkg(1, 17, UT_ARROW_L, PAL_TEXT | 0x08);
    tile_bkg(2, 17, UT_ARROW_R, PAL_TEXT | 0x08);
    text_bkg(4, 17, "CHANGE   B BACK", PAL_TEXT);
    garage_draw();
    garage_cursors(row);
    garage_vehicles();
    pal_garage();
    video_on();
    music_ensure(SONG_MENU_GB);
    for (;;) {
        int8_t d = 0;
        frame_wait();
        music_tick();
        if (g_pressed & (J_B | J_START | J_A)) {
            sfx_play(SFX_BACK);
            break;
        }
        if (g_pressed & (J_UP | J_DOWN)) {
            tile_bkg(0, GARAGE_Y[row], UT_BLANK, PAL_TEXT | 0x08);
            row = g_pressed & J_UP ? (row ? row - 1 : 2) : (row == 2 ? 0 : row + 1);
            garage_cursors(row);
            sfx_play(SFX_MOVE);
        }
        if (g_pressed & J_LEFT) d = -1;
        if (g_pressed & J_RIGHT) d = 1;
        if (d) {
            uint8_t *v = row == 0 ? &g_save.icon : (row == 1 ? &g_save.col1 : &g_save.col2);
            uint8_t n = row == 0 ? ICON_COUNT : PLAYER_COLOR_COUNT;
            *v = (uint8_t)(d > 0 ? (*v + 1 == n ? 0 : *v + 1) : (*v ? *v - 1 : n - 1));
            sfx_play(SFX_MOVE);
            garage_draw();
            garage_cursors(row);
            if (row == 0) video_icon(g_save.icon);
            pal_sprites();
            pal_garage();
        }
    }
    save_write();
}

/* --- level select --- */

static void percent_line(uint8_t y, const char *label, uint8_t pc)
{
    char buf[21], n[6];
    uint8_t len;
    strcpy(buf, label);
    fmt_uint(n, pc);
    strcat(n, "%");
    len = (uint8_t)strlen(buf);
    while (len < 16 - strlen(n)) buf[len++] = ' ';
    buf[len] = 0;
    strcat(buf, n);
    text_bkg(2, y, buf, PAL_TEXT);
}

static void draw_card(uint8_t level, uint8_t practice)
{
    const GbLevel *lv = &gbc_levels[level];
    char buf[21], n[6], n12[12];
    uint8_t i;
    fill_bkg(0, 3, 20, 12, UT_BLANK, PAL_TEXT | 0x08);
    tile_bkg(0, 4, UT_ARROW_L, PAL_TEXT | 0x08);
    tile_bkg(19, 4, UT_ARROW_R, PAL_TEXT | 0x08);
    center_bkg(4, lv->name, PAL_TEXT);
    strcpy(buf, DIFF_NAMES[lv->difficulty < 6 ? lv->difficulty : 5]);
    strcat(buf, "  ");
    i = (uint8_t)strlen(buf);
    center_bkg(6, buf, PAL_TEXT);
    tile_bkg((uint8_t)((20 - i) / 2 + i - 1), 6, UT_STAR, PAL_TEXT | 0x08);
    fmt_uint(n, lv->stars);
    text_bkg((uint8_t)((20 - i) / 2 + i), 6, n, PAL_TEXT);
    {
        /* (a mask moving along rather than coins >> i: SDCC 4.2 got that
         * wrong here, shifting a register that didn't hold the coins) */
        uint8_t coins = g_save.progress.coins[lv->id], bit = 1;
        for (i = 0; i < lv->ncoins && i < 4; i++, bit <<= 1)
            tile_bkg((uint8_t)(10 - lv->ncoins + 2 * i + 1), 8, coins & bit ? UT_COIN_YES : UT_COIN_NO,
                     PAL_TEXT | 0x08);
    }
    percent_line(10, "NORMAL", g_save.progress.best[lv->id]);
    percent_line(11, "PRACTICE", g_save.progress.best_practice[lv->id]);
    strcpy(buf, "ATTEMPTS ");
    strcat(buf, fmt_ulong(n12, g_save.progress.attempts[lv->id]));
    center_bkg(13, buf, PAL_TEXT);
    center_bkg(14, practice ? "MODE: PRACTICE" : " MODE: NORMAL ", PAL_TEXT);
    pal_level(lv->pal, lv->pal, 255, 0, 8);
}

uint8_t select_screen(uint8_t *level, uint8_t *practice) BANKED
{
    uint8_t lv = *level, pr = *practice, redraw = 1;
    screen_begin(SCR_SELECT);
    center_bkg(1, "SELECT LEVEL", PAL_TEXT);
    ui_ground(20);
    video_on();
    music_ensure(SONG_MENU_GB);
    for (;;) {
        if (redraw) {
            draw_card(lv, pr);
            redraw = 0;
        }
        frame_wait();
        music_tick();
        if (g_pressed & J_LEFT) {
            lv = lv ? lv - 1 : GBC_LEVEL_COUNT - 1;
            redraw = 1;
            sfx_play(SFX_MOVE);
        } else if (g_pressed & J_RIGHT) {
            lv = (uint8_t)((lv + 1) % GBC_LEVEL_COUNT);
            redraw = 1;
            sfx_play(SFX_MOVE);
        } else if (g_pressed & J_SELECT) {
            pr ^= 1;
            redraw = 1;
            sfx_play(SFX_MOVE);
        } else if (g_pressed & (J_A | J_START)) {
            sfx_play(SFX_SELECT);
            *level = lv;
            *practice = pr;
            return 1;
        } else if (g_pressed & J_B) {
            sfx_play(SFX_BACK);
            *level = lv;
            return 0;
        }
    }
}
