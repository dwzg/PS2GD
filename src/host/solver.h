/*
 * The level solver of the host tools (pd_tool, gbc_tool): a breadth-first
 * beam search over button inputs on the reference physics, src/core/sim.c,
 * which every platform plays exactly (see src/core/sim_rules.h). A level it
 * solves can be finished on all of them.
 */
#ifndef PD_SOLVER_H
#define PD_SOLVER_H

#include "../core/sim.h"

#define MAX_TICKS 20000

/*
 * Find inputs that finish the level. Inputs may only change on ticks where
 * (tick - phase) % K == 0, which models a player reacting at 60/K Hz (and in
 * rhythm mode, presses in the tap modes only on the beat grid). Returns the
 * ticks to finish, or -1; *reached (if not NULL): the furthest x any run got
 * to. The inputs, one byte per tick (1 = held), are then in g_sol, those of
 * the furthest run in g_best_sol.
 */
int solve(const Level *L, int K, int phase, float *reached);
extern uint8_t g_sol[MAX_TICKS];
extern uint8_t g_best_sol[MAX_TICKS];
extern float g_best_x;
extern long g_solve_nodes; /* states the last solve visited */

/* Count a run as solved only if it has these coins (bitmask) too;
 * g_coin_x: where each coin of the level is (set by solve). */
extern int g_solve_coins;
extern float g_coin_x[4];
/* > 0: count reaching this x as solved (quick checks). */
extern float g_solve_until_x;

/* Rhythm mode (g_solve_rhythm set): in the tap modes (cube, ball, UFO) the
 * button may only go down on the ticks marked in g_solve_grid, and an orb
 * only counts if the press began just before it (a tap, not a hold).
 * solve_rhythm_grid marks every 8th note at bpm, shifted by offset ticks. */
extern int g_solve_rhythm;
extern uint8_t g_solve_grid[MAX_TICKS];
void solve_rhythm_grid(float bpm, int offset);

#endif
