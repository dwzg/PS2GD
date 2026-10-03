/*
 * pd_tool - headless developer tool for Pulse Dash.
 *
 *   pd_tool check                       parse every level and song, report stats
 *   pd_tool solve <level|all> [K]       prove levels are beatable (inputs change
 *                                       at most every K ticks, all phases)
 *   pd_tool shot <level> <sec> <out.bmp> [practice]
 *                                       screenshot of the level at time sec,
 *                                       played by the solver
 *   pd_tool menu <title|select|garage|options> <out.bmp>
 *   pd_tool overview <level> <out.bmp>  whole-level map with the solver path
 *   pd_tool wav <song> <seconds> <out.wav>
 *   pd_tool smoke                       drive the full game through menus and a
 *                                       level with scripted input
 *   pd_tool rhythm <lvl|all> [tol] [x]  can the level be beaten pressing (in cube,
 *                                       ball and UFO) only on 8th notes of its
 *                                       song, tol ticks early or late (default 2)?
 *                                       (with x: only check up to that x)
*   pd_tool trace <lvl> x0 x1 [off]     player state along the solver's path (with
 *                                       off: the rhythm check's run at that offset)
 *   pd_tool ruler <lvl>                 print the level source with the beat grid
 *                                       (where the player is on each 8th note)
 *   pd_tool script <lvl> [off]          print the rhythm check's presses as an
 *                                       emulator harness script ("+frame:CROSS:n",
 *                                       frames at 59.94 Hz from the attempt start)
 *   pd_tool prof <lvl>                  play a level along the solver's path,
 *                                       rendering every tick into a null backend;
 *                                       prints primitives per frame (run it under
 *                                       valgrind --tool=callgrind for CPU costs)
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_sdl.h"
#include "../core/audio.h"
#include "../core/draw.h"
#include "../core/font.h"
#include "../core/game_internal.h"
#include "../core/platform.h"
#include "../core/songdata.h"

/* ------------------------------------------------------------------ */
/* platform stubs                                                      */
/* ------------------------------------------------------------------ */

int plat_save_read(void *buf, int size)
{
    (void)buf;
    (void)size;
    return -1;
}

int plat_save_write(const void *buf, int size)
{
    (void)buf;
    (void)size;
    return 0;
}

const char *plat_name(void) { return "TOOL"; }

/* ------------------------------------------------------------------ */
/* Solver                                                              */
/* ------------------------------------------------------------------ */

#define MAX_TICKS 20000
#define BEAM 20000
#define HSIZE 65536 /* > 2 * BEAM, power of two */

/*
 * Level sources: the built-in levels, plus one loaded from a text file (one
 * source line per line, a blank line between sections) when a command is
 * given a path instead of a level number. Used by tools that edit levels.
 */
#define FILE_LEVEL 1000
static const char **s_file_src;

static const char *const *level_src(int idx)
{
    return idx == FILE_LEVEL ? (const char *const *)s_file_src : g_levels[idx].src;
}

static void tool_level_info(int idx, LevelInfo *info)
{
    if (idx != FILE_LEVEL) {
        level_info(idx, info);
        return;
    }
    Level *L = level_parse(level_src(idx));
    memset(info, 0, sizeof(*info));
    memcpy(info->name, L->name, sizeof(info->name));
    info->song = L->song;
    info->ncoins = L->ncoins;
    level_free(L);
}

static int load_level_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        exit(2);
    }
    int n = 0, cap = 256;
    s_file_src = (const char **)malloc(sizeof(char *) * (size_t)cap);
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (n + 2 >= cap) {
            cap *= 2;
            s_file_src = (const char **)realloc(s_file_src, sizeof(char *) * (size_t)cap);
        }
        s_file_src[n++] = strdup(line);
    }
    s_file_src[n++] = "";
    s_file_src[n] = NULL;
    fclose(f);
    return FILE_LEVEL;
}

static const Level *s_L;
static int s_K, s_phase;
static long s_nodes;
static uint8_t s_sol[MAX_TICKS];
static uint8_t s_best_sol[MAX_TICKS]; /* inputs of the furthest attempt */
static float s_best_x;
static int s_need_coins; /* require these coins (bitmask) to count as solved */
static float s_coin_x[4];
/* Rhythm mode: in the tap modes (cube, ball, UFO) the button may only go
 * down on 8th-note ticks of the level's song (s_grid). */
static int s_rhythm;
static uint8_t s_grid[MAX_TICKS];
static float s_until_x; /* > 0: count reaching this x as solved (quick checks) */

typedef struct {
    Player p;
    uint8_t prev; /* button held on the previous tick */
} Node;

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

/* States closer than this quantization are treated as identical. */
static uint64_t state_key(const Player *p, int prev_held)
{
    uint64_t h = 1469598103934665603ULL;
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->y * 16.0f));
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->vy * 2.0f));
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->x * 16.0f));
    h = mix64(h, (uint64_t)(p->mode | ((p->grav + 1) << 4) | (p->grounded << 6) | (p->buf << 7) |
                            (p->speed_idx << 8) | (prev_held << 11) | ((uint64_t)p->coins << 12)));
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
    if (!s_need_coins) return 1;
    for (int i = 0; i < s_L->ncoins && i < 4; i++)
        if (((s_need_coins >> i) & 1) && !((p->coins >> i) & 1) && (p->done || p->x > s_coin_x[i] + 1.6f))
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
    if (!s_rhythm || !held || p->mode == MODE_SHIP || p->mode == MODE_WAVE) return coarse;
    return s_grid[t];
}

/* Marks the ticks of every 8th note of the level's song, shifted by offset. */
static void rhythm_grid(const Level *L, int offset)
{
    float step = 1800.0f / audio_song_bpm(SONG_FIRST_LEVEL + L->song); /* ticks per 8th */
    memset(s_grid, 0, sizeof(s_grid));
    for (int k = 0;; k++) {
        int t = (int)lroundf((float)k * step) + offset;
        if (t >= MAX_TICKS) break;
        if (t >= 0) s_grid[t] = 1;
    }
}

/*
 * Breadth-first beam search over inputs. Inputs may only change on ticks
 * where (tick - phase) % K == 0, which models a player reacting at 60/K Hz
 * (and in rhythm mode, presses in the tap modes only on the beat grid).
 * Returns ticks to finish, or -1.
 */
static int solve(const Level *L, int K, int phase, float *reached)
{
    s_L = L;
    s_K = K;
    s_phase = phase;
    for (int i = 0; i < L->nobjs; i++)
        if (L->objs[i].type == OBJ_COIN) s_coin_x[(L->objs[i].flags >> 4) & 3] = L->objs[i].cx + 0.5f;
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
    s_nodes = 0;
    s_best_x = 0.0f;
    memset(s_sol, 0, sizeof(s_sol));
    memset(s_best_sol, 0, sizeof(s_best_sol));

    int ncur = 1;
    sim_reset(&s_cur[0].p, L);
    s_cur[0].prev = 0;
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
                sim_tick(&n.p, L, held, held && !n.prev);
                n.prev = (uint8_t)held;
                s_nodes++;
                if (n.p.dead || !coins_ok(&n.p)) continue;
                if (n.p.done || (s_until_x > 0.0f && n.p.x >= s_until_x)) {
                    s_tmp_par[0] = (uint16_t)i;
                    s_tmp_inp[0] = (uint8_t)held;
                    result = t + 1;
                    break;
                }
                if (nnext >= BEAM || !hash_insert(state_key(&n.p, held))) continue;
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
            backtrack(t + 1, 0, s_sol);
            break;
        }
        for (int i = 0; i < nnext; i++)
            if (s_next[i].p.x > s_next[best_i].p.x) best_i = i;
        if (nnext > 0 && s_next[best_i].p.x > s_best_x) {
            s_best_x = s_next[best_i].p.x;
            backtrack(t + 1, best_i, s_best_sol);
        }
        Node *tmp = s_cur;
        s_cur = s_next;
        s_next = tmp;
        ncur = nnext;
    }
    if (reached) *reached = s_best_x;
    return result;
}

static int solve_level(int idx, int maxK, int verbose)
{
    Level *L = level_parse(level_src(idx));
    LevelInfo info;
    tool_level_info(idx, &info);
    printf("level %d \"%s\" width=%d objs=%d coins=%d\n", idx, info.name, L->width, L->nobjs, L->ncoins);
    int all_ok = 1;
    for (int K = 1; K <= maxK; K++) {
        int okc = 0;
        for (int ph = 0; ph < K; ph++) {
            float reached;
            int t = solve(L, K, ph, &reached);
            if (t >= 0) {
                okc++;
                if (verbose && K == 1) printf("  K=1: solved in %d ticks (%.1f s), %ld states\n", t, t / 60.0f, s_nodes);
                /* a portal the solution never touched means the level can be skipped */
                Player r;
                sim_reset(&r, L);
                int prev = 0;
                while (!r.done && !r.dead && r.ticks < MAX_TICKS) {
                    int h = s_sol[r.ticks];
                    sim_tick(&r, L, h, h && !prev);
                    prev = h;
                }
                for (int i = 0; i < L->nobjs; i++) {
                    const LevelObj *o = &L->objs[i];
                    if (o->type < OBJ_PORTAL_CUBE || o->type > OBJ_SPEED_3) continue;
                    if (!((r.used[o->id >> 5] >> (o->id & 31)) & 1)) {
                        printf("  WARNING K=%d phase=%d: portal type %d at x=%d y=%d was bypassed\n", K, ph, o->type,
                               o->cx, o->cy);
                        okc--;
                        break;
                    }
                }
            } else {
                printf("  K=%d phase=%d: FAILED, furthest x=%.1f (%d%%) states=%ld\n", K, ph, reached,
                       (int)(reached / L->end_x * 100), s_nodes);
            }
        }
        printf("  K=%d: %d/%d phases solvable\n", K, okc, K);
        if (okc < K) all_ok = 0;
    }
    level_free(L);
    return all_ok;
}

/* Fill s_sol with a solution (K=1) for level idx. Returns ticks or -1. */
static int get_solution(int idx)
{
    Level *L = level_parse(level_src(idx));
    int K = 3, t = -1;
    /* prefer coarse (human-like) inputs */
    for (K = 3; K >= 1 && t < 0; K--) t = solve(L, K, 0, NULL);
    level_free(L);
    return t;
}

/* ------------------------------------------------------------------ */
/* Offscreen rendering                                                 */
/* ------------------------------------------------------------------ */

static SDL_Surface *s_surf;
static SDL_Renderer *s_ren;

static void offscreen_init(int w, int h)
{
    s_surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    s_ren = SDL_CreateSoftwareRenderer(s_surf);
}

static void render_frame(const char *out)
{
    SDL_SetRenderDrawColor(s_ren, 0, 0, 0, 255);
    SDL_RenderClear(s_ren);
    gfx_sdl_begin(s_ren, (float)s_surf->w / SCREEN_W, (float)s_surf->h / SCREEN_H);
    game_render(1.0f);
    gfx_sdl_flush();
    SDL_RenderPresent(s_ren);
    const GfxStats *st = gfx_sdl_stats();
    if (SDL_SaveBMP(s_surf, out) != 0) fprintf(stderr, "save failed: %s\n", SDL_GetError());
    else printf("wrote %s (%d tris, %d quads, %d rects)\n", out, st->tris, st->quads, st->rects);
}

static int16_t s_abuf[2048 * 2];

static void tick_with_audio(uint32_t held)
{
    game_tick(held);
    audio_mix(s_abuf, AUDIO_RATE / TICK_HZ);
}

static void enter_play(int idx, int practice)
{
    g_game.sel_level = idx;
    g_game.start_practice = practice;
    g_game.screen = SCR_PLAY;
    g_game.fade = 0.0f;
    g_game.fading = 0;
    play_start(idx, practice);
}

static int cmd_shot(int idx, float sec, const char *out, int practice)
{
    if (get_solution(idx) < 0) printf("warning: no solution, playing with no input\n");
    game_init();
    enter_play(idx, practice);
    int ticks = (int)(sec * TICK_HZ);
    for (int t = 0; t < ticks; t++) {
        int tick = g_game.play.p.ticks;
        uint32_t held = (tick < MAX_TICKS && s_sol[tick]) ? BTN_CROSS : 0;
        tick_with_audio(held);
    }
    render_frame(out);
    return 0;
}

static int cmd_menu(const char *which, const char *out)
{
    game_init();
    int scr = SCR_TITLE;
    if (!strcmp(which, "select")) scr = SCR_SELECT;
    else if (!strcmp(which, "garage")) scr = SCR_GARAGE;
    else if (!strcmp(which, "options")) scr = SCR_OPTIONS;
    g_game.screen = scr;
    g_game.fade = 0.0f;
    g_game.fading = 0;
    g_game.save.best[0] = 100;
    g_game.save.coins[0] = 5;
    g_game.save.best[1] = 47;
    g_game.save.best_practice[1] = 82;
    for (int t = 0; t < 100; t++) tick_with_audio(0);
    if (scr == SCR_SELECT) {
        tick_with_audio(BTN_RIGHT);
        for (int t = 0; t < 40; t++) tick_with_audio(0);
    }
    render_frame(out);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Level overview                                                      */
/* ------------------------------------------------------------------ */

static void ov_rect(SDL_Renderer *r, int x, int y, int w, int h, Uint8 R, Uint8 G, Uint8 B)
{
    SDL_Rect rc = {x, y, w, h};
    SDL_SetRenderDrawColor(r, R, G, B, 255);
    SDL_RenderFillRect(r, &rc);
}

static int cmd_overview(int idx, const char *out)
{
    const int S = 6, ROWW = 220, ROWH = 24;
    Level *L = level_parse(level_src(idx));
    int sol_ticks = get_solution(idx);
    int rows = (L->width + ROWW - 1) / ROWW;
    int W = ROWW * S, H = rows * ROWH * S;
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surf);
    SDL_SetRenderDrawColor(r, 20, 24, 40, 255);
    SDL_RenderClear(r);
    for (int row = 0; row < rows; row++) {
        int oy = (row + 1) * ROWH * S - 2 * S; /* ground line */
        ov_rect(r, 0, oy, W, 2, 120, 140, 200);
        for (int c = 0; c < ROWW; c++) {
            int gx = row * ROWW + c;
            if (gx >= L->width) break;
            if (gx % 10 == 0) ov_rect(r, c * S, oy + 2, 1, S, 90, 90, 120);
            for (int y = 0; y < L->height && y < ROWH - 2; y++) {
                int t = L->grid[y * L->width + gx];
                if (t) ov_rect(r, c * S, oy - (y + 1) * S, S, t == OBJ_BLOCK ? S : S / 2, 200, 210, 230);
            }
        }
        for (int i = 0; i < L->nobjs; i++) {
            const LevelObj *o = &L->objs[i];
            int c = o->cx - row * ROWW;
            if (c < 0 || c >= ROWW) continue;
            int x = c * S, y = oy - (o->cy + 1) * S;
            Uint8 R = 255, G = 255, B = 255;
            int h = S;
            if (o->type <= OBJ_SAW_SMALL) { R = 255; G = 60; B = 60; }
            else if (o->type <= OBJ_ORB_GREEN) { R = 255; G = 220; B = 40; }
            else if (o->type <= OBJ_PAD_BLUE) { R = 255; G = 120; B = 220; }
            else if (o->type <= OBJ_SPEED_3) { R = 60; G = 255; B = 120; h = 3 * S; y -= S; }
            else { R = 255; G = 200; B = 0; }
            ov_rect(r, x + 1, y + 1, S - 2, h - 2, R, G, B);
        }
    }
    if (sol_ticks <= 0) {
        /* show how far the solver got */
        solve(L, 1, 0, NULL);
        memcpy(s_sol, s_best_sol, sizeof(s_sol));
    }
    {
        Player p;
        sim_reset(&p, L);
        int prev = 0;
        SDL_SetRenderDrawColor(r, 80, 255, 255, 255);
        while (!p.done && !p.dead && p.ticks < MAX_TICKS) {
            int h = s_sol[p.ticks];
            sim_tick(&p, L, h, h && !prev);
            prev = h;
            int row = (int)(p.x / ROWW);
            if (row >= rows) break;
            int oy = (row + 1) * ROWH * S - 2 * S;
            int x = (int)((p.x - row * ROWW) * S), y = (int)(oy - p.y * S);
            SDL_RenderDrawPoint(r, x, y);
            if (h) SDL_RenderDrawPoint(r, x, y + 1);
        }
        if (p.dead) {
            int row = (int)(p.x / ROWW);
            int oy = (row + 1) * ROWH * S - 2 * S;
            int x = (int)((p.x - row * ROWW) * S), y = (int)(oy - p.y * S);
            ov_rect(r, x - 4, y - 4, 9, 9, 255, 0, 255);
            printf("solver dies at x=%.2f y=%.2f\n", p.x, p.y);
        }
    }
    SDL_SaveBMP(surf, out);
    printf("wrote %s (%s)\n", out, sol_ticks > 0 ? "with path" : "no solution");
    level_free(L);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

static void put_le32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put_le16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static int cmd_wav(int song, float secs, const char *out)
{
    audio_init();
    audio_set_volume(8, 8);
    audio_play_song(song, 0.0f);
    int frames = (int)(secs * AUDIO_RATE);
    int16_t *buf = (int16_t *)malloc((size_t)frames * 4);
    audio_mix(buf, frames);
    double sum = 0;
    int peak = 0, clip = 0;
    for (int i = 0; i < frames * 2; i++) {
        int v = abs(buf[i]);
        if (v > peak) peak = v;
        if (v >= 32000) clip++;
        sum += (double)buf[i] * buf[i];
    }
    printf("song %d \"%s\": %.1fs, bars=%d, peak=%d rms=%.0f clipped=%d\n", song, audio_song_name(song), secs,
           audio_song_bars(song), peak, sqrt(sum / (frames * 2)), clip);
    FILE *f = fopen(out, "wb");
    if (!f) return 1;
    fwrite("RIFF", 1, 4, f);
    put_le32(f, 36 + (uint32_t)frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le32(f, 16);
    put_le16(f, 1);
    put_le16(f, 2);
    put_le32(f, AUDIO_RATE);
    put_le32(f, AUDIO_RATE * 4);
    put_le16(f, 4);
    put_le16(f, 16);
    fwrite("data", 1, 4, f);
    put_le32(f, (uint32_t)frames * 4);
    fwrite(buf, 4, (size_t)frames, f);
    fclose(f);
    free(buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Check                                                               */
/* ------------------------------------------------------------------ */

static int cmd_check(void)
{
    int bad = 0;
    audio_init();
    for (int s = 0; s < g_song_count; s++) {
        const SongDef *sd = g_songs[s];
        float len = audio_song_length(s);
        printf("song %d %-16s bpm=%5.1f bars=%3d length=%5.1fs\n", s, sd->title, sd->bpm, audio_song_bars(s), len);
        for (const PatternDef *pd = sd->patterns; pd && pd->name; pd++) {
            int st = audio_pattern_steps(pd->data, 0);
            if (st % 16) {
                printf("  pattern %s: %d steps (not a whole number of bars)\n", pd->name, st);
                bad++;
            }
        }
    }
    for (const PatternDef *pd = g_common_patterns; pd->name; pd++) {
        int drum = pd->name[0] == 'k' || pd->name[0] == 'c' || pd->name[0] == 's' || pd->name[0] == 'h';
        int st = audio_pattern_steps(pd->data, drum);
        if (st % 16) {
            printf("  common pattern %s: %d steps\n", pd->name, st);
            bad++;
        }
    }
    for (int i = 0; i < g_level_count; i++) {
        /* every row of a section should have the same width */
        const char *const *src = level_src(i);
        for (int k = 0; src[k];) {
            if (src[k][0] != '|' && src[k][0] != '!') { k++; continue; }
            int start = k, w = (int)strlen(src[k]);
            while (src[k] && (src[k][0] == '|' || src[k][0] == '!')) {
                if ((int)strlen(src[k]) != w) {
                    printf("level %d: ragged section starting at line %d (line %d has width %d, expected %d)\n", i,
                           start, k, (int)strlen(src[k]) - 1, w - 1);
                    bad++;
                    break;
                }
                k++;
            }
            while (src[k] && (src[k][0] == '|' || src[k][0] == '!')) k++;
        }
        Level *L = level_parse(level_src(i));
        LevelInfo info;
        tool_level_info(i, &info);
        /* estimated duration: walk speed portals along the ground path */
        float t = 0.0f, x = 0.0f;
        int sp = L->start_speed;
        for (int k = 0; k < L->nobjs; k++) {
            const LevelObj *o = &L->objs[k];
            if (o->type >= OBJ_SPEED_0 && o->type <= OBJ_SPEED_3) {
                t += (o->cx + 0.5f - x) / SIM_SPEEDS[sp];
                x = o->cx + 0.5f;
                sp = o->type - OBJ_SPEED_0;
            }
        }
        t += (L->end_x - x) / SIM_SPEEDS[sp];
        float slen = audio_song_length(SONG_FIRST_LEVEL + L->song);
        printf("level %d %-18s %-7s width=%4d height=%2d objs=%4d coins=%d est=%5.1fs song=%5.1fs%s\n", i,
               info.name, difficulty_name(info.difficulty), L->width, L->height, L->nobjs, L->ncoins, t, slen,
               t > slen ? "  (song loops)" : "");
        if (L->ncoins != 3) printf("  note: level has %d coins (expected 3)\n", L->ncoins);
        for (int k = 0; k < L->nobjs; k++) {
            const LevelObj *o = &L->objs[k];
            if (o->type == OBJ_SPIKE_UP && o->cy > 0 && !level_solid_at(L, o->cx, o->cy - 1))
                printf("  note: floating up-spike at x=%d row=%d\n", o->cx, o->cy);
            if (o->type == OBJ_SPIKE_DOWN && o->cy < 9 && !level_solid_at(L, o->cx, o->cy + 1))
                printf("  note: floating down-spike at x=%d row=%d\n", o->cx, o->cy);
            if (o->type >= OBJ_SPEED_0 && o->type <= OBJ_SPEED_3 && o->cy == 0)
                printf("  note: speed portal at x=%d sits on row 0 (a jump can skip it)\n", o->cx);
        }
        level_free(L);
    }
    return bad;
}

/* ------------------------------------------------------------------ */
/* Smoke test: menus -> level -> results -> menus                      */
/* ------------------------------------------------------------------ */

static int cmd_smoke(void)
{
    game_init();
    int fails = 0;
    for (int t = 0; t < 30; t++) tick_with_audio(0);
    /* title -> garage -> back */
    tick_with_audio(BTN_LEFT);
    tick_with_audio(0);
    tick_with_audio(BTN_CROSS);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_GARAGE) { printf("smoke: expected garage\n"); fails++; }
    tick_with_audio(BTN_RIGHT);
    tick_with_audio(0);
    tick_with_audio(BTN_CIRCLE);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_TITLE) { printf("smoke: expected title\n"); fails++; }
    /* title -> select -> play level 0 */
    tick_with_audio(BTN_RIGHT);
    tick_with_audio(0);
    tick_with_audio(BTN_CROSS);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_SELECT) { printf("smoke: expected select (menu_sel=%d)\n", g_game.menu_sel); fails++; }
    g_game.sel_level = 0;
    g_game.sel_scroll = 0;
    if (get_solution(0) < 0) { printf("smoke: level 0 unsolvable\n"); fails++; }
    tick_with_audio(BTN_CROSS);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_PLAY) { printf("smoke: expected play\n"); fails++; }
    /* die once on purpose (no input), then play the solution */
    int guard = 0;
    while (g_game.play.attempt < 2 && guard++ < 6000) tick_with_audio(0);
    guard = 0;
    while (g_game.play.phase != PH_COMPLETE && guard++ < 20000) {
        int tick = g_game.play.p.ticks;
        tick_with_audio(s_sol[tick] ? BTN_CROSS : 0);
    }
    if (g_game.play.phase != PH_COMPLETE) { printf("smoke: level not completed\n"); fails++; }
    for (int t = 0; t < 150; t++) tick_with_audio(0);
    tick_with_audio(BTN_CROSS);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_SELECT) { printf("smoke: expected select after results\n"); fails++; }
    if (g_game.save.best[0] != 100) { printf("smoke: best not recorded\n"); fails++; }
    /* practice mode with a checkpoint and pause menu */
    tick_with_audio(BTN_SQUARE);
    for (int t = 0; t < 60; t++) tick_with_audio(0);
    if (!g_game.play.practice) { printf("smoke: expected practice\n"); fails++; }
    for (int t = 0; t < 60; t++) tick_with_audio(t == 30 ? BTN_SQUARE : 0);
    if (g_game.play.ncp != 1) { printf("smoke: checkpoint not placed (%d)\n", g_game.play.ncp); fails++; }
    tick_with_audio(BTN_START);
    tick_with_audio(0);
    if (!g_game.play.paused) { printf("smoke: pause failed\n"); fails++; }
    tick_with_audio(BTN_DOWN); tick_with_audio(0);
    tick_with_audio(BTN_DOWN); tick_with_audio(0);
    tick_with_audio(BTN_DOWN); tick_with_audio(0);
    tick_with_audio(BTN_CROSS);
    for (int t = 0; t < 40; t++) tick_with_audio(0);
    if (g_game.screen != SCR_SELECT) { printf("smoke: exit from pause failed\n"); fails++; }
    printf("smoke: %s (attempts on level 0: %u)\n", fails ? "FAILED" : "ok", (unsigned)g_game.save.attempts[0]);
    return fails;
}

/* Can all coins (or the `want` mask) be collected in one run? */
static int coins_level(int idx, int want)
{
    Level *L = level_parse(level_src(idx));
    float reached;
    s_need_coins = want >= 0 ? want : (1 << L->ncoins) - 1;
    int t = solve(L, 1, 0, &reached);
    printf("level %d: all %d coins %s\n", idx, L->ncoins, t >= 0 ? "collectable" : "NOT collectable in one run");
    if (t < 0) {
        for (int c = 0; c < L->ncoins; c++) {
            s_need_coins = 1 << c;
            int tc = solve(L, 1, 0, &reached);
            printf("  coin %d at x=%.0f: %s\n", c, (double)s_coin_x[c], tc >= 0 ? "ok alone" : "UNREACHABLE");
        }
    }
    s_need_coins = 0;
    level_free(L);
    return t >= 0;
}

/* Rhythm check: solvable with on-beat presses at each offset in [-tol, tol]? */
static int rhythm_level(int idx, int tol)
{
    Level *L = level_parse(level_src(idx));
    LevelInfo info;
    tool_level_info(idx, &info);
    int ok = 1;
    printf("level %d \"%s\" (%.0f BPM): presses on 8th notes", idx, info.name,
           (double)audio_song_bpm(SONG_FIRST_LEVEL + L->song));
    for (int o = -tol; o <= tol; o += tol > 0 ? tol : 1) {
        float reached;
        rhythm_grid(L, o);
        s_rhythm = 1;
        int t = solve(L, 3, 0, &reached);
        s_rhythm = 0;
        if (t >= 0) printf(", %+d ticks ok", o);
        else {
            printf(", %+d ticks FAILED at x=%.1f", o, (double)reached);
            ok = 0;
        }
    }
    printf("\n");
    level_free(L);
    return ok;
}

/* Print the level's source sections with a beat ruler above each one. */
static int cmd_ruler(int idx)
{
    Level *L = level_parse(level_src(idx));
    if (get_solution(idx) < 0) printf("(no solution: ruler follows the furthest attempt)\n");
    /* column the player is in on every 8th note */
    static int8_t mark[4096];
    memset(mark, 0, sizeof(mark));
    float step = 1800.0f / audio_song_bpm(SONG_FIRST_LEVEL + L->song);
    Player p;
    sim_reset(&p, L);
    int prev = 0, k = 0;
    while (!p.done && !p.dead && p.ticks < MAX_TICKS) {
        while (lroundf((float)k * step) <= p.ticks) {
            int c = (int)p.x;
            if (c >= 0 && c < 4096 && !mark[c]) mark[c] = (int8_t)(k % 8 == 0 ? 'B' : (k % 2 == 0 ? '+' : '.'));
            if (k % 8 == 0 && c >= 0 && c < 4096) mark[c] = (int8_t)('0' + (k / 8) % 10);
            k++;
        }
        int h = s_sol[p.ticks];
        sim_tick(&p, L, h, h && !prev);
        prev = h;
    }
    int col = 0, in_sec = 0, width = 0;
    for (const char *const *ln = level_src(idx); *ln; ln++) {
        const char *l = *ln;
        int row = l[0] == '|' || l[0] == '!';
        if (row && !in_sec) {
            width = (int)strlen(l) - 1;
            printf("bar  ");
            for (int c = 0; c < width; c++) putchar(col + c < 4096 && mark[col + c] ? mark[col + c] : ' ');
            printf("   x=%d\n", col);
        }
        if (row) printf("     %s\n", l + 1);
        else if (in_sec) {
            col += width;
            printf("\n");
        }
        in_sec = row;
    }
    level_free(L);
    return 0;
}

static int cmd_script(int idx, int offset)
{
    Level *L = level_parse(level_src(idx));
    rhythm_grid(L, offset);
    s_rhythm = 1;
    int t = solve(L, 3, 0, NULL);
    s_rhythm = 0;
    level_free(L);
    if (t < 0) {
        fprintf(stderr, "no on-beat solution\n");
        return 1;
    }
    const float frames_per_tick = 60000.0f / 1001.0f / 60.0f;
    const char *sep = "";
    for (int k = 0; k < t;) {
        if (!s_sol[k]) {
            k++;
            continue;
        }
        int start = k;
        while (k < t && s_sol[k]) k++;
        int next = k;
        while (next < t && !s_sol[next]) next++;
        int f0 = (int)lroundf((float)start * frames_per_tick), f1 = (int)lroundf((float)k * frames_per_tick);
        int fn = next < t ? (int)lroundf((float)next * frames_per_tick) : f0 + 100;
        /* hold at least 4 frames so the pad poll sees it, but release before the next press */
        int len = f1 - f0 < 4 ? 4 : f1 - f0;
        if (f0 + len >= fn) len = fn - f0 - 1;
        printf("%s+%d:CROSS:%d", sep, f0, len > 0 ? len : 1);
        sep = ",";
    }
    printf("\n");
    return 0;
}

static int cmd_prof(int idx)
{
    if (get_solution(idx) < 0) printf("warning: no solution, playing with no input\n");
    game_init();
    enter_play(idx, 0);
    long sum = 0;
    int frames = 0, worst = 0;
    float worst_x = 0.0f;
    GfxStats worst_s = {0, 0, 0, 0};
    while (g_game.play.phase == PH_RUN && frames < 20000) {
        int tick = g_game.play.p.ticks;
        tick_with_audio((tick < MAX_TICKS && s_sol[tick]) ? BTN_CROSS : 0);
        gfx_sdl_begin(NULL, 1.0f, 1.0f);
        game_render(1.0f);
        gfx_sdl_flush();
        const GfxStats *st = gfx_sdl_stats();
        int n = st->tris + st->quads + st->rects;
        sum += n;
        if (n > worst) {
            worst = n;
            worst_x = g_game.play.p.x;
            worst_s = *st;
        }
        frames++;
    }
    printf("level %d: %d frames, %s, prims/frame avg %.0f, max %d at x=%.0f (tris %d quads %d rects %d, %d blend switches)\n",
           idx, frames, g_game.play.phase == PH_COMPLETE ? "completed" : "NOT completed", (double)sum / frames, worst,
           (double)worst_x, worst_s.tris, worst_s.quads, worst_s.rects, worst_s.blend_switches);
    return g_game.play.phase == PH_COMPLETE ? 0 : 1;
}

/* ------------------------------------------------------------------ */

static int level_arg(const char *s)
{
    if (strchr(s, '/') || strchr(s, '.')) return s_file_src ? FILE_LEVEL : load_level_file(s);
    int i = atoi(s);
    if (i < 0 || i >= g_level_count) {
        fprintf(stderr, "bad level %s\n", s);
        exit(2);
    }
    return i;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: pd_tool check|solve|shot|menu|overview|wav|smoke ...\n");
        return 2;
    }
    SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
    SDL_Init(0);
    draw_init();
    font_init();
    audio_init();
    const char *cmd = argv[1];
    if (!strcmp(cmd, "check")) return cmd_check() ? 1 : 0;
    if (!strcmp(cmd, "solve")) {
        int K = argc > 3 ? atoi(argv[3]) : 3;
        int fails = 0;
        if (argc < 3 || !strcmp(argv[2], "all")) {
            for (int i = 0; i < g_level_count; i++) fails += !solve_level(i, K, 1);
        } else {
            fails += !solve_level(level_arg(argv[2]), K, 1);
        }
        printf("%s\n", fails ? "SOME LEVELS NOT ROBUSTLY SOLVABLE" : "all levels solvable");
        return fails ? 1 : 0;
    }
    if (!strcmp(cmd, "coins") && argc >= 3) {
        /* can every coin be collected in a single run? */
        int fails = 0, want = argc > 3 ? atoi(argv[3]) : -1;
        if (strcmp(argv[2], "all")) fails += !coins_level(level_arg(argv[2]), want);
        else
            for (int i = 0; i < g_level_count; i++) fails += !coins_level(i, want);
        return fails ? 1 : 0;
    }
    if (!strcmp(cmd, "shot") && argc >= 5) {
        offscreen_init(argc > 6 ? atoi(argv[6]) : 1280, argc > 6 ? atoi(argv[6]) * 448 / 640 : 896);
        return cmd_shot(level_arg(argv[2]), (float)atof(argv[3]), argv[4], argc > 5 && atoi(argv[5]));
    }
    if (!strcmp(cmd, "menu") && argc >= 4) {
        offscreen_init(1280, 896);
        return cmd_menu(argv[2], argv[3]);
    }
    if (!strcmp(cmd, "overview") && argc >= 4) return cmd_overview(level_arg(argv[2]), argv[3]);
    if (!strcmp(cmd, "wav") && argc >= 5) return cmd_wav(atoi(argv[2]), (float)atof(argv[3]), argv[4]);
    if (!strcmp(cmd, "smoke")) return cmd_smoke() ? 1 : 0;
    if (!strcmp(cmd, "prof") && argc >= 3) return cmd_prof(level_arg(argv[2]));
    if (!strcmp(cmd, "ruler") && argc >= 3) return cmd_ruler(level_arg(argv[2]));
    if (!strcmp(cmd, "script") && argc >= 3) return cmd_script(level_arg(argv[2]), argc > 3 ? atoi(argv[3]) : 0);
    if (!strcmp(cmd, "rhythm") && argc >= 3) {
        int tol = argc > 3 ? atoi(argv[3]) : 2, fails = 0;
        s_until_x = argc > 4 ? (float)atof(argv[4]) : 0.0f;
        if (strcmp(argv[2], "all")) fails += !rhythm_level(level_arg(argv[2]), tol);
        else
            for (int i = 0; i < g_level_count; i++) fails += !rhythm_level(i, tol);
        printf("%s\n", fails ? "SOME LEVELS ARE OFF THE BEAT" : "all levels can be played on the beat");
        return fails ? 1 : 0;
    }
    if (!strcmp(cmd, "trace") && argc >= 5) {
        /* print the solver's player state between two x positions */
        int idx = level_arg(argv[2]);
        float x0 = (float)atof(argv[3]), x1 = (float)atof(argv[4]);
        if (argc > 5) {
            /* trace the rhythm check's run at this offset (furthest attempt if it fails) */
            Level *Lr = level_parse(level_src(idx));
            rhythm_grid(Lr, atoi(argv[5]));
            s_rhythm = 1;
            if (solve(Lr, 3, 0, NULL) < 0) memcpy(s_sol, s_best_sol, sizeof(s_sol));
            s_rhythm = 0;
            level_free(Lr);
        } else if (get_solution(idx) < 0) {
            printf("no solution; tracing furthest attempt\n");
            Level *Lb = level_parse(level_src(idx));
            solve(Lb, 1, 0, NULL);
            memcpy(s_sol, s_best_sol, sizeof(s_sol));
            level_free(Lb);
        }
        Level *L = level_parse(level_src(idx));
        Player p;
        sim_reset(&p, L);
        int prev = 0;
        while (!p.done && !p.dead && p.ticks < MAX_TICKS) {
            int h = s_sol[p.ticks];
            sim_tick(&p, L, h, h && !prev);
            prev = h;
            if (p.x >= x0 && p.x <= x1)
                printf("t=%4d x=%6.2f y=%6.2f vy=%6.2f mode=%d grav=%d grounded=%d held=%d floor=%.1f ceil=%.1f ev=%x\n",
                       p.ticks, p.x, p.y, p.vy, p.mode, p.grav, p.grounded, h, p.floor_y, p.ceil_y, p.events);
        }
        level_free(L);
        return 0;
    }
    fprintf(stderr, "unknown command\n");
    return 2;
}
