/*
 * Text and menus on the GBA: BG0 as a drawing surface, 512x160 pixels (its
 * 64x32 tile map; screens use the first 240, the level select a card in
 * each half, slid by scrolling BG0). Colours are a palette bank and an
 * index (UI(bank, index)); a tile shows one bank, so what shares a tile
 * shares a bank (indices 1-3, the panels and outlines, are the same in all
 * of them: a tile's bank is that of its other colours; gba_test checks
 * that no tile mixes two banks). Each changed tile is packed (a tile of
 * one colour shared by all the tiles like it, any other a tile of its
 * own) and copied to VRAM in the vertical blank with the map, so nothing
 * shows half drawn. Text that moves or grows is drawn into sprite
 * tiles instead (ui_obj_text).
 */
#ifndef PD_GBA_UI_H
#define PD_GBA_UI_H

#include <stdint.h>
#include "common.h"

#define UI(bank, i) ((uint8_t)(((bank) << 4) | (i)))
#define UI_W 512 /* the surface's width */

/* BG palette banks for text (2..15; 0 and 1 are the world's) */
enum {
    UB_WHITE = 2, /* white to pale blue: titles, names */
    UB_GOLD,      /* yellow to orange: the logo, stars, NEW BEST */
    UB_GREEN,     /* the selected item, PRACTICE, COMPLETE */
    UB_GRAY,      /* items not selected */
    UB_BAR,       /* progress bars */
    UB_RED,       /* warnings (erase) */
    UB_DIFF,      /* the six difficulties' colours (4..9) */
    UB_SEL,       /* white to pale yellow: the title's chosen button */
    UB_HINT,      /* text whose colour pulses (UI_TEXT, set every frame) */
    UB_LIGHT,     /* the audio delay's lights (4..7, set every frame) */
    UB_COUNT
};
/* indices in every bank */
enum {
    UI_FILL = 1,  /* panel inside */
    UI_EDGE = 2,  /* panel border */
    UI_K = 3,     /* black: outlines, shadows */
    UI_G0 = 4,    /* 4..10: a gradient from the top of the letters down */
    UI_TEXT = 11, /* plain text */
    UI_DIM = 12,
    UI_X1 = 13,   /* 13..15: each bank's own */
    UI_X2 = 14,
    UI_X3 = 15,
};

enum { UI_LEFT = 0, UI_CENTER = 1, UI_RIGHT = 2 };

/* How text is coloured: each of the font's 7 rows, and its 1-pixel
 * outline (0: none). */
typedef struct {
    uint8_t row[7];
    uint8_t outline;
} TextStyle;

/* A bank's look: fancy (black outline, the gradient), or plain (UI_TEXT,
 * or `plain` if not 0). */
TextStyle ui_style(int bank, int fancy, uint8_t plain);

void ui_init(void);
/* Start a screen: everything gone, no scrolling, no dimming. */
void ui_clear(void);
void ui_erase(int x, int y, int w, int h);
/* Empty the rows of cells that pixel rows y0..y1 (excluded) touch, all
 * across: blank at once, nothing to pack (much quicker than erasing them
 * when they are full). */
void ui_clear_rows(int y0, int y1);
void ui_fill(int x, int y, int w, int h, uint8_t c);
/* n of them, each step pixels right of the one before (step > w: a row of
 * squares, at once) */
void ui_fill_n(int x, int y, int w, int h, int step, int n, uint8_t c);
/* render_panel: rounded corners, a border of `edge`, inside `fill` (0:
 * see-through, for a panel over the world darkened with ui_dim). */
void ui_panel(int x0, int y0, int x1, int y1, uint8_t fill, uint8_t edge);
/* Text in the core's font (5x7, 6 pixels a letter) at scale 1 or 2;
 * returns its width. ui_text: in a bank's look (ui_style). */
int ui_text_st(int x, int y, int scale, int align, const TextStyle *st, const char *s);
int ui_text(int x, int y, int scale, int align, int bank, int fancy, uint8_t plain, const char *s);
int ui_text_w(const char *s, int scale);
/* render_progress_bar: a black frame, a faint inside, filled to frac (0..256),
 * green (UB_BAR) or blue (UB_BAR + 16) */
void ui_bar(int x0, int y0, int x1, int y1, int frac, int bank);
/* BG0's scroll this frame (0 unless a screen slides it). */
void ui_scroll(int x);
/* A filled circle (r in pixels). */
void ui_disc(int cx, int cy, int r, uint8_t c);
/* Darken what is behind the rectangle (the world under a see-through panel),
 * level 0..16, this frame (two rectangles at most). */
void ui_dim(int x0, int y0, int x1, int y1, int level);
/* How much UB_HINT's plain text shows, 0..16 (a pulsing or fading hint):
 * white over the colour behind it, as the PC's see-through text. */
void ui_hint_level(int k, Color behind);
/* Pack what changed and queue it for the vertical blank; sets BG0's
 * registers and the dimming's. */
void ui_flush(void);
/* BG0 held: what is drawn from now on is packed as the frames have time,
 * but the screen goes on showing what it showed, until ui_show() or, with
 * until_packed, the first ui_flush that has packed all of it. A picture
 * that takes several frames to draw (a pause menu, a choice moving in it)
 * then appears whole, in one frame. ui_clear() ends a hold. */
void ui_hold(int until_packed);
/* The hold ended: BG0 shows what is packed from this frame on, and with
 * pack_all, all of it: what is left to pack is packed by this frame's
 * ui_flush however long that takes (for a few cells: ui_pending). */
void ui_show(int pack_all);
/* The cells drawn and not packed yet. */
int ui_pending(void);

/*
 * Text in sprites: drawn into OBJ tiles from `tile` (from OBJ_TEXT_TILE),
 * in pieces of 32x16 (8 tiles each), in the colours of a sprite palette
 * (the style's indices). The tiles are copied in the vertical blank.
 */
typedef struct {
    int tile, pieces, w, h;
} ObjText;
void ui_obj_text(ObjText *t, int tile, int scale, const TextStyle *st, const char *s);
/* Its sprites with their middle at cx and their top at y, in palette pal;
 * scaled by k/256 around its middle (k != 256: affine sprites). */
void ui_obj_text_show(const ObjText *t, int cx, int y, int pal, int k);

#endif
