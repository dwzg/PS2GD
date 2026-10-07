#include "theme.h"

#define PAL(bt, bb, gr, gl, bf, be, ac) {bt, bb, gr, gl, bf, be, ac}

const Palette g_palettes[PALETTE_COUNT] = {
    /* 0 azure */
    PAL(RGB(44, 112, 244), RGB(16, 44, 150), RGB(24, 74, 210), RGB(235, 245, 255),
        RGBA(2, 8, 30, 185), RGB(225, 238, 255), RGB(120, 180, 255)),
    /* 1 violet */
    PAL(RGB(142, 64, 240), RGB(52, 14, 128), RGB(104, 40, 206), RGB(245, 230, 255),
        RGBA(16, 2, 34, 190), RGB(240, 225, 255), RGB(200, 150, 255)),
    /* 2 mint */
    PAL(RGB(28, 196, 142), RGB(6, 82, 70), RGB(14, 150, 108), RGB(225, 255, 240),
        RGBA(0, 24, 18, 190), RGB(220, 255, 238), RGB(140, 255, 200)),
    /* 3 sunset */
    PAL(RGB(255, 128, 52), RGB(170, 28, 72), RGB(214, 70, 52), RGB(255, 240, 220),
        RGBA(34, 4, 10, 190), RGB(255, 236, 214), RGB(255, 190, 120)),
    /* 4 crimson */
    PAL(RGB(176, 18, 34), RGB(40, 2, 8), RGB(120, 10, 22), RGB(255, 200, 200),
        RGBA(16, 0, 2, 205), RGB(255, 210, 210), RGB(255, 90, 90)),
    /* 5 ice */
    PAL(RGB(70, 214, 255), RGB(18, 100, 180), RGB(34, 160, 228), RGB(240, 255, 255),
        RGBA(0, 18, 40, 180), RGB(230, 252, 255), RGB(170, 240, 255)),
    /* 6 rose */
    PAL(RGB(255, 70, 170), RGB(118, 14, 86), RGB(210, 40, 140), RGB(255, 230, 245),
        RGBA(30, 0, 18, 190), RGB(255, 228, 244), RGB(255, 160, 220)),
    /* 7 night */
    PAL(RGB(24, 28, 70), RGB(4, 4, 16), RGB(18, 22, 60), RGB(110, 200, 255),
        RGBA(0, 0, 6, 215), RGB(130, 210, 255), RGB(90, 120, 255)),
    /* 8 gold */
    PAL(RGB(244, 186, 36), RGB(150, 70, 10), RGB(210, 130, 20), RGB(255, 250, 225),
        RGBA(30, 12, 0, 190), RGB(255, 246, 220), RGB(255, 230, 140)),
    /* 9 lime */
    PAL(RGB(150, 224, 40), RGB(40, 120, 16), RGB(100, 180, 26), RGB(245, 255, 225),
        RGBA(8, 24, 0, 190), RGB(240, 255, 220), RGB(210, 255, 140)),
};

void palette_lerp(Palette *out, const Palette *a, const Palette *b, float t)
{
    /* the ends as they are (col_lerp gives a at 0 and b at 1), not blended
     * colour by colour: the run's palette is one most of the time, and
     * without a floating point unit the blend costs a GBA 10 scanlines */
    if (t <= 0.0f) {
        *out = *a;
        return;
    }
    if (t >= 1.0f) {
        *out = *b;
        return;
    }
    out->bg_top = col_lerp(a->bg_top, b->bg_top, t);
    out->bg_bot = col_lerp(a->bg_bot, b->bg_bot, t);
    out->ground = col_lerp(a->ground, b->ground, t);
    out->ground_line = col_lerp(a->ground_line, b->ground_line, t);
    out->block_fill = col_lerp(a->block_fill, b->block_fill, t);
    out->block_edge = col_lerp(a->block_edge, b->block_edge, t);
    out->accent = col_lerp(a->accent, b->accent, t);
}

const Color g_player_colors[PLAYER_COLOR_COUNT] = {
    RGB(255, 204, 0),   RGB(0, 222, 255),  RGB(120, 255, 0),  RGB(255, 120, 0),
    RGB(255, 40, 70),   RGB(255, 80, 200), RGB(160, 80, 255), RGB(40, 100, 255),
    RGB(255, 255, 255), RGB(40, 40, 48),   RGB(0, 200, 150),  RGB(255, 170, 200),
    RGB(255, 240, 120), RGB(130, 220, 255),
};

Color difficulty_color(int d)
{
    static const Color c[6] = {RGB(90, 220, 255), RGB(90, 255, 120), RGB(255, 220, 60),
                               RGB(255, 140, 40), RGB(255, 70, 160), RGB(255, 40, 40)};
    return c[clampi(d, 0, 5)];
}

const char *const g_icon_names[ICON_COUNT] = {
    "CORE", "VISOR", "BUDDY", "SPLIT", "TARGET", "PLUS", "STRIPE", "GEM",
};
