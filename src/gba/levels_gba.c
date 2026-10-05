/* The levels made at build time (levels_gba.h): level.c's LEVEL_PREBUILT hook. */
#include "levels_gba.h"
#include "game_internal.h"
#include "gen.h"

typedef char pre_levels_counted[PRE_LEVEL_COUNT >= 1 ? 1 : -1];

int level_is_prebuilt(const Level *L)
{
    return L >= g_pre_levels && L < g_pre_levels + PRE_LEVEL_COUNT;
}

const Level *level_prebuilt(const char *const *src)
{
    int k;
    for (k = 0; k < g_level_count && k < PRE_LEVEL_COUNT - 1; k++)
        if (g_levels[k].src == src) return &g_pre_levels[k];
    if (src == demo_level_src()) return &g_pre_levels[PRE_LEVEL_COUNT - 1];
    return 0;
}

typedef char demo_snaps_as_made[DEMO_SNAPS_EVERY == DEMO_SNAP_EVERY && DEMO_SNAP_BYTES == sizeof(DemoSnap) ? 1 : -1];

void levels_gba_init(void)
{
    /* the title's run needs to play nothing ahead */
    demo_take_snapshots((const DemoSnap *)(const void *)g_demo_snaps, DEMO_SNAPS);
}

const LevelArt *level_art(const Level *L)
{
    return L && level_is_prebuilt(L) ? &g_level_art[L - g_pre_levels] : 0;
}
