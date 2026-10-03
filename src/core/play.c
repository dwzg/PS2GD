#include <stdio.h>

#include "game_internal.h"
#include "audio.h"
#include "draw.h"
#include "font.h"
#include "fx.h"
#include "icons.h"

#define JUMP_BUTTONS (BTN_CROSS | BTN_CIRCLE | BTN_UP | BTN_L1 | BTN_R1 | BTN_L2 | BTN_R2)
#define CAM_PLAYER_X 5.65f
#define CAM_GROUND_Y (-2.4f)
#define VIEW_H (SCREEN_H / BLOCK_PX)

static PlayState *ps_get(void) { return &g_game.play; }

static unsigned s_attempts_started;

unsigned play_attempts_started(void)
{
    return s_attempts_started;
}

static int level_percent(const PlayState *ps)
{
    int pc = (int)(ps->p.x / ps->L->end_x * 100.0f);
    return clampi(pc, 0, 100);
}

static void snapshot_take(const PlayState *ps, PlaySnapshot *s)
{
    s->p = ps->p;
    s->cam_y = ps->cam_y;
    s->cam_target_y = ps->cam_target_y;
    s->corr_floor = ps->corr_floor;
    s->corr_ceil = ps->corr_ceil;
    s->corr_alpha = ps->corr_alpha;
    s->pal_from = ps->pal_from;
    s->pal_to = ps->pal_to;
    s->pal_t = ps->pal_t;
    s->trig_idx = ps->trig_idx;
}

/* Interpolated rendering starts from the current state (no sweep from where
 * the player was before a respawn or teleport). */
static void snap_prev(PlayState *ps)
{
    ps->prev_cam_x = ps->cam_x;
    ps->prev_cam_y = ps->cam_y;
    ps->prev_x = ps->p.x;
    ps->prev_y = ps->p.y;
    ps->prev_rot = ps->rot;
    ps->prev_angle = ps->vis_angle;
}

static void reset_visuals(PlayState *ps)
{
    ps->rot = 0.0f;
    ps->vis_angle = 0.0f;
    ps->trail_n = 0;
    ps->shake = 0.0f;
    ps->flash = 0.0f;
    fx_clear_space(FX_WORLD);
}

static void begin_attempt(PlayState *ps, const PlaySnapshot *from)
{
    Game *g = &g_game;
    reset_visuals(ps);
    if (from) {
        ps->p = from->p;
        ps->p.events = 0;
        ps->p.buf = 0;
        ps->cam_y = from->cam_y;
        ps->cam_target_y = from->cam_target_y;
        ps->corr_floor = from->corr_floor;
        ps->corr_ceil = from->corr_ceil;
        ps->corr_alpha = from->corr_alpha;
        ps->pal_from = from->pal_from;
        ps->pal_to = from->pal_to;
        ps->pal_t = from->pal_t;
        ps->trig_idx = from->trig_idx;
    } else {
        sim_reset(&ps->p, ps->L);
        ps->cam_y = ps->cam_target_y = CAM_GROUND_Y;
        ps->corr_floor = 0.0f;
        ps->corr_ceil = CORRIDOR_H;
        ps->corr_alpha = 0.0f;
        ps->pal_from = ps->pal_to = clampi(ps->L->start_pal, 0, PALETTE_COUNT - 1);
        ps->pal_t = 1.0f;
        ps->trig_idx = 0;
    }
    ps->cam_x = ps->p.x - CAM_PLAYER_X;
    snap_prev(ps);
    ps->phase = PH_RUN;
    ps->phase_t = 0.0f;
    ps->attempt++;
    s_attempts_started++;
    ps->attempt_time = 0.0f;
    if (!ps->practice) {
        audio_play_song(SONG_FIRST_LEVEL + ps->L->song, 0.0f);
        if (ps->level_idx < SAVE_MAX_LEVELS) g->save.attempts[ps->level_idx]++;
        g->save.total_attempts++;
        g->save_dirty = 1;
    }
}

void play_start(int level_idx, int practice)
{
    PlayState *ps = ps_get();
    if (ps->L && ps->level_idx != level_idx) {
        level_free(ps->L);
        ps->L = NULL;
    }
    if (!ps->L) ps->L = level_parse(g_levels[level_idx].src);
    ps->level_idx = level_idx;
    level_info(level_idx, &ps->info);
    ps->practice = practice;
    ps->attempt = 0;
    ps->ncp = 0;
    ps->paused = 0;
    ps->best_popup_t = 0.0f;
    ps->jumps_session = 0;
    ps->time_session = 0.0f;
    if (practice) audio_play_song(SONG_PRACTICE, 0.0f);
    begin_attempt(ps, NULL);
}

void play_exit(void)
{
    audio_stop_song();
    if (g_game.save_dirty) {
        save_store(&g_game.save);
        g_game.save_dirty = 0;
    }
    fx_clear_space(FX_WORLD);
}

static void on_death(PlayState *ps)
{
    Game *g = &g_game;
    ps->phase = PH_DEAD;
    ps->phase_t = 0.0f;
    ps->shake = 0.35f;
    ps->flash = 0.5f;
    audio_sfx(SFX_DEATH);
    if (!ps->practice) audio_stop_song();

    Color c1 = g_player_colors[g->save.col1 % PLAYER_COLOR_COUNT];
    Color c2 = g_player_colors[g->save.col2 % PLAYER_COLOR_COUNT];
    fx_burst(FX_WORLD, ps->p.x, ps->p.y, 14, 9.0f, 0.32f, 0.9f, c1, 0);
    fx_burst(FX_WORLD, ps->p.x, ps->p.y, 10, 7.0f, 0.25f, 0.8f, c2, 0);
    fx_burst(FX_WORLD, ps->p.x, ps->p.y, 12, 11.0f, 0.18f, 0.6f, COL_WHITE, 1);
    fx_ring(FX_WORLD, ps->p.x, ps->p.y, 0.3f, 3.2f, 0.5f, col_with_alpha(COL_WHITE, 0.9f));

    int pc = mini(level_percent(ps), 99);
    g->save.total_jumps += (uint32_t)ps->p.jumps;
    ps->jumps_session += ps->p.jumps;
    ps->time_session += ps->attempt_time;
    if (ps->level_idx < SAVE_MAX_LEVELS) {
        uint8_t *best = ps->practice ? &g->save.best_practice[ps->level_idx] : &g->save.best[ps->level_idx];
        if (pc > *best) {
            *best = (uint8_t)pc;
            g->save_dirty = 1;
            if (!ps->practice) {
                ps->best_popup_t = 2.0f;
                ps->best_popup_val = pc;
            }
        }
    }
}

static void on_complete(PlayState *ps)
{
    Game *g = &g_game;
    ps->phase = PH_COMPLETE;
    ps->phase_t = 0.0f;
    ps->results_sel = 0;
    ps->flash = 0.8f;
    audio_sfx(SFX_COMPLETE);
    g->save.total_jumps += (uint32_t)ps->p.jumps;
    ps->jumps_session += ps->p.jumps;
    ps->time_session += ps->attempt_time;
    ps->new_best = 0;
    ps->coins_gained = 0;
    if (ps->level_idx < SAVE_MAX_LEVELS) {
        if (ps->practice) {
            g->save.best_practice[ps->level_idx] = 100;
        } else {
            if (g->save.best[ps->level_idx] < 100) ps->new_best = 1;
            g->save.best[ps->level_idx] = 100;
            uint8_t before = g->save.coins[ps->level_idx];
            g->save.coins[ps->level_idx] |= ps->p.coins;
            ps->coins_gained = g->save.coins[ps->level_idx] & (uint8_t)~before;
        }
        g->save_dirty = 1;
    }
    save_store(&g->save);
    g->save_dirty = 0;
}

static void add_checkpoint(PlayState *ps)
{
    if (ps->ncp >= MAX_CHECKPOINTS) {
        memmove(&ps->cp[0], &ps->cp[1], sizeof(PlaySnapshot) * (MAX_CHECKPOINTS - 1));
        ps->ncp = MAX_CHECKPOINTS - 1;
    }
    snapshot_take(ps, &ps->cp[ps->ncp++]);
    fx_ring(FX_WORLD, ps->p.x, ps->p.y, 0.2f, 1.6f, 0.4f, RGB(80, 255, 120));
    audio_sfx(SFX_CHECKPOINT);
}

/* ------------------------------------------------------------------ */

static void update_camera(PlayState *ps, float dt)
{
    const Player *p = &ps->p;
    if (ps->phase != PH_COMPLETE) {
        ps->cam_x = p->x - CAM_PLAYER_X;
    } else {
        /* past the finish the camera glides to a stop from the speed it had
         * (stopping dead in one tick looks like a hitch) */
        const float k = 5.0f;
        float stop = ps->L->end_x - CAM_PLAYER_X + p->speed / k;
        ps->cam_x += (stop - ps->cam_x) * (1.0f - expf(-dt * k));
    }

    float k;
    if (p->mode == MODE_CUBE) {
        float lo = ps->cam_target_y + 2.6f, hi = ps->cam_target_y + VIEW_H - 4.2f;
        if (p->y > hi) ps->cam_target_y = p->y - (VIEW_H - 4.2f);
        if (p->y < lo) ps->cam_target_y = p->y - 2.6f;
        if (ps->cam_target_y < CAM_GROUND_Y) ps->cam_target_y = CAM_GROUND_Y;
        k = 1.0f - expf(-dt * 5.0f);
        ps->corr_alpha = approachf(ps->corr_alpha, 0.0f, dt * 3.0f);
    } else {
        ps->corr_floor = p->floor_y;
        ps->corr_ceil = p->ceil_y;
        ps->cam_target_y = (p->floor_y + p->ceil_y) * 0.5f - VIEW_H * 0.5f;
        k = 1.0f - expf(-dt * 6.0f);
        ps->corr_alpha = approachf(ps->corr_alpha, 1.0f, dt * 4.0f);
    }
    ps->cam_y = lerpf(ps->cam_y, ps->cam_target_y, k);
}

static void update_palette(PlayState *ps, float dt)
{
    const Level *L = ps->L;
    while (ps->trig_idx < L->ntrig && ps->p.x >= L->trig[ps->trig_idx].x) {
        palette_lerp(&ps->pal, &g_palettes[ps->pal_from], &g_palettes[ps->pal_to], smoothstepf(ps->pal_t));
        /* start a new blend from the current target */
        ps->pal_from = ps->pal_to;
        ps->pal_to = clampi(L->trig[ps->trig_idx].pal, 0, PALETTE_COUNT - 1);
        ps->pal_t = 0.0f;
        ps->trig_idx++;
    }
    ps->pal_t = minf(1.0f, ps->pal_t + dt / 0.8f);
    palette_lerp(&ps->pal, &g_palettes[ps->pal_from], &g_palettes[ps->pal_to], smoothstepf(ps->pal_t));
}

static void update_visuals(PlayState *ps, float dt)
{
    Game *g = &g_game;
    Player *p = &ps->p;
    float gdir = (float)p->grav;
    Color c1 = g_player_colors[g->save.col1 % PLAYER_COLOR_COUNT];
    Color c2 = g_player_colors[g->save.col2 % PLAYER_COLOR_COUNT];

    switch (p->mode) {
    case MODE_CUBE:
        if (p->grounded) {
            float q = PI * 0.5f;
            float target = roundf(ps->rot / q) * q;
            ps->rot = approachf(ps->rot, target, dt * 18.0f);
        } else {
            ps->rot += 7.3f * dt * gdir;
        }
        ps->vis_angle = 0.0f;
        break;
    case MODE_BALL:
        ps->rot += p->speed / 0.45f * dt * gdir;
        break;
    case MODE_SHIP: {
        float target = -atan2f(p->vy, p->speed * 1.6f);
        ps->vis_angle = lerpf(ps->vis_angle, target, 1.0f - expf(-dt * 14.0f));
        break;
    }
    case MODE_UFO:
        ps->vis_angle = lerpf(ps->vis_angle, clampf(-p->vy * 0.02f, -0.3f, 0.3f), 1.0f - expf(-dt * 10.0f));
        break;
    case MODE_WAVE:
        ps->vis_angle = -atan2f(p->vy, p->speed);
        break;
    }

    /* trail history */
    if (ps->trail_n < TRAIL_LEN) ps->trail_n++;
    memmove(&ps->trail_x[1], &ps->trail_x[0], sizeof(float) * (TRAIL_LEN - 1));
    memmove(&ps->trail_y[1], &ps->trail_y[0], sizeof(float) * (TRAIL_LEN - 1));
    ps->trail_x[0] = p->x;
    ps->trail_y[0] = p->y;
    if (p->mode != MODE_WAVE && p->mode != MODE_SHIP) ps->trail_n = mini(ps->trail_n, 1);

    /* ground dust */
    ps->ground_fx_tick++;
    if ((p->mode == MODE_CUBE || p->mode == MODE_BALL) && p->grounded && (ps->ground_fx_tick % 3) == 0) {
        Particle *q = fx_spawn(FX_WORLD);
        q->x = p->x - 0.45f;
        q->y = p->y - 0.45f * gdir;
        q->vx = -2.0f - (ps->ground_fx_tick % 7) * 0.3f;
        q->vy = (1.0f + (ps->ground_fx_tick % 5) * 0.4f) * gdir;
        q->life = q->max_life = 0.35f;
        q->size = 0.16f;
        q->size_end = 0.05f;
        q->c = col_with_alpha(c1, 0.8f);
    }
    if (p->mode == MODE_SHIP || p->mode == MODE_UFO) {
        Particle *q = fx_spawn(FX_WORLD);
        float back = p->mode == MODE_SHIP ? 0.55f : 0.0f;
        q->x = p->x - back * cosf(ps->vis_angle);
        q->y = p->y + (p->mode == MODE_UFO ? -0.25f * gdir : back * sinf(ps->vis_angle));
        q->vx = -3.0f;
        q->vy = (p->mode == MODE_UFO ? -1.5f * gdir : 0.0f);
        q->life = q->max_life = 0.3f;
        q->size = 0.22f;
        q->size_end = 0.02f;
        q->c = col_with_alpha(c2, 0.9f);
        q->add = 1;
    }

    /* event effects */
    if (p->events & (EV_ORB | EV_PAD | EV_PORTAL | EV_COIN | EV_SPEED)) {
        if (p->ev_obj >= 0 && p->ev_obj < ps->L->nobjs) {
            const LevelObj *o = &ps->L->objs[p->ev_obj];
            float ox = o->cx + 0.5f, oy = o->cy + 0.5f;
            if (p->events & EV_ORB) fx_ring(FX_WORLD, ox, oy, 0.3f, 1.6f, 0.35f, COL_WHITE);
            if (p->events & EV_PAD) fx_burst(FX_WORLD, ox, o->cy + 0.2f, 8, 6.0f, 0.18f, 0.4f, COL_WHITE, 1);
            if (p->events & (EV_PORTAL | EV_SPEED)) fx_ring(FX_WORLD, ox, oy, 0.5f, 3.0f, 0.45f, col_with_alpha(COL_WHITE, 0.8f));
            if (p->events & EV_COIN) {
                fx_burst(FX_WORLD, ox, oy, 16, 8.0f, 0.2f, 0.6f, COL_COIN, 1);
                fx_ring(FX_WORLD, ox, oy, 0.3f, 2.0f, 0.4f, COL_COIN);
                audio_sfx(SFX_COIN);
            }
        }
    }
    if (p->events & EV_LAND && p->mode == MODE_CUBE) {
        fx_burst(FX_WORLD, p->x, p->y - 0.5f * gdir, 4, 3.0f, 0.12f, 0.25f, col_with_alpha(c1, 0.8f), 0);
    }

    ps->shake = maxf(0.0f, ps->shake - dt);
    ps->flash = maxf(0.0f, ps->flash - dt * 2.0f);
    if (ps->best_popup_t > 0.0f) ps->best_popup_t -= dt;
}

static void spawn_firework(PlayState *ps)
{
    static const Color cols[5] = {RGB(255, 220, 60), RGB(80, 255, 140), RGB(80, 200, 255),
                                  RGB(255, 100, 220), RGB(255, 255, 255)};
    uint32_t h = hash_u32((uint32_t)(ps->phase_t * 1000.0f) + 77u);
    const float k = SCREEN_W / 640.0f; /* across the screen, however wide */
    float x = ps->cam_x + 3.0f * k + hash_f01(h) * 13.0f * k;
    float y = ps->cam_y + 5.0f + hash_f01(h >> 5) * 6.0f;
    Color c = cols[h % 5];
    fx_burst(FX_WORLD, x, y, 22, 9.0f, 0.18f, 1.0f, c, 1);
    fx_ring(FX_WORLD, x, y, 0.2f, 2.5f, 0.5f, c);
}

static void pause_tick(PlayState *ps)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) { ps->pause_sel = (ps->pause_sel + 3) % 4; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_DOWN) { ps->pause_sel = (ps->pause_sel + 1) % 4; audio_sfx(SFX_MENU_MOVE); }
    if (g->pressed & (BTN_START | BTN_CIRCLE)) {
        ps->paused = 0;
        audio_sfx(SFX_MENU_BACK);
        audio_pause(0);
        return;
    }
    if (!(g->pressed & BTN_CROSS)) return;
    audio_sfx(SFX_MENU_SELECT);
    switch (ps->pause_sel) {
    case 0: /* resume */
        ps->paused = 0;
        audio_pause(0);
        break;
    case 1: /* restart */
        ps->paused = 0;
        audio_pause(0);
        if (ps->phase == PH_RUN) on_death(ps);
        ps->ncp = 0;
        if (ps->practice) audio_play_song(SONG_PRACTICE, 0.0f);
        begin_attempt(ps, NULL);
        break;
    case 2: /* toggle practice */
        ps->paused = 0;
        audio_pause(0);
        ps->practice = !ps->practice;
        ps->ncp = 0;
        ps->attempt = 0;
        if (ps->practice) audio_play_song(SONG_PRACTICE, 0.0f);
        begin_attempt(ps, NULL);
        break;
    default: /* exit */
        ps->paused = 0;
        audio_pause(0);
        if (ps->phase == PH_RUN && ps->attempt_time > 0.5f) on_death(ps);
        screen_go(SCR_SELECT);
        break;
    }
}

void play_tick(void)
{
    Game *g = &g_game;
    PlayState *ps = ps_get();
    const float dt = TICK_DT;

    snap_prev(ps);
    if (ps->paused) {
        pause_tick(ps);
        return;
    }
    if ((g->pressed & BTN_START) && ps->phase != PH_COMPLETE) {
        ps->paused = 1;
        ps->pause_sel = 0;
        audio_pause(1);
        audio_sfx(SFX_MENU_SELECT);
        return;
    }

    switch (ps->phase) {
    case PH_RUN: {
        uint32_t jb = JUMP_BUTTONS;
        if (ps->practice) {
            if (g->pressed & BTN_SQUARE) add_checkpoint(ps);
            if ((g->pressed & BTN_TRIANGLE) && ps->ncp > 0) {
                ps->ncp--;
                audio_sfx(SFX_MENU_BACK);
            }
        }
        sim_tick(&ps->p, ps->L, (g->held & jb) != 0, (g->pressed & jb) != 0);
        ps->attempt_time += dt;
        update_visuals(ps, dt);
        if (ps->p.dead) on_death(ps);
        else if (ps->p.done) on_complete(ps);
        break;
    }
    case PH_DEAD:
        ps->phase_t += dt;
        ps->shake = maxf(0.0f, ps->shake - dt);
        ps->flash = maxf(0.0f, ps->flash - dt * 2.0f);
        if (ps->best_popup_t > 0.0f) ps->best_popup_t -= dt;
        if (ps->phase_t >= (ps->practice ? 0.65f : 1.0f))
            begin_attempt(ps, (ps->practice && ps->ncp > 0) ? &ps->cp[ps->ncp - 1] : NULL);
        break;
    case PH_COMPLETE: {
        ps->phase_t += dt;
        Player *p = &ps->p;
        p->x += p->speed * dt;
        if (ps->phase_t > 0.3f && ps->phase_t < 3.5f && ((int)(ps->phase_t * 60.0f) % 14) == 0) spawn_firework(ps);
        ps->flash = maxf(0.0f, ps->flash - dt * 1.5f);
        if (ps->phase_t > 1.6f) {
            if (g->repeat & (BTN_LEFT | BTN_RIGHT)) { ps->results_sel ^= 1; audio_sfx(SFX_MENU_MOVE); }
            if (g->pressed & BTN_CROSS) {
                audio_sfx(SFX_MENU_SELECT);
                if (ps->results_sel == 0) screen_go(SCR_SELECT);
                else {
                    ps->ncp = 0;
                    ps->attempt = 0;
                    if (ps->practice) audio_play_song(SONG_PRACTICE, 0.0f);
                    begin_attempt(ps, NULL);
                }
            } else if (g->pressed & BTN_CIRCLE) {
                audio_sfx(SFX_MENU_BACK);
                screen_go(SCR_SELECT);
            }
        }
        break;
    }
    }
    update_camera(ps, dt);
    update_palette(ps, dt);
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

/* Player pose for the frame being drawn (interpolated between ticks). */
typedef struct {
    float x, y, rot, angle;
} Pose;

static void draw_wave_trail(const PlayState *ps, const View *v, const Pose *pose, Color c)
{
    if (ps->trail_n < 2) return;
    /* the newest point follows the drawn player rather than the last tick */
    float tx[TRAIL_LEN], ty[TRAIL_LEN];
    memcpy(tx, ps->trail_x, sizeof(float) * (size_t)ps->trail_n);
    memcpy(ty, ps->trail_y, sizeof(float) * (size_t)ps->trail_n);
    tx[0] = pose->x;
    ty[0] = pose->y;
    gfx_blend(BLEND_ADD);
    for (int i = 0; i + 1 < ps->trail_n; i++) {
        float a0 = 1.0f - (float)i / ps->trail_n, a1 = 1.0f - (float)(i + 1) / ps->trail_n;
        draw_line2(view_sx(v, tx[i]), view_sy(v, ty[i]), view_sx(v, tx[i + 1]), view_sy(v, ty[i + 1]),
                   9.0f * a0 + 2.0f, col_with_alpha(c, 0.7f * a0), col_with_alpha(c, 0.7f * a1));
    }
    gfx_blend(BLEND_ALPHA);
    for (int i = 0; i + 1 < ps->trail_n; i++) {
        float a0 = 1.0f - (float)i / ps->trail_n;
        draw_line(view_sx(v, tx[i]), view_sy(v, ty[i]), view_sx(v, tx[i + 1]), view_sy(v, ty[i + 1]), 3.0f,
                  col_with_alpha(COL_WHITE, 0.9f * a0));
    }
}

static void draw_player(const PlayState *ps, const View *v, const Pose *pose)
{
    const Game *g = &g_game;
    const Player *p = &ps->p;
    Color c1 = g_player_colors[g->save.col1 % PLAYER_COLOR_COUNT];
    Color c2 = g_player_colors[g->save.col2 % PLAYER_COLOR_COUNT];
    float sx = view_sx(v, pose->x), sy = view_sy(v, pose->y);
    float ang = pose->angle;
    int flip = p->grav < 0;

    if (p->mode == MODE_WAVE) draw_wave_trail(ps, v, pose, c2);
    draw_glow(sx, sy, BLOCK_PX * 1.3f, col_with_alpha(c1, 0.22f + 0.15f * v->pulse));

    switch (p->mode) {
    case MODE_CUBE: icon_draw_cube(sx, sy, BLOCK_PX, pose->rot, g->save.icon, c1, c2); break;
    case MODE_SHIP: icon_draw_ship(sx, sy, BLOCK_PX, flip ? -ang : ang, flip, g->save.icon, c1, c2); break;
    case MODE_BALL: icon_draw_ball(sx, sy, BLOCK_PX, pose->rot, g->save.icon, c1, c2); break;
    case MODE_UFO: icon_draw_ufo(sx, sy, BLOCK_PX, ang, flip, g->save.icon, c1, c2); break;
    case MODE_WAVE: icon_draw_wave(sx, sy, BLOCK_PX * 0.85f, ang, c1, c2); break;
    }
}

static void draw_hud(const PlayState *ps)
{
    float frac = ps->p.x / ps->L->end_x;
    if (ps->phase == PH_COMPLETE) frac = 1.0f;
    float x0 = UI_X(180), x1 = UI_X(440);
    render_progress_bar(x0, 14, x1, 24, frac, RGB(90, 255, 120), RGB(255, 255, 255));
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", clampi((int)(frac * 100.0f), 0, 100));
    font_draw_fancy(x1 + 14, 12, 2.0f, COL_WHITE, RGB(200, 220, 255), RGB(0, 0, 0), 2.0f, ALIGN_LEFT, buf);

    if (ps->practice) {
        font_draw_fancy(SCREEN_W / 2, 32, 2.0f, RGB(120, 255, 150), RGB(40, 200, 90), RGB(0, 0, 0), 2.0f,
                        ALIGN_CENTER, "PRACTICE");
        if (ps->attempt_time < 5.0f && ps->attempt <= 2) {
            float a = clampf(5.0f - ps->attempt_time, 0.0f, 1.0f);
            font_draw(SCREEN_W / 2, SCREEN_H - 26, 2.0f, col_with_alpha(COL_WHITE, a), ALIGN_CENTER,
                      GLYPH_SQUARE " CHECKPOINT   " GLYPH_TRIANGLE " REMOVE");
        }
    }
    if (ps->best_popup_t > 0.0f) {
        float t = 2.0f - ps->best_popup_t;
        float s = 4.0f * (t < 0.25f ? ease_out_back(t / 0.25f) : 1.0f);
        float a = clampf(ps->best_popup_t / 0.4f, 0.0f, 1.0f);
        snprintf(buf, sizeof(buf), "NEW BEST %d%%", ps->best_popup_val);
        font_draw_fancy(SCREEN_W / 2, 170 - s * 3.5f, s, col_with_alpha(RGB(255, 255, 160), a),
                        col_with_alpha(RGB(255, 170, 40), a), col_with_alpha(RGB(0, 0, 0), a), 3.0f,
                        ALIGN_CENTER, buf);
    }
}

static void draw_pause(const PlayState *ps)
{
    const Game *g = &g_game;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, 150));
    render_panel(UI_X(120), 60, UI_X(520), 400, RGBA(10, 14, 30, 230), RGBA(255, 255, 255, 160));
    font_draw_fancy(SCREEN_W / 2, 80, 4.0f, COL_WHITE, RGB(190, 210, 255), RGB(0, 0, 0), 3.0f, ALIGN_CENTER, ps->info.name);

    int idx = ps->level_idx < SAVE_MAX_LEVELS ? ps->level_idx : 0;
    char buf[48];
    font_draw(UI_X(150), 130, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "NORMAL");
    render_progress_bar(UI_X(250), 130, UI_X(440), 142, g->save.best[idx] / 100.0f, RGB(90, 255, 120), RGB(200, 255, 200));
    snprintf(buf, sizeof(buf), "%d%%", g->save.best[idx]);
    font_draw(UI_X(452), 130, 2.0f, COL_WHITE, ALIGN_LEFT, buf);
    font_draw(UI_X(150), 156, 2.0f, RGB(200, 220, 255), ALIGN_LEFT, "PRACTICE");
    render_progress_bar(UI_X(250), 156, UI_X(440), 168, g->save.best_practice[idx] / 100.0f, RGB(80, 200, 255), RGB(200, 240, 255));
    snprintf(buf, sizeof(buf), "%d%%", g->save.best_practice[idx]);
    font_draw(UI_X(452), 156, 2.0f, COL_WHITE, ALIGN_LEFT, buf);

    const char *items[4] = {"RESUME", "RESTART", ps->practice ? "NORMAL MODE" : "PRACTICE MODE", "EXIT LEVEL"};
    for (int i = 0; i < 4; i++) {
        float y = 196 + i * 42;
        int sel = i == ps->pause_sel;
        if (sel) {
            render_panel(UI_X(170), y - 8, UI_X(470), y + 30, RGBA(255, 255, 255, 40), RGB(120, 255, 150));
        }
        font_draw_fancy(SCREEN_W / 2, y, 3.0f, sel ? RGB(255, 255, 255) : RGB(170, 180, 200),
                        sel ? RGB(150, 255, 170) : RGB(110, 120, 140), RGB(0, 0, 0), 2.0f, ALIGN_CENTER, items[i]);
    }
    snprintf(buf, sizeof(buf), "ATTEMPT %d", ps->attempt);
    /* about as far above the bottom border as the level name is below the top */
    font_draw(SCREEN_W / 2, 368, 2.0f, RGB(160, 170, 190), ALIGN_CENTER, buf);
}

static void draw_results(const PlayState *ps)
{
    const Game *g = &g_game;
    float t = ps->phase_t;
    if (t < 0.4f) return;
    float s = 5.0f * (t < 0.7f ? ease_out_back((t - 0.4f) / 0.3f) : 1.0f);
    font_draw_fancy(SCREEN_W / 2, 70 - s * 3.5f + 20, s, RGB(255, 255, 170), RGB(255, 170, 30), RGB(0, 0, 0), 3.0f,
                    ALIGN_CENTER, "LEVEL COMPLETE!");
    if (t < 1.6f) return;
    float a = clampf((t - 1.6f) / 0.3f, 0.0f, 1.0f);
    render_panel(UI_X(140), 130, UI_X(500), 380, RGBA(10, 14, 30, (int)(220 * a)), RGBA(255, 255, 255, (int)(160 * a)));
    char buf[64];
    snprintf(buf, sizeof(buf), "ATTEMPTS: %d", ps->attempt);
    font_draw(SCREEN_W / 2, 152, 3.0f, COL_WHITE, ALIGN_CENTER, buf);
    snprintf(buf, sizeof(buf), "JUMPS: %d", ps->jumps_session);
    font_draw(SCREEN_W / 2, 184, 3.0f, COL_WHITE, ALIGN_CENTER, buf);
    int secs = (int)ps->time_session;
    snprintf(buf, sizeof(buf), "TIME: %d:%02d", secs / 60, secs % 60);
    font_draw(SCREEN_W / 2, 216, 3.0f, COL_WHITE, ALIGN_CENTER, buf);

    /* coins */
    int n = ps->L->ncoins;
    uint8_t have = ps->level_idx < SAVE_MAX_LEVELS ? g->save.coins[ps->level_idx] : 0;
    for (int i = 0; i < n; i++) {
        float cx = SCREEN_W / 2 + (i - (n - 1) * 0.5f) * 50;
        int got = (have >> i) & 1;
        int fresh = (ps->coins_gained >> i) & 1;
        float sc = fresh ? 1.0f + 0.2f * sinf(g->t * 8.0f) : 1.0f;
        if (got) render_coin(cx, 268, 15 * sc, g->t * 3.0f + i, 1.0f, 0);
        else draw_circle(cx, 268, 15, RGBA(0, 0, 0, 150));
    }
    if (ps->practice)
        font_draw(SCREEN_W / 2, 292, 2.0f, RGB(120, 255, 150), ALIGN_CENTER, "PRACTICE RUN");
    else if (ps->new_best)
        font_draw(SCREEN_W / 2, 292, 2.0f, RGB(255, 230, 100), ALIGN_CENTER, "FIRST CLEAR!");

    const char *opts[2] = {"MENU", "REPLAY"};
    for (int i = 0; i < 2; i++) {
        float cx = SCREEN_W / 2 + (i == 0 ? -90 : 90);
        int sel = ps->results_sel == i;
        if (sel) render_panel(cx - 75, 318, cx + 75, 360, RGBA(255, 255, 255, 40), RGB(120, 255, 150));
        font_draw_fancy(cx, 328, 3.0f, sel ? COL_WHITE : RGB(160, 170, 190), sel ? RGB(150, 255, 170) : RGB(110, 120, 140),
                        RGB(0, 0, 0), 2.0f, ALIGN_CENTER, opts[i]);
    }
}

static Pose make_pose(const PlayState *ps)
{
    const float a = g_game.alpha;
    Pose pose = {lerpf(ps->prev_x, ps->p.x, a), lerpf(ps->prev_y, ps->p.y, a), lerpf(ps->prev_rot, ps->rot, a),
                 lerpf(ps->prev_angle, ps->vis_angle, a)};
    return pose;
}

/* --- also used by the title screen's demo run (demo.c) -------------- */

void play_view(const PlayState *ps, View *v)
{
    const Game *g = &g_game;
    const float a = g->alpha;
    float sh = ps->shake * 0.6f;
    v->cam_x = lerpf(ps->prev_cam_x, ps->cam_x, a) + (sh > 0 ? (hash_f01((uint32_t)(g->t * 977.0f)) - 0.5f) * sh : 0.0f);
    v->cam_y = lerpf(ps->prev_cam_y, ps->cam_y, a) + (sh > 0 ? (hash_f01((uint32_t)(g->t * 731.0f) + 9u) - 0.5f) * sh : 0.0f);
    v->time = g->t;
    v->pulse = beat_pulse();
    v->pal = &ps->pal;
}

void play_begin_tick(PlayState *ps)
{
    snap_prev(ps);
}

void play_end_tick(PlayState *ps)
{
    update_visuals(ps, TICK_DT);
    update_camera(ps, TICK_DT);
}

void play_place(PlayState *ps)
{
    reset_visuals(ps);
    ps->cam_x = ps->p.x - CAM_PLAYER_X;
    ps->cam_y = ps->cam_target_y = CAM_GROUND_Y;
    ps->phase = PH_RUN;
    snap_prev(ps);
}

void play_draw_player(const PlayState *ps, const View *v)
{
    Pose pose = make_pose(ps);
    draw_player(ps, v, &pose);
}

void play_render(void)
{
    const Game *g = &g_game;
    const PlayState *ps = ps_get();
    const float a = g->alpha;
    Pose pose = make_pose(ps);
    View v;
    play_view(ps, &v);

    render_background(&v);
    render_ground(&v, ps->corr_floor, ps->corr_ceil, ps->corr_alpha);

    /* attempt counter painted into the world near the start */
    {
        char buf[32];
        snprintf(buf, sizeof(buf), ps->practice ? "PRACTICE %d" : "ATTEMPT %d", ps->attempt);
        float ax = view_sx(&v, 3.0f), ay = view_sy(&v, 6.2f);
        if (ax > -400) font_draw_fancy(ax, ay, 4.0f, COL_WHITE, RGB(210, 225, 255), RGB(0, 0, 0), 3.0f, ALIGN_LEFT, buf);
    }

    uint8_t saved = ps->level_idx < SAVE_MAX_LEVELS ? g->save.coins[ps->level_idx] : 0;
    render_level(&v, ps->L, &ps->p, saved);

    /* practice checkpoints */
    if (ps->practice) {
        for (int i = 0; i < ps->ncp; i++) {
            float cx = view_sx(&v, ps->cp[i].p.x), cy = view_sy(&v, ps->cp[i].p.y);
            if (cx < -20 || cx > SCREEN_W + 20) continue;
            float d = 11.0f;
            float xy[8] = {cx, cy - d - 3, cx + d + 3, cy, cx, cy + d + 3, cx - d - 3, cy};
            draw_poly(xy, 4, RGB(0, 40, 10));
            float xy2[8] = {cx, cy - d, cx + d, cy, cx, cy + d, cx - d, cy};
            draw_poly(xy2, 4, RGB(90, 255, 120));
        }
    }

    if (ps->phase != PH_DEAD) draw_player(ps, &v, &pose);
    fx_draw(FX_WORLD, v.cam_x, v.cam_y, (1.0f - a) * TICK_DT);

    if (ps->flash > 0.0f) {
        gfx_blend(BLEND_ADD);
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_with_alpha(COL_WHITE, ps->flash * 0.5f));
        gfx_blend(BLEND_ALPHA);
    }

    draw_hud(ps);
    if (ps->phase == PH_COMPLETE) draw_results(ps);
    if (ps->paused) draw_pause(ps);
}
