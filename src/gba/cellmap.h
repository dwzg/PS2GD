/*
 * A level's cells as BG1 shows them (art.h): which picture each cell has
 * and the glow of the blocks beside it, and the 8x8 tiles they make. Used
 * by gba_tool, which makes every level's tiles at build time
 * (src/host/gba_levels.c), so that the ROM only copies them.
 */
#ifndef PD_GBA_CELLMAP_H
#define PD_GBA_CELLMAP_H

#include <stdint.h>
#include "../core/level.h"

/* Each cell's picture (art.h CELL_*) and the glow falling in it (G_*):
 * w * h each, row 0 at the bottom, as L->grid. */
void cellmap_pictures(const Level *L, uint8_t *pic, uint8_t *glow);

/* The world's tile (tx, ty): its top left at world pixel (8 tx, 8 ty), x
 * from the level's start, y down from the ground's line (ty < 0 above
 * it); put together from the cells' quarters (cells: g_cells, glowq:
 * g_glow) as 8 rows of 4-bit pixels. Returns 0 if it is empty. */
int cellmap_tile(const uint8_t *pic, const uint8_t *glow, int w, int h, int tx, int ty, const uint16_t *cells,
                 const uint16_t *glowq, uint32_t out[8]);

#endif
