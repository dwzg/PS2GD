/*
 * What a level shows besides the level: the progress bar's frame, the
 * attempt number, the pause menu and the results. In the menus' ROM bank
 * (bank 0, where the rest of play.c runs, is full).
 */
#pragma bank 15
#include <string.h>

#include "gbc.h"

static void center_win(uint8_t y, const char *s)
{
    text_win((uint8_t)((20 - strlen(s)) / 2), y, s, PAL_TEXT);
}

/* The window from screen line y down, cleared: rows of 8 lines. Left
 * hidden: drawing it takes more than a frame, and play.c shows it in a
 * vertical blank once all is drawn (g_lcdc_on). */
static void overlay(uint8_t y, uint8_t rows)
{
    fill_win(0, 0, 20, rows, UT_BLANK, PAL_TEXT | 0x08);
    WY_REG = y;
}

void ui_ground(uint8_t width) BANKED
{
    uint8_t x;
    for (x = 0; x < width; x++) {
        uint8_t sep = (x & 3) == 0;
        tile_bkg(x, 16, T_GROUND_TOP + sep, PAL_GROUND);
        tile_bkg(x, 17, T_GROUND_LOW + sep, PAL_GROUND);
        tile_bkg(x, 18, T_GROUND_LOW + sep, PAL_GROUND); /* (shown when the screen shakes) */
    }
}

void ui_hud_init(void) BANKED
{
    uint8_t i;
    fill_bkg(0, 0, 20, 1, UT_BLANK, PAL_HUD | 0x08);
    tile_bkg(2, 0, UT_BAR_L, PAL_HUD | 0x08);
    for (i = 0; i < 12; i++) tile_bkg(3 + i, 0, UT_BAR, PAL_HUD | 0x08);
    tile_bkg(15, 0, UT_BAR_R, PAL_HUD | 0x08);
}

/* Text in white, in the level's background map (so it scrolls with the
 * level) from map column x (wrapping round the map) on map row y. */
static void sky_text(uint8_t x, uint8_t y, const char *s)
{
    uint8_t j;
    for (; *s; s++, x++) {
        uint8_t tile = UT_SKYFONT;
        for (j = 0; SKYFONT_CHARS[j]; j++)
            if (SKYFONT_CHARS[j] == *s) tile = UT_SKYFONT + j;
        tile_bkg((uint8_t)(x & 31), y, tile, PAL_YP | 0x08);
    }
}

/* "ATTEMPT 12" in the sky over the start (columns 2.., row 4) */
/* The title over the demo run, in the window's map (shown above line 80):
 * the version, the logo, the menu. */
void ui_title(uint8_t sel) BANKED
{
    static char ver[21];
    uint8_t n = (uint8_t)strlen(gbc_version);
    fill_win(0, 0, 20, 10, UT_BLANK, PAL_TEXT | 0x08);
    /* in the top right corner; never past the screen's 20 columns */
    if (n > 20) n = 20;
    memcpy(ver, gbc_version, n);
    ver[n] = 0;
    text_win((uint8_t)(20 - n), 0, ver, PAL_TEXT);
    video_logo(1, 1);
    text_win(4, 9, "PLAY", PAL_TEXT);
    text_win(11, 9, "GARAGE", PAL_TEXT);
    ui_title_menu(sel);
}

/* a diamond before PLAY (0) or GARAGE (1) */
void ui_title_menu(uint8_t sel) BANKED
{
    tile_win(3, 9, sel ? UT_BLANK : UT_DIAMOND, PAL_TEXT | 0x08);
    tile_win(10, 9, sel ? UT_DIAMOND : UT_BLANK, PAL_TEXT | 0x08);
}

void ui_attempt(uint16_t attempts) BANKED
{
    char buf[16] = "ATTEMPT ";
    fmt_uint(buf + 8, attempts);
    sky_text(2, 4, buf);
}

/* "NEW BEST 57%" from map column x, on row 2 (above the attempt) */
void ui_new_best(uint8_t pc, uint8_t x) BANKED
{
    char buf[16] = "NEW BEST ";
    fmt_uint(buf + 9, pc);
    strcat(buf, "%");
    sky_text(x, 2, buf);
}

/* (when the level begins: a pause only shows it) */
void ui_pause(uint8_t practice) BANKED
{
    overlay(48, 12);
    center_win(1, "PAUSED");
    text_win(4, 4, "A      RESUME", PAL_TEXT);
    text_win(4, 6, "SELECT RESTART", PAL_TEXT);
    text_win(4, 8, "B      QUIT", PAL_TEXT);
    if (practice) center_win(10, "B: CHECKPOINT");
}

void ui_results(uint8_t practice, uint16_t attempts, uint16_t jumps, uint16_t secs, uint8_t ncoins,
                uint8_t coins) BANKED
{
    char buf[21], n[6];
    uint8_t i;
    overlay(40, 13);
    center_win(1, practice ? "PRACTICE COMPLETE!" : "LEVEL COMPLETE!");
    strcpy(buf, "ATTEMPTS  ");
    strcat(buf, fmt_uint(n, attempts));
    text_win(3, 4, buf, PAL_TEXT);
    strcpy(buf, "JUMPS     ");
    strcat(buf, fmt_uint(n, jumps));
    text_win(3, 5, buf, PAL_TEXT);
    strcpy(buf, "TIME      ");
    strcat(buf, fmt_uint(n, secs / 60));
    strcat(buf, ":");
    if (secs % 60 < 10) strcat(buf, "0");
    strcat(buf, fmt_uint(n, secs % 60));
    text_win(3, 6, buf, PAL_TEXT);
    if (ncoins) {
        uint8_t bit = 1; /* (not coins >> i: see draw_card in menu.c) */
        text_win(3, 8, "COINS", PAL_TEXT);
        for (i = 0; i < ncoins && i < 4; i++, bit <<= 1)
            tile_win(13 + 2 * i, 8, coins & bit ? UT_COIN_YES : UT_COIN_NO, PAL_TEXT | 0x08);
    }
    center_win(11, "A: CONTINUE");
}
