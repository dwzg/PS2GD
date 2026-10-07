/* A level's cells as BG1 shows them (cellmap.h). */
#include <string.h>

#include "cellmap.h"
#include "art.h"

static inline int floordiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

/* the glow of a block into cell (x, y), if that is in the level and not a
 * block (whose own picture has no room for it) */
static void glow_into(const Level *L, uint8_t *glow, int x, int y, int bit)
{
    if ((unsigned)x < (unsigned)L->width && (unsigned)y < (unsigned)L->height && L->grid[y * L->width + x] != OBJ_BLOCK)
        glow[y * L->width + x] |= (uint8_t)bit;
}

void cellmap_pictures(const Level *L, uint8_t *pic, uint8_t *glow)
{
    int x, y, i, w = L->width, h = L->height;
    memset(pic, 0, (size_t)w * h);
    memset(glow, 0, (size_t)w * h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int t = L->grid[y * w + x], e = L->edges[y * w + x], id = CELL_EMPTY;
            if (t == OBJ_BLOCK) {
                /* render_level's inner square, on one block in five */
                uint32_t hh = hash_u32((uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663));
                id = (hh % 5 == 0 ? CELL_BLOCK_DECOR : CELL_BLOCK) + (e & 15);
            } else if (t == OBJ_SLAB_LO || t == OBJ_SLAB_HI) {
                id = (t == OBJ_SLAB_LO ? CELL_SLAB_LO : CELL_SLAB_HI) + ((e & EDGE_L) ? 1 : 0) + ((e & EDGE_R) ? 2 : 0);
            }
            pic[y * w + x] = (uint8_t)id;
            /* render_level's glow: beyond each exposed edge of a block, and
             * round its corners where both sides are exposed; beyond a
             * slab's top or bottom where that is its cell's side (its
             * ends' glow, a half cell high, is left out) */
            if (t == OBJ_BLOCK) {
                if (e & EDGE_T) glow_into(L, glow, x, y + 1, G_BOT);
                if (e & EDGE_B) glow_into(L, glow, x, y - 1, G_TOP);
                if (e & EDGE_L) glow_into(L, glow, x - 1, y, G_RIGHT);
                if (e & EDGE_R) glow_into(L, glow, x + 1, y, G_LEFT);
                if ((e & EDGE_T) && (e & EDGE_L)) glow_into(L, glow, x - 1, y + 1, G_BR);
                if ((e & EDGE_T) && (e & EDGE_R)) glow_into(L, glow, x + 1, y + 1, G_BL);
                if ((e & EDGE_B) && (e & EDGE_L)) glow_into(L, glow, x - 1, y - 1, G_TR);
                if ((e & EDGE_B) && (e & EDGE_R)) glow_into(L, glow, x + 1, y - 1, G_TL);
            } else if (t == OBJ_SLAB_LO) {
                glow_into(L, glow, x, y - 1, G_TOP);
            } else if (t == OBJ_SLAB_HI) {
                glow_into(L, glow, x, y + 1, G_BOT);
            }
        }
    for (i = 0; i < L->nobjs; i++) {
        const LevelObj *o = &L->objs[i];
        int id;
        switch (o->type) {
        case OBJ_SPIKE_UP: id = CELL_SPIKE_UP; break;
        case OBJ_SPIKE_DOWN: id = CELL_SPIKE_DOWN; break;
        case OBJ_SPIKE_SM_UP: id = CELL_SPIKE_SM_UP; break;
        case OBJ_SPIKE_SM_DOWN: id = CELL_SPIKE_SM_DOWN; break;
        default: continue;
        }
        if (o->cx >= 0 && o->cx < w && o->cy >= 0 && o->cy < h) pic[o->cy * w + o->cx] = (uint8_t)id;
    }
}

int cellmap_tile(const uint8_t *pic, const uint8_t *glow, int w, int h, int tx, int ty, const uint16_t *cells,
                 const uint16_t *glowq, uint32_t out[8])
{
    const uint16_t *q[4];
    uint16_t lit[4][4]; /* quarters with the glow of blocks beside them */
    int i, any = 0;
    /* the tile's top left quarter: in cell (cx0, k0), quarter (rx0, ry0) of its 3x3 */
    int qx0 = tx * 2, qy0 = ty * 2;
    int cx0 = floordiv(qx0, 3), rx0 = qx0 - cx0 * 3, k0 = floordiv(qy0, 3), ry0 = qy0 - k0 * 3;
    for (i = 0; i < 4; i++) {
        int cx = cx0, rx = rx0 + (i & 1), k = k0, ry = ry0 + (i >> 1), cy, id = 0;
        if (rx == 3) {
            rx = 0;
            cx++;
        }
        if (ry == 3) {
            ry = 0;
            k++;
        }
        cy = -1 - k;
        q[i] = cells + (ry * 3 + rx) * 4;
        if ((unsigned)cx < (unsigned)w && (unsigned)cy < (unsigned)h) {
            int at = cy * w + cx, gm = glow[at];
            id = pic[at];
            q[i] += id * 36;
            if (gm) {
                /* the glow where the cell's picture has nothing */
                const uint16_t *gq = glowq + gm * 36 + (ry * 3 + rx) * 4;
                if (gq[0] | gq[1] | gq[2] | gq[3]) {
                    int r;
                    for (r = 0; r < 4; r++) {
                        uint32_t c = q[i][r], nz = c | c >> 1;
                        nz = (nz | nz >> 2) & 0x1111u;
                        lit[i][r] = (uint16_t)(c | (gq[r] & (nz ^ 0x1111u) * 15u));
                    }
                    q[i] = lit[i];
                    any = 1;
                }
            }
        }
        any |= id;
    }
    for (i = 0; i < 4; i++) out[i] = q[0][i] | (uint32_t)q[1][i] << 16;
    for (i = 0; i < 4; i++) out[4 + i] = q[2][i] | (uint32_t)q[3][i] << 16;
    if (!any) memset(out, 0, 32);
    return any;
}
