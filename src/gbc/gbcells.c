/*
 * The level the simulation plays, and the copy of the columns around the
 * player it reads them from (gbsim.h). In ROM bank 0 on the Game Boy: it
 * runs while the level's bank is mapped, then calls into the simulation's.
 */
#include "gbsim.h"

const uint8_t *gs_cells;
uint16_t gs_width;
uint8_t gs_height;
uint8_t gs_ring[GS_RING * GS_ROWS];
uint16_t gs_ring_obj[GS_RING], gs_ring_solid[GS_RING], gs_ring_block[GS_RING];

/* first column held in the ring; it holds columns lo .. lo + GS_RING - 1 */
static int16_t s_ring_lo = -32768;

static void copy_column(int16_t c)
{
    uint8_t *d = gs_ring + (((uint8_t)c & (GS_RING - 1)) << 4);
    uint16_t m = 0, sm = 0, bm = 0, bit = 1;
    uint8_t r;
    if (c < 0 || (uint16_t)c >= gs_width) {
        for (r = 0; r < GS_ROWS; r++) d[r] = T_EMPTY;
    } else {
        const uint8_t *s = gs_cells + ((uint16_t)c << 4);
        for (r = 0; r < GS_ROWS; r++, bit <<= 1) {
            d[r] = s[r];
            uint8_t k = GTI_KIND(gs_tile_info[s[r]]);
            if (k > GT_SLAB_HI) m |= bit;
            else if (k) sm |= bit;
            if (k == GT_BLOCK) bm |= bit;
        }
    }
    gs_ring_obj[(uint8_t)c & (GS_RING - 1)] = m;
    gs_ring_solid[(uint8_t)c & (GS_RING - 1)] = sm;
    gs_ring_block[(uint8_t)c & (GS_RING - 1)] = bm;
}

void gs_ring_reset(void)
{
    s_ring_lo = -32768;
}

void gs_ring_update(void)
{
    int16_t lo = (int16_t)(gs_p.x >> 16) - 3, c;
    if (lo == s_ring_lo) return;
    if (lo > s_ring_lo && lo - s_ring_lo < GS_RING) {
        for (c = s_ring_lo + GS_RING; c < lo + GS_RING; c++) copy_column(c);
    } else {
        for (c = lo; c < lo + GS_RING; c++) copy_column(c);
    }
    s_ring_lo = lo;
}

void gs_step(uint8_t held, uint8_t pressed)
{
    gs_ring_update();
    gs_tick(held, pressed);
}
