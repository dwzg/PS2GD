/*
 * Building the palettes: a level's colours (blended while it changes
 * palette, flashing on the beat, faded in and out), the menus' and the
 * sprites'. In the menus' ROM bank with the colour tables (gen/pals.c).
 *
 * This runs during play (every other frame after a beat, and while the
 * level changes palette), so it is table driven: colours are kept as 5-bit
 * channels, each of the 32 background colours names the colour it shows, a
 * palette change is set up once and then stepped through the lerp table,
 * and the two loops a palette change runs are in assembly.
 */
#pragma bank 15
#include "gbc.h"

/* colour channel * k / 8, for fades */
static uint8_t s_fade[9][32];

/* The colours the palettes are made of, 5-bit RGB: the level's (blended),
 * the fixed ones, and the level's edge and line colours with the flash. */
#define SRC_FIXED LC_COUNT
#define SRC_EDGE_FLASH (LC_COUNT + FC_COUNT)
#define SRC_LINE_FLASH (SRC_EDGE_FLASH + 1)
#define SRC_COUNT (SRC_LINE_FLASH + 1)
static uint8_t s_src[SRC_COUNT][4]; /* r, g, b, unused: a 4-byte stride indexes without a multiply */

#define F(c) (SRC_FIXED + (c))
/* the colour each of the 8 x 4 background colours shows during a level */
static const uint8_t LEVEL_SRC[32] = {
    LC_SKY, LC_FILL, SRC_EDGE_FLASH, LC_SPIKE,                       /* world */
    LC_GROUND, LC_GROUND_DARK, SRC_LINE_FLASH, LC_SEP,               /* ground */
    LC_SKY, F(FC_YELLOW), F(FC_PINK), F(FC_WHITE),                   /* orbs, pads, portals */
    LC_SKY, F(FC_BLUE), F(FC_GREEN), F(FC_WHITE),
    LC_SKY, F(FC_ORANGE), F(FC_CYAN), F(FC_WHITE),
    LC_SKY, F(FC_GOLD), F(FC_DARK_GOLD), F(FC_WHITE),                /* coins */
    LC_HUD, F(FC_WHITE), F(FC_BAR), LC_HUD_DIM,                      /* progress bar */
    LC_SKY, F(FC_WHITE), F(FC_MENU_GOLD), F(FC_BLACK),               /* text over the level */
};
/* ... and in the menus (the ground and bar keep the level's colours) */
static const uint8_t MENU_SRC[32] = {
    F(FC_MENU), LC_FILL, LC_EDGE, LC_SPIKE,
    LC_GROUND, LC_GROUND_DARK, LC_LINE, LC_SEP,
    F(FC_MENU), F(FC_YELLOW), F(FC_PINK), F(FC_WHITE),
    F(FC_MENU), F(FC_BLUE), F(FC_GREEN), F(FC_WHITE),
    F(FC_MENU), F(FC_ORANGE), F(FC_CYAN), F(FC_WHITE),
    F(FC_MENU), F(FC_GOLD), F(FC_DARK_GOLD), F(FC_WHITE),
    LC_HUD, F(FC_WHITE), F(FC_BAR), LC_HUD_DIM,
    F(FC_MENU), F(FC_WHITE), F(FC_MENU_GOLD), F(FC_BLACK),
};

/* the blend the level colours were last computed for, and how the
 * palettes were last composed */
static uint8_t s_from = 0xff, s_to, s_t;
static uint8_t s_flash = 0xff, s_fade_done = 0xff;
static const uint8_t *s_map_done;

void pal_init(void) BANKED
{
    uint8_t k, c;
    for (k = 0; k <= 8; k++)
        for (c = 0; c < 32; c++) s_fade[k][c] = (uint8_t)((c * k) >> 3);
    for (k = 0; k < FC_COUNT; k++)
        for (c = 0; c < 3; c++) s_src[SRC_FIXED + k][c] = gbc_fixed_colors[k][c] >> 3;
}

static void put(uint8_t *out, const uint8_t *c, const uint8_t *ft)
{
    uint8_t r = ft[c[0]], g = ft[c[1]], b = ft[c[2]];
    out[0] = (uint8_t)(r | (g << 5));
    out[1] = (uint8_t)((g >> 3) | (b << 2));
}

/* compose() at full brightness, in assembly (three times as fast as
 * SDCC's code; a palette change does this every other frame): the 32
 * colours map names, from s_src, packed into g_bgpal. map in de. */
static void compose_full(const uint8_t *map) __naked
{
    map;
    __asm
    ld hl, #_g_bgpal
    ld c, #32
1$:
    ld a, (de)
    inc de
    push de
    add a, a
    add a, a
    add a, #<_s_src
    ld e, a
    ld a, #0
    adc a, #>_s_src
    ld d, a
    ld a, (de) ; red
    inc de
    ld b, a
    ld a, (de) ; green: its low 3 bits to bits 5..7, high 2 to bits 0..1
    inc de
    rrca
    rrca
    rrca
    push af
    and a, #0xe0
    or a, b
    ld (hl+), a
    ld a, (de) ; blue
    add a, a
    add a, a
    ld b, a
    pop af
    and a, #0x03
    or a, b
    ld (hl+), a
    pop de
    dec c
    jr nz, 1$
    ret
    __endasm;
}

static void compose(const uint8_t *map, uint8_t fade)
{
    const uint8_t *ft = s_fade[fade];
    uint8_t *out = (uint8_t *)g_bgpal;
    uint8_t i = 32;
    s_fade_done = fade;
    s_map_done = map;
    g_pal_dirty |= 1;
    if (fade == 8) {
        compose_full(map);
        return;
    }
    do {
        const uint8_t *c = s_src[*map++];
        uint8_t r = ft[c[0]], g = ft[c[1]], b = ft[c[2]];
        *out++ = (uint8_t)(r | (g << 5));
        *out++ = (uint8_t)((g >> 3) | (b << 2));
    } while (--i);
}

/* A palette change, set up when it starts: for each channel of the level
 * colours (and the 0 after each colour), how far it goes (bit 7: down) and
 * where from. blend_step() moves them to the point s_bl_lerp (a row of the
 * lerp table) says. Both in assembly: a change starts or steps every other
 * frame of play. */
static uint8_t s_bl[LC_COUNT * 4][2];
static uint8_t s_bl_n;
static const uint8_t *s_bl_lerp, *s_bl_a, *s_bl_b;

/* s_bl from the colours at s_bl_a and s_bl_b */
static void blend_start(void) __naked
{
    __asm
    ld hl, #_s_bl_a
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld hl, #_s_bl_b
    ld a, (hl+)
    ld c, a
    ld b, (hl)
    ld hl, #_s_bl
1$:
    ld a, (de) ; from
    inc de
    inc hl
    ld (hl-), a
    ld a, (bc) ; to
    inc bc
    inc hl
    sub a, (hl)
    dec hl
    jr nc, 2$
    cpl
    inc a
    or a, #0x80
2$:
    ld (hl+), a
    inc hl
    ld a, (_s_bl_n)
    dec a
    ld (_s_bl_n), a
    jr nz, 1$
    ret
    __endasm;
}

/* s_src[i][k] = start +- lerp[size], in assembly */
static void blend_step(void) __naked
{
    __asm
    ld bc, #_s_src
    ld de, #_s_bl
    ld hl, #_s_bl_lerp
    ld a, (hl+)
    ld h, (hl)
    ld l, a
1$:
    ld a, (de) ; size, bit 7: down
    inc de
    push hl
    bit 7, a
    jr nz, 2$
    add a, l
    ld l, a
    adc a, h
    sub a, l
    ld h, a
    ld l, (hl)
    ld a, (de) ; start
    inc de
    add a, l
    jr 3$
2$:
    and a, #0x7f
    add a, l
    ld l, a
    adc a, h
    sub a, l
    ld h, a
    ld l, (hl)
    ld a, (de)
    inc de
    sub a, l
3$:
    ld (bc), a
    inc bc
    pop hl
    ld a, (_s_bl_n)
    dec a
    ld (_s_bl_n), a
    jr nz, 1$
    ret
    __endasm;
}

static void flashed(uint8_t dst, uint8_t src, uint8_t flash)
{
    uint8_t k;
    for (k = 0; k < 3; k++) {
        uint8_t c = s_src[src][k];
        s_src[dst][k] = (uint8_t)(c + gbc_lerp5[flash << 1][31 - c]); /* flash of 16 */
    }
}

void pal_level(uint8_t from, uint8_t to, uint8_t t, uint8_t flash, uint8_t fade) BANKED
{
    if (from == s_from && to == s_to && t == s_t && fade == s_fade_done && s_map_done == LEVEL_SRC) {
        /* only the beat's flash changed: the two colours it lights up */
        if (flash != s_flash) {
            const uint8_t *ft = s_fade[fade];
            flashed(SRC_EDGE_FLASH, LC_EDGE, flash);
            flashed(SRC_LINE_FLASH, LC_LINE, flash);
            s_flash = flash;
            put((uint8_t *)&g_bgpal[PAL_WORLD * 4 + 2], s_src[SRC_EDGE_FLASH], ft);
            put((uint8_t *)&g_bgpal[PAL_GROUND * 4 + 2], s_src[SRC_LINE_FLASH], ft);
            g_pal_dirty |= 1;
        }
        return;
    }
    if (from != s_from || to != s_to || t != s_t) {
        /* the lerp table's row 0 is all 0 and row 32 the identity */
        if (from != s_from || to != s_to) {
            s_bl_a = gbc_level_pals[from].c[0];
            s_bl_b = gbc_level_pals[to].c[0];
            s_bl_n = LC_COUNT * 4;
            blend_start();
        }
        s_bl_lerp = gbc_lerp5[(uint8_t)((t + 4) >> 3)];
        s_bl_n = LC_COUNT * 4;
        blend_step();
        s_from = from;
        s_to = to;
        s_t = t;
        s_flash = 0xff;
    }
    if (flash != s_flash) {
        flashed(SRC_EDGE_FLASH, LC_EDGE, flash);
        flashed(SRC_LINE_FLASH, LC_LINE, flash);
        s_flash = flash;
    }
    compose(LEVEL_SRC, fade);
}

void pal_menu(uint8_t fade) BANKED
{
    pal_level(0, 0, 0, 0, fade);
    compose(MENU_SRC, fade);
}

void pal_sprites(void) BANKED
{
    uint8_t p, i;
    uint8_t *out = (uint8_t *)g_objpal;
    for (p = 0; p < 3; p++)
        for (i = 0; i < 4; i++) {
            uint8_t r = gbc_obj_pals[p][i][0] >> 3, g = gbc_obj_pals[p][i][1] >> 3, b = gbc_obj_pals[p][i][2] >> 3;
            *out++ = (uint8_t)(r | (g << 5));
            *out++ = (uint8_t)((g >> 3) | (b << 2));
        }
    g_pal_dirty |= 2;
}
