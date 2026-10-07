/*
 * The levels, made at build time for the Game Boy Advance (gba_tool
 * export): parsing a level's text takes the GBA four frames, and putting
 * its tiles together from the cells' quarters another few, so gba_tool
 * does both. For each level (and the title's demo run, last):
 *
 * - the parsed Level, as level_parse makes it (g_pre_levels; level.c's
 *   LEVEL_PREBUILT hook returns it instead of parsing);
 * - every 8x8 tile of its world (src/gba/cellmap.c, as BG1 shows it), the
 *   same ones once: its tiles (g_lvN_tiles) and a map of the world in them
 *   (g_lvN_map: an index, or 0xFFFF for an empty tile), tw tiles across
 *   from the level's start and th up from the ground's line.
 *
 * The ROM copies a level's tiles into VRAM when it is shown, and writes
 * the map's entries into BG1's as the camera moves (src/gba/world.c).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_gen.h"
#include "../core/level.h"
#include "../core/game_internal.h"
#include "../gba/cellmap.h"

/* the tiles BG1 has for a level (video.h: those under the shared ones) */
#define LEVEL_TILES_MAX 768
/* the GBA's DEMO_SNAP_EVERY (src/gba/target.h: the ROM checks they agree) */
#define GBA_DEMO_SNAP_EVERY 1

/* a set of distinct tiles, found again by a hash */
typedef struct {
    uint32_t (*t)[8];
    int n, cap;
    int *hash; /* (hsize slots: index + 1, 0 free) */
    int hsize;
} Tiles;

static uint32_t tile_hash(const uint32_t t[8])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 8; i++) h = (h ^ t[i]) * 16777619u;
    return h;
}

static int tiles_add(Tiles *s, const uint32_t t[8])
{
    uint32_t h = tile_hash(t) & (uint32_t)(s->hsize - 1);
    while (s->hash[h]) {
        if (!memcmp(s->t[s->hash[h] - 1], t, 32)) return s->hash[h] - 1;
        h = (h + 1) & (uint32_t)(s->hsize - 1);
    }
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->t = realloc(s->t, (size_t)s->cap * 32);
    }
    memcpy(s->t[s->n], t, 32);
    s->hash[h] = ++s->n;
    return s->n - 1;
}

static void emit_level(GbaGen *g, int k, const Level *L, const uint16_t *cells, const uint16_t *glowq, int *ok)
{
    int w = L->width, h = L->height, tw = (3 * w + 1) / 2, th = (3 * h + 1) / 2;
    uint8_t *pic = malloc((size_t)w * h), *glow = malloc((size_t)w * h);
    uint16_t *map = malloc((size_t)tw * th * 2);
    Tiles ts = {0};
    char sym[64];
    ts.hsize = 4096;
    ts.hash = calloc((size_t)ts.hsize, sizeof(int));
    cellmap_pictures(L, pic, glow);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            uint32_t t[8];
            /* (map row 0: the tile row just over the ground's line, ty = -1) */
            map[y * tw + x] = cellmap_tile(pic, glow, w, h, x, -1 - y, cells, glowq, t) ? (uint16_t)tiles_add(&ts, t)
                                                                                         : 0xFFFF;
        }
    printf("gba_tool: level %d (%s): %d tiles of %d (%dx%d)\n", k, L->name, ts.n, LEVEL_TILES_MAX, tw, th);
    if (ts.n > LEVEL_TILES_MAX) {
        fprintf(stderr, "gba_tool: level %d (%s) has %d different tiles, more than BG1's %d\n", k, L->name, ts.n,
                LEVEL_TILES_MAX);
        *ok = 0;
    }
    snprintf(sym, sizeof(sym), "g_lv%d_tiles", k);
    gba_gen_blob(g, sym, "uint32_t", ts.t, (size_t)ts.n * 32);
    snprintf(sym, sizeof(sym), "g_lv%d_map", k);
    gba_gen_blob(g, sym, "uint16_t", map, (size_t)tw * th * 2);
    /* the parsed level's arrays */
    snprintf(sym, sizeof(sym), "g_lv%d_grid", k);
    gba_gen_blob(g, sym, "uint8_t", L->grid, (size_t)w * h);
    snprintf(sym, sizeof(sym), "g_lv%d_edges", k);
    gba_gen_blob(g, sym, "uint8_t", L->edges, (size_t)w * h);
    snprintf(sym, sizeof(sym), "g_lv%d_objs", k);
    gba_gen_blob(g, sym, "uint8_t", L->objs, (size_t)L->nobjs * sizeof(LevelObj));
    snprintf(sym, sizeof(sym), "g_lv%d_cols", k);
    gba_gen_blob(g, sym, "uint8_t", L->col_start, (size_t)(w + 1) * sizeof(int));
    free(pic);
    free(glow);
    free(map);
    free(ts.t);
    free(ts.hash);
    /* (the tables: written after all the blobs) */
    fprintf(g->src, "    {g_lv%d_tiles, %d, g_lv%d_map, %d, %d},\n", k, ts.n, k, tw, th);
}

/* a C string literal */
static void c_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s < 32) fprintf(f, "\\%03o", (unsigned char)*s);
        else fputc(*s, f);
    }
    fputc('"', f);
}

static void emit_parsed(FILE *f, int k, const Level *L)
{
    fprintf(f, "    {.name = ");
    c_string(f, L->name);
    fprintf(f, ", .author = ");
    c_string(f, L->author);
    fprintf(f, ",\n     .song = %d, .difficulty = %d, .stars = %d, .start_speed = %d, .start_pal = %d,\n", L->song,
            L->difficulty, L->stars, L->start_speed, L->start_pal);
    fprintf(f, "     .width = %d, .height = %d, .grid = (uint8_t *)g_lv%d_grid, .edges = (uint8_t *)g_lv%d_edges,\n",
            L->width, L->height, k, k);
    fprintf(f, "     .objs = (LevelObj *)g_lv%d_objs, .nobjs = %d, .col_start = (int *)g_lv%d_cols,\n", k, L->nobjs, k);
    fprintf(f, "     .ninteract = %d, .ncoins = %d, .end_x = %a, .ntrig = %d, .trig = {", L->ninteract, L->ncoins,
            (double)L->end_x, L->ntrig);
    for (int i = 0; i < L->ntrig; i++) fprintf(f, "{%a, %d}, ", (double)L->trig[i].x, L->trig[i].pal);
    fprintf(f, "}},\n");
}

int gba_levels_export(GbaGen *g)
{
    const uint16_t *cells = gba_art_cells(), *glowq = gba_art_glow();
    int n = g_level_count, ok = 1;
    Level **L = calloc((size_t)n + 1, sizeof(Level *));
    for (int k = 0; k <= n; k++) {
        L[k] = level_parse(k < n ? g_levels[k].src : demo_level_src());
        if (!L[k]) return 1;
        if (k < n) {
            /* (level_info reads the parsed level on the GBA: as it reads the
             * text here) */
            LevelInfo in;
            level_info(k, &in);
            if (in.ncoins != L[k]->ncoins || in.stars != L[k]->stars || strcmp(in.name, L[k]->name)) {
                fprintf(stderr, "gba_tool: level %d's header reads differently parsed\n", k);
                ok = 0;
            }
        }
    }
    fprintf(g->src, "\n/* the levels (gba_levels.c): their tiles, then parsed */\n#include \"level.h\"\n#include \"art.h\"\n");
    fprintf(g->src, "const LevelArt g_level_art[%d] = {\n", n + 1);
    for (int k = 0; k <= n; k++) emit_level(g, k, L[k], cells, glowq, &ok);
    fprintf(g->src, "};\nconst Level g_pre_levels[%d] = {\n", n + 1);
    for (int k = 0; k <= n; k++) emit_parsed(g->src, k, L[k]);
    fprintf(g->src, "};\n");
    fprintf(g->hdr, "#define PRE_LEVEL_COUNT %d /* g_levels', then the demo run's */\n", n + 1);
    {
        /* the title's run's snapshots (demo.c), as the ROM would make them
         * playing ahead: the same run, here (gba_test checks the host's and
         * the ROM's physics agree tick for tick, Player for Player) */
        int ns = DEMO_LOOP / GBA_DEMO_SNAP_EVERY;
        DemoSnap *snap = calloc((size_t)ns, sizeof(DemoSnap));
        demo_record(snap, ns, GBA_DEMO_SNAP_EVERY);
        gba_gen_blob(g, "g_demo_snaps", "uint8_t", snap, (size_t)ns * sizeof(DemoSnap));
        fprintf(g->hdr, "#define DEMO_SNAPS %d\n#define DEMO_SNAPS_EVERY %d\n#define DEMO_SNAP_BYTES %d\n", ns,
                GBA_DEMO_SNAP_EVERY, (int)sizeof(DemoSnap));
        free(snap);
    }
    for (int k = 0; k <= n; k++) level_free(L[k]);
    free(L);
    return ok ? 0 : 1;
}
