/*
 * The levels as gba_tool made them (src/host/gba_levels.c): parsed, and
 * their tiles; level.c's LEVEL_PREBUILT hook (target.h) takes them from
 * here.
 */
#ifndef PD_GBA_LEVELS_GBA_H
#define PD_GBA_LEVELS_GBA_H

#include "level.h"
#include "art.h"

/* (gen.c: g_levels' order, the title's demo run last) */
extern const Level g_pre_levels[];
extern const LevelArt g_level_art[];

/* A level's tiles, if it is one of those (else NULL). */
const LevelArt *level_art(const Level *L);
/* At start-up: the title's run's snapshots, made by gba_tool. */
void levels_gba_init(void);

#endif
