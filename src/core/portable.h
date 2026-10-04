/*
 * What shared integer code (sim_rules.h, progress.c) needs to know about
 * the compiler it is built with. Only plain C and <stdint.h>: the Game
 * Boy's compiler, SDCC, builds this code too.
 */
#ifndef PD_PORTABLE_H
#define PD_PORTABLE_H

#include <stdint.h>

/* A function the Game Boy keeps in a ROM bank of its own, which callers
 * reach through a bank switch (GBDK's __banked); nothing elsewhere. */
#ifdef __SDCC
#define PD_BANKED __banked
#else
#define PD_BANKED
#endif

#endif
