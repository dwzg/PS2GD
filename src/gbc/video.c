/*
 * Palettes, tiles and the background map.
 *
 * VRAM can only be written outside the LCD's drawing mode. Work for the
 * screen is queued during the frame and done right after the vertical
 * blank starts (video_vblank); the text helpers used by menus write at any
 * time, each byte when the LCD allows it.
 */
#include <string.h>

#include "gbc.h"

volatile uint8_t g_scx;
uint8_t g_hud_split, g_title_split;

/* palettes to upload (palette.c builds them) */
uint16_t g_bgpal[32];
uint16_t g_objpal[12];
uint8_t g_pal_dirty;

/* queued map columns and cells */
#define COLQ 4
static uint8_t s_colq_n;
static uint8_t s_colq_x[COLQ];
static uint8_t s_colq_t[COLQ][COL_ROWS], s_colq_a[COLQ][COL_ROWS];
#define CELLQ 8
static uint8_t s_cellq_n;
static uint8_t s_cellq_x[CELLQ], s_cellq_y[CELLQ], s_cellq_t[CELLQ], s_cellq_a[CELLQ];

static uint8_t s_saw[SAW_FRAMES][16];
static uint8_t s_saw_pending = 0xff;

/* LY = 7: below the progress bar row, scroll the level (on the title,
 * LY = 79: below the logo and the menu, show the level's map, scrolled).
 * Waits for the horizontal blank so the next line starts with it. */
static void lcd_isr(void)
{
    while (STAT_REG & 3)
        ;
    SCX_REG = g_scx;
    if (g_title_split) LCDC_REG &= ~LCDCF_BG9C00;
}

volatile uint8_t g_vbl_count;

#ifdef PD_PERF
uint8_t g_perf[PERF_N];
static volatile uint16_t s_perf_lines; /* 154 a frame, from vblank */

/* scanlines since a fixed point (the vblank interrupt comes at line 144) */
uint16_t perf_now(void)
{
    uint16_t base;
    uint8_t ly;
    do {
        base = s_perf_lines;
        ly = LY_REG;
    } while (base != s_perf_lines);
    return base + (ly >= 144 ? ly - 144 : ly + 10);
}
#endif

static void vbl_isr(void)
{
    g_vbl_count++;
#ifdef PD_PERF
    s_perf_lines += 154;
#endif
    if (g_title_split) {
        LCDC_REG |= LCDCF_BG9C00;
        SCX_REG = 0;
    } else {
        SCX_REG = g_hud_split ? 0 : g_scx;
    }
}

/* Write a VRAM byte as soon as the LCD isn't reading it (if an interrupt
 * comes in between, GBDK's handler returns in a mode that allows it). */
static void vput(uint8_t *a, uint8_t v)
{
    while (STAT_REG & 2)
        ;
    *a = v;
}

void video_off(void)
{
    if (LCDC_REG & LCDCF_ON) display_off();
}

/* The screen black from the next frame, the LCD left on: a Game Boy
 * Color's LCD shows white while it is off. Every background palette goes
 * black and the sprites are hidden; what is drawn meanwhile is written as
 * the LCD allows (vput, vram_put), and the next screen's palettes go up
 * with its first frame (video_vblank). */
void video_blank(void)
{
    uint8_t k;
    if (!(LCDC_REG & LCDCF_ON)) return;
    pal_black();
    for (k = 0; k < 40; k++) hide_sprite(k);
    video_flush();
}

/* video_vblank's work in the next vertical blank (at once with the LCD off) */
void video_flush(void)
{
    if (LCDC_REG & LCDCF_ON) wait_vbl_done();
    video_vblank();
}

void video_on(void)
{
    LCDC_REG = LCDCF_ON | LCDCF_WIN9C00 | LCDCF_BG8000 | LCDCF_BG9800 | LCDCF_OBJ16 | LCDCF_OBJON | LCDCF_BGON |
               (LCDC_REG & LCDCF_WINON);
}

void video_init(void)
{
    uint8_t k;
    video_off();
    pal_init();

    /* tiles (tile 0 of bank 0 and of bank 1 at 0x8000; set_sprite_data
     * always writes there) */
    SWITCH_ROM_MBC5(GBC_BANK_GFX);
    VBK_REG = 0;
    set_sprite_data(0, GFX_BG_COUNT, gfx_bg);
    set_sprite_data(BT_LOGO, GFX_LOGO_COUNT, gfx_logo);
    VBK_REG = 1;
    set_sprite_data(0, 128, gfx_ui);
    set_sprite_data(128, 128, gfx_ui + 128 * 16);
    VBK_REG = 0;
    memcpy(s_saw, gfx_saw, sizeof(s_saw));

    bkg_clear();
    fill_win(0, 0, 20, 18, UT_BLANK, PAL_TEXT | 0x08);
    for (k = 0; k < 40; k++) hide_sprite(k);
    SCY_REG = 0;
    g_scx = 0;
    WX_REG = 7;
    WY_REG = 144;

    CRITICAL {
        add_VBL(vbl_isr);
        add_LCD(lcd_isr);
    }
    LYC_REG = 7;
    STAT_REG = 0x40; /* interrupt on LY == LYC */
    set_interrupts(VBL_IFLAG | LCD_IFLAG);
    pal_sprites();
    video_icon(g_save.icon);
}

void video_icon(uint8_t icon)
{
    uint8_t bank = _current_bank, *d = (uint8_t *)(0x8000 + ST_CUBE * 16);
    const uint8_t *s;
    uint16_t n = CUBE_FRAMES * 4 * 16;
    if (icon >= ICON_COUNT) icon = 0;
    SWITCH_ROM_MBC5(GBC_BANK_GFX);
    s = gfx_icons + (uint16_t)icon * (CUBE_FRAMES * 4 * 16);
    VBK_REG = 1;
    if (LCDC_REG & LCDCF_ON) {
        while (n--) vput(d++, *s++);
    } else {
        memcpy(d, s, n);
    }
    VBK_REG = 0;
    SWITCH_ROM_MBC5(bank);
}

void video_logo(uint8_t y0, uint8_t win)
{
    uint8_t x, y, bank = _current_bank;
    SWITCH_ROM_MBC5(GBC_BANK_GFX);
    for (y = 0; y < GFX_LOGO_H; y++)
        for (x = 0; x < GFX_LOGO_W; x++) {
            uint8_t t = gfx_logo_map[y * GFX_LOGO_W + x];
            if (win) tile_win(x, y0 + y, t, PAL_TEXT);
            else tile_bkg(x, y0 + y, t, PAL_TEXT);
        }
    SWITCH_ROM_MBC5(bank);
}

void bkg_clear(void)
{
    fill_bkg(0, 0, 32, 32, UT_BLANK, PAL_TEXT | 0x08);
}

/* --- the vblank's work --- */

/* The vertical blank is 10 scanlines: what is written then is written in
 * assembly (SDCC's loops took twice that, and past it every byte waits for
 * the LCD, and palette writes while it draws are lost). */

/* the background (64 bytes) or sprite (24) palettes */
static void bg_pal_upload(void) __naked
{
    __asm
    ld c, #0x68 ; BCPS, then BCPD
    ld a, #0x80
    ldh (c), a
    inc c
    ld hl, #_g_bgpal
    ld b, #64
1$:
    ld a, (hl+)
    ldh (c), a
    dec b
    jr nz, 1$
    ret
    __endasm;
}

static void obj_pal_upload(void) __naked
{
    __asm
    ld c, #0x6a ; OCPS, then OCPD
    ld a, #0x80
    ldh (c), a
    inc c
    ld hl, #_g_objpal
    ld b, #24
1$:
    ld a, (hl+)
    ldh (c), a
    dec b
    jr nz, 1$
    ret
    __endasm;
}

/* s_put_n bytes from s_put_src to VRAM at s_put_dst, s_put_step apart (1 or
 * 32), each when the LCD allows (as vput) */
static uint8_t *s_put_dst;
static const uint8_t *s_put_src;
static uint8_t s_put_n, s_put_step;

static void vram_put(void) __naked
{
    __asm
    ld hl, #_s_put_src
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld a, (_s_put_step)
    ld c, a
    ld a, (_s_put_n)
    ld b, a
    ld hl, #_s_put_dst
    ld a, (hl+)
    ld h, (hl)
    ld l, a
1$:
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr nz, 1$
    ld a, (de)
    ld (hl), a
    inc de
    ld a, l
    add a, c
    ld l, a
    jr nc, 2$
    inc h
2$:
    dec b
    jr nz, 1$
    ret
    __endasm;
}

static void put_col(uint8_t x, const uint8_t *t, const uint8_t *a)
{
    s_put_dst = (uint8_t *)0x9820 + x; /* map row 1 */
    s_put_src = t;
    s_put_n = COL_ROWS;
    s_put_step = 32;
    vram_put();
    VBK_REG = 1;
    s_put_src = a;
    vram_put();
    VBK_REG = 0;
}

void video_vblank(void)
{
    uint8_t i;
    if (g_pal_dirty & 1) {
        bg_pal_upload();
    } else if (g_pal_dirty & 4) {
        /* only the beat's flash: block edges, ground line (colour 2 of
         * palettes 0 and 1) */
        const uint8_t *p = (const uint8_t *)g_bgpal;
        BCPS_REG = 0x80 | 4;
        BCPD_REG = p[4];
        BCPD_REG = p[5];
        BCPS_REG = 0x80 | 12;
        BCPD_REG = p[12];
        BCPD_REG = p[13];
    }
    if (g_pal_dirty & 2) obj_pal_upload();
    g_pal_dirty = 0;
    if (s_saw_pending != 0xff) {
        s_put_dst = (uint8_t *)0x8000 + T_SAW * 16;
        s_put_src = s_saw[s_saw_pending];
        s_put_n = 16;
        s_put_step = 1;
        vram_put();
        s_saw_pending = 0xff;
    }
    for (i = 0; i < s_colq_n; i++) put_col(s_colq_x[i], s_colq_t[i], s_colq_a[i]);
    s_colq_n = 0;
    for (i = 0; i < s_cellq_n; i++) tile_bkg(s_cellq_x[i], s_cellq_y[i], s_cellq_t[i], s_cellq_a[i]);
    s_cellq_n = 0;
}

void col_queue(uint8_t mapx, const uint8_t *tiles, const uint8_t *attrs)
{
    if (s_colq_n >= COLQ) return;
    s_colq_x[s_colq_n] = mapx;
    memcpy(s_colq_t[s_colq_n], tiles, COL_ROWS);
    memcpy(s_colq_a[s_colq_n], attrs, COL_ROWS);
    s_colq_n++;
}

uint8_t col_queue_free(void) { return COLQ - s_colq_n; }
uint8_t cell_queue_free(void) { return CELLQ - s_cellq_n; }

void cell_queue(uint8_t mapx, uint8_t mapy, uint8_t tile, uint8_t attr)
{
    if (s_cellq_n >= CELLQ) return;
    s_cellq_x[s_cellq_n] = mapx;
    s_cellq_y[s_cellq_n] = mapy;
    s_cellq_t[s_cellq_n] = tile;
    s_cellq_a[s_cellq_n] = attr;
    s_cellq_n++;
}

void saw_frame(uint8_t f) { s_saw_pending = f; }

/* --- map and text --- */

static void put_tile(uint8_t *map, uint8_t x, uint8_t y, uint8_t tile, uint8_t attr)
{
    uint8_t *v = map + ((uint16_t)y << 5) + x;
    vput(v, tile);
    VBK_REG = 1;
    vput(v, attr);
    VBK_REG = 0;
}

void tile_bkg(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) { put_tile((uint8_t *)0x9800, x, y, tile, attr); }
void tile_win(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr) { put_tile((uint8_t *)0x9c00, x, y, tile, attr); }

static void fill(uint8_t *map, uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr)
{
    uint8_t i, j;
    for (j = 0; j < h; j++) {
        uint8_t *v = map + ((uint16_t)(y + j) << 5) + x;
        for (i = 0; i < w; i++) vput(v + i, tile);
        VBK_REG = 1;
        for (i = 0; i < w; i++) vput(v + i, attr);
        VBK_REG = 0;
    }
}

void fill_bkg(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr)
{
    fill((uint8_t *)0x9800, x, y, w, h, tile, attr);
}

void fill_win(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr)
{
    fill((uint8_t *)0x9c00, x, y, w, h, tile, attr);
}

static void text(uint8_t *map, uint8_t x, uint8_t y, const char *s, uint8_t pal)
{
    uint8_t *v = map + ((uint16_t)y << 5) + x;
    const char *p;
    uint8_t i;
    for (p = s, i = 0; *p; p++, i++) {
        char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        vput(v + i, (uint8_t)(c >= 32 && c < 96 ? c - 32 : 0) + UT_FONT);
    }
    VBK_REG = 1;
    for (p = s, i = 0; *p; p++, i++) vput(v + i, pal | 0x08);
    VBK_REG = 0;
}

void text_bkg(uint8_t x, uint8_t y, const char *s, uint8_t pal) { text((uint8_t *)0x9800, x, y, s, pal); }
void text_win(uint8_t x, uint8_t y, const char *s, uint8_t pal) { text((uint8_t *)0x9c00, x, y, s, pal); }

char *fmt_uint(char *buf, uint16_t v)
{
    /* by subtracting powers of ten: the CPU has no divide */
    static const uint16_t POW[4] = {10000, 1000, 100, 10};
    uint8_t i, n = 0;
    for (i = 0; i < 4; i++) {
        char d = '0';
        while (v >= POW[i]) {
            v -= POW[i];
            d++;
        }
        if (d != '0' || n) buf[n++] = d;
    }
    buf[n++] = (char)('0' + v);
    buf[n] = 0;
    return buf;
}
