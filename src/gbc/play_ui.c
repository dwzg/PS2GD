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

/* The window from screen line y down, cleared: rows of 8 lines. */
static void overlay(uint8_t y, uint8_t rows)
{
    fill_win(0, 0, 20, rows, UT_BLANK, PAL_TEXT | 0x08);
    WY_REG = y;
    SHOW_WIN;
}

void ui_ground(uint8_t width) BANKED
{
    uint8_t x;
    for (x = 0; x < width; x++) {
        uint8_t sep = (x & 3) == 0;
        tile_bkg(x, 16, T_GROUND_TOP + sep, PAL_GROUND);
        tile_bkg(x, 17, T_GROUND_LOW + sep, PAL_GROUND);
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

/* "ATTEMPT 12" in the sky over the start (columns 2.., background row 4,
 * which scrolls away with the level), in white */
void ui_attempt(uint16_t attempts) BANKED
{
    char buf[16] = "ATTEMPT ";
    uint8_t i, j;
    fmt_uint(buf + 8, attempts);
    for (i = 0; buf[i]; i++) {
        uint8_t tile = UT_SKYFONT;
        for (j = 0; SKYFONT_CHARS[j]; j++)
            if (SKYFONT_CHARS[j] == buf[i]) tile = UT_SKYFONT + j;
        tile_bkg((uint8_t)((2 + i) & 31), 4, tile, PAL_YP | 0x08);
    }
}

void ui_new_best(uint8_t pc) BANKED
{
    char buf[16] = "NEW BEST ";
    fmt_uint(buf + 9, pc);
    strcat(buf, "%");
    overlay(128, 2); /* over the ground */
    center_win(0, buf);
}

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
        text_win(3, 8, "COINS", PAL_TEXT);
        for (i = 0; i < ncoins && i < 4; i++)
            tile_win(13 + 2 * i, 8, (coins >> i) & 1 ? UT_COIN_YES : UT_COIN_NO, PAL_TEXT | 0x08);
    }
    center_win(11, "A: CONTINUE");
}
