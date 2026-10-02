/* Registry of the built-in levels (each defined in its own file). */
#include "../core/level.h"

extern const char *const LEVEL_NEON_STEPS[];
extern const char *const LEVEL_SKYWARD_PULSE[];
extern const char *const LEVEL_GRAVITY_GARDEN[];
extern const char *const LEVEL_SAUCER_GROOVE[];
extern const char *const LEVEL_WAVE_RIDER[];
extern const char *const LEVEL_PRISM_OVERDRIVE[];

const LevelEntry g_levels[] = {
    {LEVEL_NEON_STEPS},
    {LEVEL_SKYWARD_PULSE},
    {LEVEL_GRAVITY_GARDEN},
    {LEVEL_SAUCER_GROOVE},
    {LEVEL_WAVE_RIDER},
    {LEVEL_PRISM_OVERDRIVE},
};

const int g_level_count = (int)(sizeof(g_levels) / sizeof(g_levels[0]));
