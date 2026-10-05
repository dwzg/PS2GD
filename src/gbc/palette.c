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
#include <string.h>

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
static uint8_t s_flash = 0xff, s_fade_done = 0xff, s_white_done, s_blend_due;
uint8_t g_pal_split, g_pal_pending;
static const uint8_t *s_map_done;
uint8_t g_pal_white;

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
static uint16_t *s_cmp_out; /* where compose_full and compose_tab write, how many colours */
static uint8_t s_cmp_n;

static void compose_full(const uint8_t *map) __naked
{
    map;
    __asm
    ld hl, #_s_cmp_out
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, (_s_cmp_n)
    ld c, a
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

/* compose_full() with each channel through a table of 32 (faded, or
 * whitened): s_tab, 32 bytes in a 256-byte page (s_tab_buf's), its
 * address in s_tab_hi:s_tab_lo. */
static uint8_t s_tab_buf[64];
static uint8_t s_tab_lo, s_tab_hi, s_tab_n, s_tab_g;
static const uint8_t *s_tab_ft, *s_tab_wt;

/* s_tab[c] = f + s_tab_wt[31 - f], f = s_tab_ft[c]: faded, then whitened */
static void tab_build(void) __naked
{
    __asm
    ld a, (_s_tab_lo)
    ld e, a
    ld a, (_s_tab_hi)
    ld d, a
    ld hl, #_s_tab_ft
    ld a, (hl+)
    ld c, a
    ld b, (hl)
    ld a, #32
    ld (_s_tab_n), a
1$:
    ld a, (bc) ; f
    inc bc
    push af
    cpl
    add a, #32 ; 31 - f
    ld hl, #_s_tab_wt
    add a, (hl)
    inc hl
    ld h, (hl)
    ld l, a
    jr nc, 2$
    inc h
2$:
    pop af
    add a, (hl)
    ld (de), a
    inc de
    ld a, (_s_tab_n)
    dec a
    ld (_s_tab_n), a
    jr nz, 1$
    ret
    __endasm;
}

static void compose_tab(const uint8_t *map) __naked
{
    map;
    __asm
    ld a, (_s_tab_lo)
    ld c, a
    ld a, (_s_tab_hi)
    ld b, a
    ld hl, #_s_cmp_out
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, (_s_cmp_n)
    ld (_s_tab_n), a
1$:
    ld a, (de)
    inc de
    push de
    push hl
    add a, a
    add a, a
    add a, #<_s_src
    ld e, a
    ld a, #0
    adc a, #>_s_src
    ld d, a
    ld h, b
    ld a, (de) ; red
    inc de
    add a, c
    ld l, a
    ld a, (hl)
    push af
    ld a, (de) ; green
    inc de
    add a, c
    ld l, a
    ld a, (hl)
    ld (_s_tab_g), a
    ld a, (de) ; blue
    add a, c
    ld l, a
    ld a, (hl)
    add a, a
    add a, a
    ld e, a
    ld a, (_s_tab_g) ; green: its low 3 bits to bits 5..7, high 2 to bits 0..1
    rrca
    rrca
    rrca
    ld l, a
    and a, #0x03
    or a, e
    ld e, a
    ld a, l
    and a, #0xe0
    ld l, a
    pop af
    or a, l
    pop hl
    ld (hl+), a
    ld a, e
    ld (hl+), a
    pop de
    ld a, (_s_tab_n)
    dec a
    ld (_s_tab_n), a
    jr nz, 1$
    ret
    __endasm;
}

/* the sky's bands, for g_skypal */
static const uint8_t BAND_SRC[SKY_BANDS] = {LC_BAND, LC_BAND + 1, LC_BAND + 2, LC_BAND + 3,
                                            LC_BAND + 4, LC_BAND + 5, LC_BAND + 6, LC_BAND + 7};

/* The 32 colours of map into g_bgpal, a level's sky bands into g_skypal. */
static void compose(const uint8_t *map, uint8_t fade)
{
    uint8_t full = fade == 8 && !g_pal_white;
    s_fade_done = fade;
    s_white_done = g_pal_white;
    s_map_done = map;
    g_pal_dirty |= 1;
    if (!full) {
        uint8_t *t = s_tab_buf + ((uint8_t)(32 - (uint8_t)(uint16_t)s_tab_buf) & 31);
        s_tab_lo = (uint8_t)(uint16_t)t;
        s_tab_hi = (uint8_t)((uint16_t)t >> 8);
        /* (a death's flash: g_pal_white / 32 of the way to white) */
        s_tab_ft = s_fade[fade];
        s_tab_wt = gbc_lerp5[g_pal_white];
        tab_build();
    }
    s_cmp_out = g_bgpal;
    s_cmp_n = 32;
    if (full) compose_full(map);
    else compose_tab(map);
    if (map != LEVEL_SRC) return;
    s_cmp_out = g_skypal;
    s_cmp_n = SKY_BANDS;
    if (full) compose_full(BAND_SRC);
    else compose_tab(BAND_SRC);
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

/* The block edges and the ground line pulse with the beat. Their colours
 * are nearly white in every palette, so they rest 40% of the way to the
 * colour beside them (block fill, ground) and the beat brings them back:
 * flash 16 all the way, 0 not at all. */
static void flashed(uint8_t dst, uint8_t src, uint8_t beside, uint8_t flash)
{
    const uint8_t *rest = gbc_lerp5[13], *up = gbc_lerp5[flash << 1];
    uint8_t k;
    for (k = 0; k < 3; k++) {
        uint8_t c = s_src[src][k], b = s_src[beside][k], dim;
        dim = c >= b ? (uint8_t)(c - rest[c - b]) : (uint8_t)(c + rest[b - c]);
        s_src[dst][k] = c >= dim ? (uint8_t)(dim + up[c - dim]) : (uint8_t)(dim - up[dim - c]);
    }
}

void pal_level(uint8_t from, uint8_t to, uint8_t t, uint8_t flash, uint8_t fade) BANKED
{
    if (from == s_from && to == s_to && t == s_t && fade == s_fade_done && !g_pal_white && !s_white_done && !s_blend_due &&
        s_map_done == LEVEL_SRC) {
        /* only the beat's flash changed: the two colours it lights up */
        if (flash != s_flash) {
            const uint8_t *ft = s_fade[fade];
            flashed(SRC_EDGE_FLASH, LC_EDGE, LC_FILL, flash);
            flashed(SRC_LINE_FLASH, LC_LINE, LC_GROUND, flash);
            s_flash = flash;
            put((uint8_t *)&g_bgpal[PAL_WORLD * 4 + 2], s_src[SRC_EDGE_FLASH], ft);
            put((uint8_t *)&g_bgpal[PAL_GROUND * 4 + 2], s_src[SRC_LINE_FLASH], ft);
            g_pal_dirty |= 4; /* (video_vblank uploads just these two) */
        }
        return;
    }
    if (from != s_from || to != s_to || t != s_t || s_blend_due) {
        /* the lerp table's row 0 is all 0 and row 32 the identity */
        if (from != s_from || to != s_to) {
            s_bl_a = gbc_level_pals[from].c[0];
            s_bl_b = gbc_level_pals[to].c[0];
            s_bl_n = LC_COUNT * 4;
            blend_start();
            s_from = from;
            s_to = to;
            if (g_pal_split) {
                /* (the rest on the next call: together they took too
                 * long for a busy frame) */
                s_blend_due = 1;
                g_pal_pending = 1;
                return;
            }
        }
        s_blend_due = 0;
        s_bl_lerp = gbc_lerp5[(uint8_t)((t + 4) >> 3)];
        s_bl_n = LC_COUNT * 4;
        blend_step();
        s_from = from;
        s_to = to;
        s_t = t;
        s_flash = 0xff;
    }
    if (flash != s_flash) {
        flashed(SRC_EDGE_FLASH, LC_EDGE, LC_FILL, flash);
        flashed(SRC_LINE_FLASH, LC_LINE, LC_GROUND, flash);
        s_flash = flash;
    }
    compose(LEVEL_SRC, fade);
}

void pal_black(void) BANKED
{
    memset(g_bgpal, 0, sizeof(g_bgpal));
    memset(g_skypal, 0, sizeof(g_skypal));
    s_fade_done = 0xff; /* (the next palette is composed afresh) */
    g_pal_dirty |= 1;
}

void pal_menu(uint8_t fade) BANKED
{
    pal_level(0, 0, 0, 0, fade);
    compose(MENU_SRC, fade);
}

/* a 5-bit colour into a palette entry */
static void put5(uint8_t *out, const uint8_t *c)
{
    out[0] = (uint8_t)(c[0] | (c[1] << 5));
    out[1] = (uint8_t)((c[1] >> 3) | (c[2] << 2));
}

static const uint8_t *player_color(uint8_t k)
{
    return gbc_player_colors[k < PLAYER_COLOR_COUNT ? k : 0];
}

void pal_sprites(void) BANKED
{
    uint8_t p, i, c[3];
    uint8_t *out = (uint8_t *)g_objpal;
    /* a death's particles: each of the player's colours, the same half
     * way to white, and white (OPAL_COL1, OPAL_COL2), and white */
    for (p = 0; p < 2; p++) {
        const uint8_t *pc = player_color(p ? g_save.col2 : g_save.col1);
        for (i = 0; i < 3; i++) c[i] = (uint8_t)((pc[i] + 32) >> 1);
        put5(out + OPAL_COL1 * 8 + p * 8 + 2, pc);
        put5(out + OPAL_COL1 * 8 + p * 8 + 4, c);
    }
    c[0] = c[1] = c[2] = 31;
    for (i = 1; i < 4; i++) {
        put5(out + OPAL_COL1 * 8 + 6, c);
        put5(out + OPAL_COL2 * 8 + 6, c);
        put5(out + OPAL_WHITE * 8 + 2 * i, c);
    }
    for (p = 0; p < 3; p++)
        for (i = 0; i < 4; i++, out += 2) {
            /* the player and its effects in the colours of the garage */
            if (p < 2 && i == 1) {
                put5(out, player_color(g_save.col1));
            } else if ((p == 0 && i == 2) || (p == 1 && i == 3)) {
                put5(out, player_color(g_save.col2));
            } else {
                c[0] = gbc_obj_pals[p][i][0] >> 3;
                c[1] = gbc_obj_pals[p][i][1] >> 3;
                c[2] = gbc_obj_pals[p][i][2] >> 3;
                put5(out, c);
            }
        }
    g_pal_dirty |= 2;
}

void pal_garage(void) BANKED
{
    uint8_t p, i, c[3];
    uint8_t *out = (uint8_t *)g_bgpal;
    pal_menu(8);
    /* palette 0: the icons in the player's colours, 1..5 three colours
     * to choose from each (colour 0 is the menu's background) */
    for (p = 0; p < 6; p++) {
        out[0] = ((uint8_t *)g_bgpal)[PAL_TEXT * 8];
        out[1] = ((uint8_t *)g_bgpal)[PAL_TEXT * 8 + 1];
        for (i = 1; i < 4; i++) {
            uint8_t k = (uint8_t)(3 * (p - 1) + i - 1);
            if (!p) {
                if (i == 3) {
                    c[0] = gbc_obj_pals[0][3][0] >> 3;
                    c[1] = gbc_obj_pals[0][3][1] >> 3;
                    c[2] = gbc_obj_pals[0][3][2] >> 3;
                    put5(out + 2 * i, c);
                } else {
                    put5(out + 2 * i, player_color(i == 1 ? g_save.col1 : g_save.col2));
                }
            } else if (k < PLAYER_COLOR_COUNT) {
                put5(out + 2 * i, gbc_player_colors[k]);
            } else {
                out[2 * i] = out[0];
                out[2 * i + 1] = out[1];
            }
        }
        out += 8;
    }
    g_pal_dirty |= 1;
}
