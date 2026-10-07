/* A level being played: attempts, practice checkpoints, the pause menu and
 * results, the camera, palette changes and effects. Drawn by play_draw.c in
 * the vector family, by the platform's own code elsewhere. */
#include "game_internal.h"
#include "audio.h"
#include "fx.h"

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
    return progress_percent(ps->p.x, (uint16_t)ps->L->width);
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
    ps->prev_x = sim_x(&ps->p);
    ps->prev_y = sim_y(&ps->p);
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
    /* the colours the attempt starts in (update_palette goes on from them
     * each tick): a run paused on its first tick is drawn in them too */
    palette_lerp(&ps->pal, &g_palettes[ps->pal_from], &g_palettes[ps->pal_to], smoothstepf(ps->pal_t));
    ps->cam_x = sim_x(&ps->p) - CAM_PLAYER_X;
    snap_prev(ps);
    ps->phase = PH_RUN;
    ps->phase_t = 0.0f;
    ps->attempt++;
    s_attempts_started++;
    ps->attempt_time = 0.0f;
    progress_attempt(&g->save.progress, (uint8_t)ps->level_idx, (uint8_t)ps->practice);
    if (!ps->practice) {
        audio_play_song(SONG_FIRST_LEVEL + ps->L->song, 0.0f);
        g->save_dirty = 1;
    }
}

/* The button that chose to go on (the pause menu's choice, the results'
 * replay), or one still down from the level select, is not a jump until
 * it has been let go: the menus' button is a jump button too. (A jump
 * button already down as the pause is closed with START goes on as one.) */
static uint32_t s_jump_lock;

void play_start(int level_idx, int practice)
{
    PlayState *ps = ps_get();
    s_jump_lock = g_game.held & JUMP_BUTTONS;
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
    float px = sim_x(&ps->p), py = sim_y(&ps->p);
    fx_burst(FX_WORLD, px, py, 14, 9.0f, 0.32f, 0.9f, c1, 0);
    fx_burst(FX_WORLD, px, py, 10, 7.0f, 0.25f, 0.8f, c2, 0);
    fx_burst(FX_WORLD, px, py, 12, 11.0f, 0.18f, 0.6f, COL_WHITE, 1);
    fx_ring(FX_WORLD, px, py, 0.3f, 3.2f, 0.5f, col_with_alpha(COL_WHITE, 0.9f));

    ps->jumps_session += ps->p.jumps;
    ps->time_session += ps->attempt_time;
    int best = progress_death(&g->save.progress, (uint8_t)ps->level_idx, (uint8_t)ps->practice,
                              (uint8_t)level_percent(ps), ps->p.jumps);
    g->save_dirty = 1;
    if (best) {
        ps->best_popup_t = 2.0f;
        ps->best_popup_val = best;
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
    ps->jumps_session += ps->p.jumps;
    ps->time_session += ps->attempt_time;
    uint8_t first;
    ps->coins_gained = progress_complete(&g->save.progress, (uint8_t)ps->level_idx, (uint8_t)ps->practice, ps->p.coins,
                                         ps->p.jumps, &first);
    ps->new_best = first;
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
    fx_ring(FX_WORLD, sim_x(&ps->p), sim_y(&ps->p), 0.2f, 1.6f, 0.4f, RGB(80, 255, 120));
    audio_sfx(SFX_CHECKPOINT);
}

/* ------------------------------------------------------------------ */

/* past the finish the camera glides to a stop from the speed it had
 * (stopping dead in one tick looks like a hitch): what is left of the way
 * falls off as e^(-CAM_STOP_K t), and the way is speed / CAM_STOP_K */
#define CAM_STOP_K 5.0f

float play_camera_stop(const PlayState *ps)
{
    return ps->L->end_x - CAM_PLAYER_X + sim_speed(&ps->p) / CAM_STOP_K;
}

/* 1 - e^(-dt rate): the share of the way an ease covers in dt; for a
 * tick, worked out once (expf in software takes thousands of cycles) */
static float ease_share(float dt, float rate, float *tick_share)
{
    if (dt != TICK_DT) return 1.0f - expf(-dt * rate);
    if (*tick_share == 0.0f) *tick_share = 1.0f - expf(-TICK_DT * rate);
    return *tick_share;
}

static void update_camera(PlayState *ps, float dt)
{
    static float share5, share6;
    const Player *p = &ps->p;
    const float px = sim_x(p), py = sim_y(p);
    if (ps->phase != PH_COMPLETE) {
        ps->cam_x = px - CAM_PLAYER_X;
    } else {
        ps->cam_x += (play_camera_stop(ps) - ps->cam_x) * (1.0f - expf(-dt * CAM_STOP_K));
    }

    float k;
    if (p->mode == MODE_CUBE) {
        float lo = ps->cam_target_y + 2.6f, hi = ps->cam_target_y + VIEW_H - 4.2f;
        if (py > hi) ps->cam_target_y = py - (VIEW_H - 4.2f);
        if (py < lo) ps->cam_target_y = py - 2.6f;
        if (ps->cam_target_y < CAM_GROUND_Y) ps->cam_target_y = CAM_GROUND_Y;
        k = ease_share(dt, 5.0f, &share5);
        ps->corr_alpha = approachf(ps->corr_alpha, 0.0f, dt * 3.0f);
    } else {
        ps->corr_floor = p->floor_y;
        ps->corr_ceil = p->ceil_y;
        ps->cam_target_y = (p->floor_y + p->ceil_y) * 0.5f - VIEW_H * 0.5f;
        k = ease_share(dt, 6.0f, &share6);
        ps->corr_alpha = approachf(ps->corr_alpha, 1.0f, dt * 4.0f);
    }
    ps->cam_y = lerpf(ps->cam_y, ps->cam_target_y, k);
}

static void update_palette(PlayState *ps, float dt)
{
    const Level *L = ps->L;
    while (ps->trig_idx < L->ntrig && sim_x(&ps->p) >= L->trig[ps->trig_idx].x) {
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

/* The player's rotation and tilt, and the trail and exhaust behind it.
 * Past the finish (finished) a cube lands its spin on a side as on the
 * ground. */
static void update_motion(PlayState *ps, float dt, int finished)
{
    Game *g = &g_game;
    Player *p = &ps->p;
    const float px = sim_x(p), py = sim_y(p), vy = sim_vy(p), speed = sim_speed(p);
    float gdir = (float)p->grav;
    Color c1 = g_player_colors[g->save.col1 % PLAYER_COLOR_COUNT];
    Color c2 = g_player_colors[g->save.col2 % PLAYER_COLOR_COUNT];

    switch (p->mode) {
    case MODE_CUBE:
        if (p->grounded || finished) {
            float q = PI * 0.5f;
            float target = roundf(ps->rot / q) * q;
            ps->rot = approachf(ps->rot, target, dt * 18.0f);
        } else {
            ps->rot += 7.3f * dt * gdir;
        }
        ps->vis_angle = 0.0f;
        break;
    case MODE_BALL:
        ps->rot += speed / 0.45f * dt * gdir;
        break;
    case MODE_SHIP: {
        float target = -atan2f(vy, speed * 1.6f);
        ps->vis_angle = lerpf(ps->vis_angle, target, 1.0f - expf(-dt * 14.0f));
        break;
    }
    case MODE_UFO:
        ps->vis_angle = lerpf(ps->vis_angle, clampf(-vy * 0.02f, -0.3f, 0.3f), 1.0f - expf(-dt * 10.0f));
        break;
    case MODE_WAVE:
        ps->vis_angle = -atan2f(vy, speed);
        break;
    }

    /* trail history */
    if (ps->trail_n < TRAIL_LEN) ps->trail_n++;
    memmove(&ps->trail_x[1], &ps->trail_x[0], sizeof(float) * (TRAIL_LEN - 1));
    memmove(&ps->trail_y[1], &ps->trail_y[0], sizeof(float) * (TRAIL_LEN - 1));
    ps->trail_x[0] = px;
    ps->trail_y[0] = py;
    if (p->mode != MODE_WAVE && p->mode != MODE_SHIP) ps->trail_n = mini(ps->trail_n, 1);

    /* ground dust */
    ps->ground_fx_tick++;
    if ((p->mode == MODE_CUBE || p->mode == MODE_BALL) && p->grounded && (ps->ground_fx_tick % 3) == 0) {
        Particle *q = fx_spawn(FX_WORLD);
        q->x = px - 0.45f;
        q->y = py - 0.45f * gdir;
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
        q->x = px - back * cosf(ps->vis_angle);
        q->y = py + (p->mode == MODE_UFO ? -0.25f * gdir : back * sinf(ps->vis_angle));
        q->vx = -3.0f;
        q->vy = (p->mode == MODE_UFO ? -1.5f * gdir : 0.0f);
        q->life = q->max_life = 0.3f;
        q->size = 0.22f;
        q->size_end = 0.02f;
        q->c = col_with_alpha(c2, 0.9f);
        q->add = 1;
    }
}

static void update_visuals(PlayState *ps, float dt)
{
    Player *p = &ps->p;
    const float px = sim_x(p), py = sim_y(p);
    float gdir = (float)p->grav;
    Color c1 = g_player_colors[g_game.save.col1 % PLAYER_COLOR_COUNT];

    update_motion(ps, dt, 0);

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
        fx_burst(FX_WORLD, px, py - 0.5f * gdir, 4, 3.0f, 0.12f, 0.25f, col_with_alpha(c1, 0.8f), 0);
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

/* A resumed run's music goes on with its next tick, not with the tick
 * that resumed it: neither the tick that paused nor that one moves the
 * run, so the song would otherwise be a tick ahead after every pause. */
static int s_resume_music;

/* A pause game_suspend asked for as a run was being faded into. It opens on
 * the run's first tick, as START would there: opened with the run's start,
 * before its song has played a tick, the song would come back a tick late
 * (see s_resume_music). */
static int s_pause_asked;

static void pause_run(PlayState *ps)
{
    ps->paused = 1;
    ps->pause_sel = 0;
    audio_pause(1);
    s_pause_asked = 0;
}

void play_suspend(int starting)
{
    PlayState *ps = ps_get();
    if (starting) s_pause_asked = 1;
    else if (!ps->paused && ps->phase != PH_COMPLETE) pause_run(ps);
}

static void pause_tick(PlayState *ps)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) { ps->pause_sel = (ps->pause_sel + 3) % 4; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_DOWN) { ps->pause_sel = (ps->pause_sel + 1) % 4; audio_sfx(SFX_MENU_MOVE); }
    if (g->pressed & (BTN_START | BTN_CIRCLE)) {
        ps->paused = 0;
        audio_sfx(SFX_MENU_BACK);
        s_resume_music = 1;
        s_jump_lock = g->pressed & JUMP_BUTTONS;
        return;
    }
    if (!(g->pressed & BTN_CROSS)) return;
    audio_sfx(SFX_MENU_SELECT);
    s_jump_lock = g->pressed & JUMP_BUTTONS;
    switch (ps->pause_sel) {
    case 0: /* resume */
        ps->paused = 0;
        s_resume_music = 1;
        break;
    case 1: /* restart */
        ps->paused = 0;
        audio_pause(0);
        if (ps->phase == PH_RUN && ps->p.ticks > PROGRESS_LEFT_TICKS) on_death(ps); /* counts as one */
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
        if (ps->phase == PH_RUN && ps->p.ticks > PROGRESS_LEFT_TICKS) on_death(ps);
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
    s_jump_lock &= g->held;
    if (ps->paused) {
        pause_tick(ps);
        return;
    }
    if (((g->pressed & BTN_START) || s_pause_asked) && ps->phase != PH_COMPLETE) {
        if (!s_pause_asked) audio_sfx(SFX_MENU_SELECT);
        pause_run(ps);
        return;
    }
    if (s_resume_music) {
        s_resume_music = 0;
        audio_pause(0);
    }

    switch (ps->phase) {
    case PH_RUN: {
        uint32_t jb = JUMP_BUTTONS & ~s_jump_lock;
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
        /* (until it is off the screen: on and on, x would overflow) */
        if (sim_x(&ps->p) < ps->cam_x + SCREEN_W / BLOCK_PX + 4.0f)
            sim_coast(&ps->p, (uint16_t)lroundf(ps->phase_t * 60.0f));
        update_motion(ps, dt, 1);
        if (ps->phase_t > 0.3f && ps->phase_t < 3.5f && ((int)(ps->phase_t * 60.0f) % 14) == 0) spawn_firework(ps);
        ps->flash = maxf(0.0f, ps->flash - dt * 1.5f);
        if (ps->phase_t > 1.6f) {
            if (g->repeat & (BTN_LEFT | BTN_RIGHT)) { ps->results_sel ^= 1; audio_sfx(SFX_MENU_MOVE); }
            if (g->pressed & BTN_CROSS) {
                audio_sfx(SFX_MENU_SELECT);
                if (ps->results_sel == 0) screen_go(SCR_SELECT);
                else {
                    s_jump_lock = g->pressed & JUMP_BUTTONS;
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

/* --- also used by the title screen's demo run (demo.c) -------------- */

void play_camera(const PlayState *ps, float *x, float *y)
{
    const Game *g = &g_game;
    const float a = g->alpha;
    float sh = ps->shake * 0.6f;
    *x = lerpf(ps->prev_cam_x, ps->cam_x, a) + (sh > 0 ? (hash_f01((uint32_t)(g->t * 977.0f)) - 0.5f) * sh : 0.0f);
    *y = lerpf(ps->prev_cam_y, ps->cam_y, a) + (sh > 0 ? (hash_f01((uint32_t)(g->t * 731.0f) + 9u) - 0.5f) * sh : 0.0f);
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
    ps->cam_x = sim_x(&ps->p) - CAM_PLAYER_X;
    ps->cam_y = ps->cam_target_y = CAM_GROUND_Y;
    ps->phase = PH_RUN;
    snap_prev(ps);
}
