/*
 * The level solver shared by the host tools: a breadth-first beam search
 * over button inputs, on the reference physics (src/core/sim.c), so what it
 * proves holds on every platform. See solver.h.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "solver.h"

#define BEAM 20000
#define HSIZE 65536 /* > 2 * BEAM, power of two */

static const Level *s_L;
static int s_K, s_phase;
long g_solve_nodes;
uint8_t g_sol[MAX_TICKS];
uint8_t g_best_sol[MAX_TICKS];
float g_best_x;
int g_solve_coins;
float g_coin_x[4];
int g_solve_rhythm;
uint8_t g_solve_grid[MAX_TICKS];
float g_solve_until_x;

typedef struct {
    Player p;
    uint8_t prev;     /* button held on the previous tick */
    uint16_t press_t; /* tick the current press started */
} Node;

/* In rhythm mode an orb only counts if the press began at most this many
 * ticks before it fired: a player taps on the orb rather than holding the
 * button through it. */
#define ORB_TAP_TICKS 6

static Node *s_cur, *s_next;
static uint16_t *s_par[MAX_TICKS]; /* parent index in the previous frontier */
static uint8_t *s_inp[MAX_TICKS];  /* input that led to this node */
static uint64_t s_hash[HSIZE];
static uint16_t s_tmp_par[BEAM];
static uint8_t s_tmp_inp[BEAM];

static uint64_t mix64(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdULL;
}

/* States closer than this quantization are treated as identical. `input`
 * holds the button (bit 0) and, in rhythm mode, whether the press is still
 * fresh enough to take an orb (bit 1). */
static uint64_t state_key(const Player *p, int input)
{
    uint64_t h = 1469598103934665603ULL;
    h = mix64(h, (uint64_t)(int64_t)((p->y + 2048) >> 12));
    h = mix64(h, (uint64_t)(int64_t)((p->vy + 64) >> 7));
    h = mix64(h, (uint64_t)(int64_t)((p->x + 2048) >> 12));
    h = mix64(h, (uint64_t)(p->mode | ((p->grav + 1) << 4) | (p->grounded << 6) | (p->buf << 7) |
                            (p->speed_idx << 8) | (input << 11) | ((uint64_t)p->coins << 13)));
    for (int i = 0; i < (s_L->ninteract + 31) / 32; i++) h = mix64(h, p->used[i]);
    return h | 1u;
}

static int hash_insert(uint64_t k)
{
    uint32_t i = (uint32_t)(k >> 24) & (HSIZE - 1);
    for (;;) {
        if (s_hash[i] == k) return 0;
        if (s_hash[i] == 0) {
            s_hash[i] = k;
            return 1;
        }
        i = (i + 1) & (HSIZE - 1);
    }
}

static int coins_ok(const Player *p)
{
    if (!g_solve_coins) return 1;
    for (int i = 0; i < s_L->ncoins && i < 4; i++)
        if (((g_solve_coins >> i) & 1) && !((p->coins >> i) & 1) && (p->done || sim_x(p) > g_coin_x[i] + 1.6f))
            return 0;
    return 1;
}

/* Write the input sequence ending at frontier node idx of tick t into dst. */
static void backtrack(int t, int idx, uint8_t *dst)
{
    for (int k = t; k > 0; k--) {
        dst[k - 1] = s_inp[k][idx];
        idx = s_par[k][idx];
    }
}

/* May the button change from prev to held on tick t? */
static int input_allowed(int t, const Player *p, int prev, int held)
{
    if (held == prev) return 1;
    int coarse = ((t - s_phase) % s_K + s_K) % s_K == 0;
    if (!g_solve_rhythm || !held || p->mode == MODE_SHIP || p->mode == MODE_WAVE) return coarse;
    return g_solve_grid[t];
}

void solve_rhythm_grid(float bpm, int offset)
{
    float step = 1800.0f / bpm; /* ticks per 8th */
    memset(g_solve_grid, 0, sizeof(g_solve_grid));
    for (int k = 0;; k++) {
        int t = (int)lroundf((float)k * step) + offset;
        if (t >= MAX_TICKS) break;
        if (t >= 0) g_solve_grid[t] = 1;
    }
}

/*
 * Breadth-first beam search over inputs. Inputs may only change on ticks
 * where (tick - phase) % K == 0, which models a player reacting at 60/K Hz
 * (and in rhythm mode, presses in the tap modes only on the beat grid).
 * Returns ticks to finish, or -1.
 */
int solve(const Level *L, int K, int phase, float *reached)
{
    s_L = L;
    s_K = K;
    s_phase = phase;
    for (int i = 0; i < L->nobjs; i++)
        if (L->objs[i].type == OBJ_COIN) g_coin_x[(L->objs[i].flags >> 4) & 3] = L->objs[i].cx + 0.5f;
    if (!s_cur) {
        s_cur = (Node *)malloc(sizeof(Node) * BEAM);
        s_next = (Node *)malloc(sizeof(Node) * BEAM);
    }
    for (int t = 0; t < MAX_TICKS; t++) {
        free(s_par[t]);
        free(s_inp[t]);
        s_par[t] = NULL;
        s_inp[t] = NULL;
    }
    g_solve_nodes = 0;
    g_best_x = 0.0f;
    memset(g_sol, 0, sizeof(g_sol));
    memset(g_best_sol, 0, sizeof(g_best_sol));

    int ncur = 1;
    sim_reset(&s_cur[0].p, L);
    s_cur[0].prev = 0;
    s_cur[0].press_t = 0;
    int result = -1;
    for (int t = 0; t < MAX_TICKS - 1 && ncur > 0; t++) {
        int nnext = 0;
        memset(s_hash, 0, sizeof(s_hash));
        s_par[t + 1] = s_tmp_par;
        s_inp[t + 1] = s_tmp_inp;
        int best_i = 0;
        for (int i = 0; i < ncur && result < 0; i++) {
            for (int held = 0; held < 2; held++) {
                if (!input_allowed(t, &s_cur[i].p, s_cur[i].prev, held)) continue;
                Node n = s_cur[i];
                if (held && !n.prev) n.press_t = (uint16_t)t;
                sim_tick(&n.p, L, held, held && !n.prev);
                n.prev = (uint8_t)held;
                g_solve_nodes++;
                if (n.p.dead || !coins_ok(&n.p)) continue;
                int fresh = g_solve_rhythm && held && t - n.press_t <= ORB_TAP_TICKS;
                if (g_solve_rhythm && (n.p.events & EV_ORB) && t - n.press_t > ORB_TAP_TICKS) continue;
                if (n.p.done || (g_solve_until_x > 0.0f && sim_x(&n.p) >= g_solve_until_x)) {
                    s_tmp_par[0] = (uint16_t)i;
                    s_tmp_inp[0] = (uint8_t)held;
                    result = t + 1;
                    break;
                }
                if (nnext >= BEAM || !hash_insert(state_key(&n.p, held | fresh << 1))) continue;
                s_tmp_par[nnext] = (uint16_t)i;
                s_tmp_inp[nnext] = (uint8_t)held;
                s_next[nnext++] = n;
            }
        }
        /* keep exactly-sized copies of this tick's parent links */
        int keep = result >= 0 ? 1 : nnext;
        s_par[t + 1] = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)(keep > 0 ? keep : 1));
        s_inp[t + 1] = (uint8_t *)malloc((size_t)(keep > 0 ? keep : 1));
        memcpy(s_par[t + 1], s_tmp_par, sizeof(uint16_t) * (size_t)keep);
        memcpy(s_inp[t + 1], s_tmp_inp, (size_t)keep);
        if (result >= 0) {
            backtrack(t + 1, 0, g_sol);
            break;
        }
        for (int i = 0; i < nnext; i++)
            if (s_next[i].p.x > s_next[best_i].p.x) best_i = i;
        if (nnext > 0 && sim_x(&s_next[best_i].p) > g_best_x) {
            g_best_x = sim_x(&s_next[best_i].p);
            backtrack(t + 1, best_i, g_best_sol);
        }
        Node *tmp = s_cur;
        s_cur = s_next;
        s_next = tmp;
        ncur = nnext;
    }
    if (reached) *reached = g_best_x;
    return result;
}
