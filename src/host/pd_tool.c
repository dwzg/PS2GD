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

#define VIS_BITS 23
#define VIS_SIZE (1u << VIS_BITS)
#define MAX_TICKS 20000

static uint64_t *s_vis;
static const Level *s_L;
static int s_K, s_phase;
static long s_nodes, s_node_limit = 30000000;
static uint8_t s_sol[MAX_TICKS];
static uint8_t s_best_sol[MAX_TICKS]; /* inputs of the furthest attempt */
static int s_best_x_tick;
static float s_best_x;
static int s_need_coins; /* require every coin to count as solved */

static uint64_t mix64(uint64_t h, uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    return h * 0xff51afd7ed558ccdULL;
}

static uint64_t state_key(const Player *p, int prev_held)
{
    uint64_t h = 1469598103934665603ULL;
    h = mix64(h, (uint64_t)p->ticks);
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->y * 64.0f));
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->vy * 8.0f));
    h = mix64(h, (uint64_t)(int64_t)lroundf(p->x * 16.0f));
    h = mix64(h, (uint64_t)(p->mode | ((p->grav + 1) << 4) | (p->grounded << 6) | (p->buf << 7) |
                            (p->speed_idx << 8) | (prev_held << 11) | ((uint64_t)p->coins << 12)));
    for (int i = 0; i < (s_L->ninteract + 31) / 32; i++) h = mix64(h, p->used[i]);
    return h | 1u;
}

/* Returns 1 if newly inserted. */
static int visit(uint64_t k)
{
    uint32_t i = (uint32_t)(k >> 20) & (VIS_SIZE - 1);
    for (;;) {
        if (s_vis[i] == k) return 0;
        if (s_vis[i] == 0) {
            s_vis[i] = k;
            return 1;
        }
        i = (i + 1) & (VIS_SIZE - 1);
    }
}

static int dfs(const Player *p, int prev_held)
{
    if (p->done) return !s_need_coins || p->coins == (uint8_t)((1u << s_L->ncoins) - 1u);
    if (p->dead) return 0;
    if (p->ticks >= MAX_TICKS - 64) return 0;
    if (p->x > s_best_x) {
        s_best_x = p->x;
        s_best_x_tick = p->ticks;
        memcpy(s_best_sol, s_sol, (size_t)p->ticks);
    }
    if (!visit(state_key(p, prev_held))) return 0;
    if (++s_nodes > s_node_limit) return 0;
    int seg = s_K - ((p->ticks + s_K - s_phase) % s_K);
    for (int choice = 0; choice < 2; choice++) {
        int held = choice;
        Player q = *p;
        int ph = prev_held;
        int t0 = q.ticks;
        for (int i = 0; i < seg && !q.dead && !q.done; i++) {
            sim_tick(&q, s_L, held, held && !ph);
            s_sol[t0 + i] = (uint8_t)held;
            ph = held;
        }
        if (dfs(&q, ph)) return 1;
    }
    return 0;
}

/* Returns ticks to finish, or -1. */
static int solve(const Level *L, int K, int phase, float *reached)
{
    s_L = L;
    s_K = K;
    s_phase = phase;
    s_nodes = 0;
    s_best_x = 0.0f;
    if (!s_vis) s_vis = (uint64_t *)calloc(VIS_SIZE, sizeof(uint64_t));
    else memset(s_vis, 0, VIS_SIZE * sizeof(uint64_t));
    memset(s_sol, 0, sizeof(s_sol));
    Player p;
    sim_reset(&p, L);
    int ok = dfs(&p, 0);
    if (reached) *reached = s_best_x;
    if (!ok) return -1;
    /* replay to measure */
    Player r;
    sim_reset(&r, L);
    int prev = 0;
    while (!r.done && !r.dead && r.ticks < MAX_TICKS) {
        int h = s_sol[r.ticks];
        sim_tick(&r, L, h, h && !prev);
        prev = h;
    }
    return r.done ? r.ticks : -1;
}

static int solve_level(int idx, int maxK, int verbose)
{
    Level *L = level_parse(g_levels[idx].src);
    LevelInfo info;
    level_info(idx, &info);
    printf("level %d \"%s\" width=%d objs=%d coins=%d\n", idx, info.name, L->width, L->nobjs, L->ncoins);
    int all_ok = 1;
    for (int K = 1; K <= maxK; K++) {
        int okc = 0;
        for (int ph = 0; ph < K; ph++) {
            float reached;
            int t = solve(L, K, ph, &reached);
            if (t >= 0) {
                okc++;
                if (verbose && K == 1) printf("  K=1: solved in %d ticks (%.1f s), %ld nodes\n", t, t / 60.0f, s_nodes);
            } else {
                printf("  K=%d phase=%d: FAILED, furthest x=%.1f (%d%%) nodes=%ld\n", K, ph, reached,
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
    Level *L = level_parse(g_levels[idx].src);
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
    game_render();
    gfx_sdl_flush();
    SDL_RenderPresent(s_ren);
    if (SDL_SaveBMP(s_surf, out) != 0) fprintf(stderr, "save failed: %s\n", SDL_GetError());
    else printf("wrote %s\n", out);
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
    Level *L = level_parse(g_levels[idx].src);
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
        const char *const *src = g_levels[i].src;
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
        Level *L = level_parse(g_levels[i].src);
        LevelInfo info;
        level_info(i, &info);
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

/* ------------------------------------------------------------------ */

static int level_arg(const char *s)
{
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
        int fails = 0;
        s_need_coins = 1;
        for (int i = 0; i < g_level_count; i++) {
            if (strcmp(argv[2], "all") && atoi(argv[2]) != i) continue;
            Level *L = level_parse(g_levels[i].src);
            float reached;
            int t = solve(L, 1, 0, &reached);
            printf("level %d: all %d coins %s\n", i, L->ncoins, t >= 0 ? "collectable" : "NOT collectable in one run");
            fails += t < 0;
            level_free(L);
        }
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
    fprintf(stderr, "unknown command\n");
    return 2;
}
