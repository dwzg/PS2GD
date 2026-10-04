/*
 * Title screen and level select (in a ROM bank of their own: they must not
 * switch banks themselves, video.c does that for them).
 */
#pragma bank 15
#include <string.h>

#include "gbc.h"

static const char *const DIFF_NAMES[] = {"EASY", "NORMAL", "HARD", "HARDER", "INSANE", "DEMON"};

static void center_bkg(uint8_t y, const char *s, uint8_t pal)
{
    text_bkg((uint8_t)((20 - strlen(s)) / 2), y, s, pal);
}

static void screen_begin(uint8_t screen)
{
    uint8_t i;
    g_screen = screen;
    video_off();
    for (i = 0; i < 40; i++) hide_sprite(i);
    HIDE_WIN;
    g_hud_split = 0;
    g_scx = 0;
    bkg_clear();
}

/* --- title --- */

/* a hop's height over 26 frames, in pixels */
static const uint8_t ARC[26] = {0, 3, 6, 8, 10, 12, 13, 14, 15, 16, 16, 17, 17, 17,
                                17, 16, 16, 15, 14, 13, 12, 10, 8, 6, 3, 0};

void title_screen(void) BANKED
{
    uint8_t jump = 0, hop = 0;
    uint16_t rot = 0;
    screen_begin(SCR_TITLE);
    video_logo(2);
    center_bkg(11, "GAME BOY COLOR", PAL_TEXT);
    ui_ground(20);
    text_bkg((uint8_t)(20 - strlen(gbc_version)), 0, gbc_version, PAL_TEXT);
    pal_menu(8);
    video_on();
    music_play(SONG_MENU_GB);

    for (;;) {
        frame_wait();
        music_tick();
        if (g_pressed & (J_START | J_A)) break;
        /* blinking prompt */
        if ((g_frame & 31) == 0) center_bkg(13, "PRESS START", PAL_TEXT);
        if ((g_frame & 31) == 20) fill_bkg(4, 13, 12, 1, UT_BLANK, PAL_TEXT | 0x08);
        /* a cube hopping on every other beat of the music */
        if (music_beat && !(hop++ & 1)) jump = 1;
        if (jump) {
            rot += 20;
            if (++jump > 25) {
                jump = 0;
                rot = (rot + 128) & 0xff00;
            }
        }
        {
            uint8_t f = (uint8_t)(((uint16_t)(rot & 255) * 6 + 128) >> 8);
            uint8_t h = jump ? ARC[jump] : 0;
            if (f >= CUBE_FRAMES) f = 0;
            set_sprite_tile(0, ST_CUBE + 4 * f);
            set_sprite_tile(1, ST_CUBE + 4 * f + 2);
            set_sprite_prop(0, OPAL_PLAYER | 0x08);
            set_sprite_prop(1, OPAL_PLAYER | 0x08);
            move_sprite(0, 80, (uint8_t)(128 - 4 - h + 8));
            move_sprite(1, 88, (uint8_t)(128 - 4 - h + 8));
        }
    }
    sfx_play(SFX_SELECT);
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
    char buf[21], n[6];
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
    for (i = 0; i < lv->ncoins && i < 4; i++)
        tile_bkg((uint8_t)(10 - lv->ncoins + 2 * i + 1), 8,
                 (g_save.coins[level] >> i) & 1 ? UT_COIN_YES : UT_COIN_NO, PAL_TEXT | 0x08);
    percent_line(10, "NORMAL", g_save.best[level]);
    percent_line(11, "PRACTICE", g_save.best_practice[level]);
    strcpy(buf, "ATTEMPTS ");
    strcat(buf, fmt_uint(n, g_save.attempts[level]));
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
