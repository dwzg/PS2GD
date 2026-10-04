/*
 * Pulse Dash for the Game Boy Color.
 *
 * The same six levels, physics and songs as the other versions, drawn with
 * the Game Boy's tiles (a block is 8x8 pixels, so the whole height of a
 * level fits the screen) and played on its four sound channels. Needs a
 * Game Boy Color: it runs the CPU at double speed and uses the colour
 * palettes and second bank of video memory.
 */
#include "gbc.h"

uint8_t g_keys, g_pressed;
uint16_t g_frame;
uint8_t g_screen;
uint8_t g_perf_ly, g_perf_ly_max;
/* The emulator test plays levels by setting this (from a hook on
 * input_update) to the buttons for the tick about to be played; 0xff:
 * read the pad. */
uint8_t g_test_keys = 0xff;
static uint8_t s_prev_keys;

void input_update(void)
{
    uint8_t k = g_test_keys == 0xff ? joypad() : g_test_keys;
    g_pressed = (uint8_t)(k & ~s_prev_keys);
    g_keys = k;
    s_prev_keys = k;
}

void frame_wait(void)
{
    /* how far into the frame the work got: the scanline it ended on */
    uint8_t ly = LY_REG;
    g_perf_ly = ly;
    if (ly < 144 && ly > g_perf_ly_max) g_perf_ly_max = ly;
    wait_vbl_done();
    /* the pad first, at the same moment of every frame */
    input_update();
    video_vblank();
    g_frame++;
}

/* On a Game Boy or Game Boy Pocket: say what it needs, in the font. */
static void dmg_message(void)
{
    static const char *const LINES[] = {"PULSE DASH", "", "THIS GAME NEEDS A", "GAME BOY COLOR."};
    uint8_t i;
    display_off();
    SWITCH_ROM_MBC5(GBC_BANK_GFX);
    set_sprite_data(0, 64, gfx_ui);
    BGP_REG = 0xfc; /* colour 0 white, the rest black */
    for (i = 0; i < 18; i++) fill_bkg(0, i, 20, 1, 0, 0);
    for (i = 0; i < 4; i++) {
        const char *s = LINES[i];
        uint8_t n = 0, x;
        while (s[n]) n++;
        for (x = 0; x < n; x++) set_vram_byte((uint8_t *)0x9800 + (6 + i) * 32 + (20 - n) / 2 + x, (uint8_t)(s[x] - 32));
    }
    LCDC_REG = LCDCF_ON | LCDCF_BG8000 | LCDCF_BG9800 | LCDCF_BGON;
    for (;;) wait_vbl_done();
}

void main(void)
{
    uint8_t level = 0, practice = 0;
    if (_cpu != CGB_TYPE) dmg_message();
    cpu_fast();
    music_init();
    save_load();
    video_init();
    for (;;) {
        title_screen();
        while (select_screen(&level, &practice)) play_level(level, practice);
    }
}
