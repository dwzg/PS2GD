/*
 * The player's progress, and what an attempt counts for: when attempts
 * count, how far into a level a run got, what a best, a finish and coins
 * are. The same rules on every platform (progress.c is integer C that the
 * Game Boy builds too); how a platform shows them and stores them is its
 * own (save.h, src/gbc/save.c).
 */
#ifndef PD_PROGRESS_H
#define PD_PROGRESS_H

#include "portable.h"

#define PROGRESS_LEVELS 16

typedef struct {
    uint8_t best[PROGRESS_LEVELS];          /* best normal-mode percent */
    uint8_t best_practice[PROGRESS_LEVELS]; /* best practice percent */
    uint8_t coins[PROGRESS_LEVELS];         /* bitmask of coins collected */
    uint32_t attempts[PROGRESS_LEVELS];     /* normal-mode attempts started */
    uint32_t total_jumps;
    uint32_t total_attempts;
} Progress;

/* How far x (16.16 blocks) is into a level `width` columns long, in whole
 * percent (rounded down, 0..100). */
uint8_t progress_percent(int32_t x, uint16_t width) PD_BANKED;
/* x[k] (k = 0..100) = the first x at which progress_percent says k. */
void progress_steps(uint32_t *x, uint16_t width) PD_BANKED;

/* An attempt starts (only normal-mode ones count). */
void progress_attempt(Progress *p, uint8_t level, uint8_t practice) PD_BANKED;

/* The attempt ended short of the finish at pc percent (counted as 99 at
 * most) after `jumps` jumps. Returns the new best if the normal-mode best
 * went up (to show), else 0; the practice best goes up silently. */
uint8_t progress_death(Progress *p, uint8_t level, uint8_t practice, uint8_t pc, uint16_t jumps) PD_BANKED;

/* The level was finished with `coins` (bitmask) after `jumps` jumps.
 * Returns the coins collected for the first time (coins only count in
 * normal mode); *first: 1 if it is the first normal-mode finish. */
uint8_t progress_complete(Progress *p, uint8_t level, uint8_t practice, uint8_t coins, uint16_t jumps,
                          uint8_t *first) PD_BANKED;

/* A run left from the pause menu (restart, quit) counts as a death once it
 * has run this many ticks: a quick restart is free. */
#define PROGRESS_LEFT_TICKS 30

#endif
