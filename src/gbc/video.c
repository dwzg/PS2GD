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

volatile uint8_t g_scx, g_scy;
uint8_t g_hud_split, g_title_split;
uint8_t g_lcdc_on, g_lcdc_off;

/* palettes to upload (palette.c builds them) */
uint16_t g_bgpal[32];
uint16_t g_skypal[SKY_BANDS];
uint8_t g_sky_on;
/* the sky's bands as uploaded, and the band the next interrupt starts */
static uint16_t s_sky[SKY_BANDS];
static uint8_t s_band;
uint16_t g_objpal[24];
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
 * Waits for the horizontal blank so the next line starts with it.
 *
 * In a level, then at LY = 22, 37, .. 112: the sky's next band (its
 * colour 0 of the background palettes 0, 2..5; band 0 is written by the
 * vertical blank's interrupt, as those palettes aren't shown above the
 * level; on the title, band 4, and the interrupts from LY = 82). In
 * assembly, the five colours in the horizontal blank: palettes can't be
 * written while the LCD draws the line. */
static void lcd_isr(void) __naked
{
    __asm
    ld a, (_s_band)
    or a, a
    jr nz, 3$
1$:
    ldh a, (_STAT_REG + 0)
    and a, #3
    jr nz, 1$
    ld a, (_g_scx)
    ldh (_SCX_REG + 0), a
    ld a, (_g_scy)
    ldh (_SCY_REG + 0), a
    ld a, (_g_title_split)
    or a, a
    ld bc, #0x0116 ; a level: band 1 next, at line 22
    jr z, 2$
    ldh a, (_LCDC_REG + 0)
    and a, #0xf7 ; ~LCDCF_BG9C00
    ldh (_LCDC_REG + 0), a
    ld bc, #0x0552 ; the title (the level from line 80): band 5, at line 82
2$:
    ld a, (_g_sky_on)
    or a, a
    ret z
    ld a, b
    ld (_s_band), a
    ld a, c
    ldh (_LYC_REG + 0), a
    ret
3$:
    add a, a
    add a, #<_s_sky
    ld l, a
    ld a, #0
    adc a, #>_s_sky
    ld h, a
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld c, #0x68 ; BCPS, then BCPD
    ld a, #0x80 ; palette 0, colour 0 (the index can be set while the LCD draws)
    ldh (c), a
    ld hl, #0x9098 ; palettes 2, 3
4$:
    ldh a, (_STAT_REG + 0)
    and a, #3
    jr nz, 4$
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    dec c
    ld a, h
    ldh (c), a
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    dec c
    ld a, l
    ldh (c), a
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    dec c
    ld a, #0xa0
    ldh (c), a
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    dec c
    ld a, #0xa8
    ldh (c), a
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    ld hl, #_s_band ; the next band, 15 lines on
    inc (hl)
    ld a, (hl)
    cp a, #8 ; SKY_BANDS
    ret nc
    ldh a, (_LYC_REG + 0)
    add a, #15
    ldh (_LYC_REG + 0), a
    ret
    __endasm;
}

/* the sky's first band shown (in the vertical blank): band 0 from line 8,
 * on the title band 4 from line 80 */
static void sky_band0(void) __naked
{
    __asm
    ld hl, #_s_sky
    ld a, (_g_title_split)
    or a, a
    jr z, 3$
    ld hl, #_s_sky + 8
3$:
    ld a, (hl+)
    ld e, a
    ld d, (hl)
    ld c, #0x68
    ld b, #5
    ld hl, #2$
1$:
    ld a, (hl+)
    ldh (c), a
    inc c
    ld a, e
    ldh (c), a
    ld a, d
    ldh (c), a
    dec c
    dec b
    jr nz, 1$
    ret
2$:
    .db 0x80, 0x90, 0x98, 0xa0, 0xa8
    __endasm;
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
    if (s_band) {
        /* (the sky's bands moved it) */
        s_band = 0;
        LYC_REG = g_title_split ? 79 : 7;
    }
    /* The sky's first band, here and not in video_vblank, as the other
     * bands are in interrupts: a frame whose work runs past the next
     * vertical blank (drawing the pause menu or the results) has no
     * video_vblank in it, and the top of the sky would show the last
     * band's colour, the darkest, until there is one. */
    if (g_sky_on) sky_band0();
#ifdef PD_PERF
    s_perf_lines += 154;
#endif
    if (g_title_split) {
        LCDC_REG |= LCDCF_BG9C00;
        SCX_REG = 0;
    } else {
        SCX_REG = g_hud_split ? 0 : g_scx;
    }
    SCY_REG = 0;
}

/* Write a VRAM byte as soon as the LCD isn't reading it, with interrupts
 * off from the check to the write: an interrupt in between (a band of the
 * sky) can return late in the next line's mode 2, and with sprites on the
 * line it waited in, a real LCD can be drawing again by then, which loses
 * the write. */
static void vput(uint8_t *a, uint8_t v)
{
    for (;;) {
        disable_interrupts();
        if (!(STAT_REG & 2)) break;
        enable_interrupts();
    }
    *a = v;
    enable_interrupts();
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
        /* (not GBDK's return, which waits for the next horizontal blank
         * for a VRAM write the interrupt held up: most of a line for each
         * band; vput and vram_put keep interrupts off from their check to
         * their write instead) */
        add_LCD(nowait_int_handler);
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

/* the background (64 bytes) or sprite (48) palettes */
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
    ld hl, #_g_skypal ; and the sky bands, for the interrupt
    ld de, #_s_sky
    ld b, #16 ; SKY_BANDS * 2
2$:
    ld a, (hl+)
    ld (de), a
    inc de
    dec b
    jr nz, 2$
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
    ld b, #48
1$:
    ld a, (hl+)
    ldh (c), a
    dec b
    jr nz, 1$
    ret
    __endasm;
}

/* s_put_n bytes from s_put_src to VRAM at s_put_dst, s_put_step apart (1 or
 * 32), each when the LCD allows, as vput (a respawn's four columns run on
 * past the vertical blank, into the sky's interrupts) */
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
    di
    ldh a, (_STAT_REG + 0)
    and a, #2
    jr nz, 3$
    ld a, (de)
    ld (hl), a
    ei
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
3$:
    ei
    jr 1$
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
    if (g_lcdc_on | g_lcdc_off) {
        LCDC_REG = (LCDC_REG | g_lcdc_on) & ~g_lcdc_off;
        g_lcdc_on = g_lcdc_off = 0;
    }
    if (g_pal_dirty & 1) {
        bg_pal_upload();
        /* (over the sky's first band, which the interrupt wrote) */
        if (g_sky_on) sky_band0();
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
