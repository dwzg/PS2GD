/*
 * The gameplay behind the title screen: the real engine plays a short level
 * on a loop, pressing on the beat of the menu music as a player would.
 *
 * The level is DEMO_LOOP columns long, 8 bars of the menu song at normal
 * speed, and is followed by a copy of its first DEMO_TAIL columns. When the
 * player passes DEMO_WRAP the run moves back by DEMO_LOOP, which looks the
 * same. The presses come from the rhythm solver (`pd_tool demo gen` prints
 * them, `pd_tool demo` checks them) and are keyed by position, so the run
 * can be nudged to follow the music without missing a jump.
 */
#include "game_internal.h"
#include "audio.h"
#include "fx.h"

#define DEMO_CAM_DROP 1.2f /* the menu covers the top of the screen: show the run lower */

/* 8 bars of the menu song (112 BPM) at normal speed, 5.625 blocks a beat
 * (`pd_tool ruler demo` shows the grid). The first 14 columns stay empty:
 * the run wraps there, from x = DEMO_WRAP back to x = 8. */
static const char *const DEMO_ART[] = {
"#name Title demo",
"#speed 1",
/* bars 1-2 */
"|                                             ",
"|                                             ",
"|                                         ^   ",
"|                  ^     ^    ^^     #########",
"",
/* bars 3-4 */
"|                                             ",
"|                                             ",
"|                   #######                   ",
"|###    ^      ############ ^^ Y ^^^      ^   ",
"",
/* bars 5-6 */
"|                                             ",
"|  y                                          ",
"|                         #####               ",
"|  ^^^^^      ^^     ########## ^^   ^     ###",
"",
/* bars 7-8 */
"|                                             ",
"|                                             ",
"|                                             ",
"|###### ^    ^^    ^    ^^                    ",
"",
NULL,
};

/* Button held while the player's x is in [x0, x1). The table is printed by
 * `pd_tool demo gen`: after changing the level, regenerate it and check it
 * with `pd_tool demo`. */
typedef struct {
    float x0, x1;
} DemoPress;

static const DemoPress DEMO_PRESSES[] = {
    {16.537f, 17.062f},
    {22.488f, 23.013f},
    {27.738f, 27.913f},
    {33.688f, 34.038f},
    {39.288f, 39.813f},
    {50.488f, 50.838f},
    {56.088f, 56.613f},
    {61.863f, 62.388f},
    {70.088f, 70.613f},
    {84.262f, 84.437f},
    {89.861f, 90.211f},
    {92.661f, 92.836f},
    {101.586f, 102.111f},
    {107.010f, 107.185f},
    {112.435f, 112.785f},
    {118.385f, 118.909f},
    {123.984f, 124.159f},
    {129.409f, 129.759f},
    {140.258f, 140.783f},
    {145.858f, 146.033f},
    {151.632f, 151.982f},
    {157.057f, 157.582f},
};

#define NPRESS ((int)(sizeof(DEMO_PRESSES) / sizeof(DEMO_PRESSES[0])))

const char *const *demo_level_src(void)
{
    enum { NART = sizeof(DEMO_ART) / sizeof(DEMO_ART[0]) - 1, MAXROWS = 8 };
    static const char *src[NART + MAXROWS + 2];
    static char tail[MAXROWS][DEMO_TAIL + 2];
    if (src[0]) return src;
    int n = 0, first = -1, rows = 0;
    for (int i = 0; i < NART; i++) {
        src[n++] = DEMO_ART[i];
        if (DEMO_ART[i][0] == '|' && (first < 0 || i == first + rows) && rows < MAXROWS) {
            if (first < 0) first = i;
            rows++;
        }
    }
    /* the first section's first DEMO_TAIL columns again, as one more section */
    for (int r = 0; r < rows; r++) {
        memcpy(tail[r], DEMO_ART[first + r], DEMO_TAIL + 1);
        tail[r][DEMO_TAIL + 1] = 0;
        src[n++] = tail[r];
    }
    src[n] = NULL;
    return src;
}

/* A run of the demo level: the player plus the button state. The title
 * screen's run uses the PlayState's player; a second one plays ahead in the
 * background to record the snapshots below. */
typedef struct {
    Player *p;
    float last_x; /* x at the previous tick */
    int held;
} Runner;

static float s_main_last_x;
static int s_main_held, s_broken;

/* Snapshots of the run every SNAP_EVERY blocks of the first time round, so
 * that following the music to a new place replays a few blocks rather than
 * the level up to there. */
#define SNAP_EVERY 15
#define NSNAP (DEMO_LOOP / SNAP_EVERY)
#define AHEAD_TICKS 24 /* background run's ticks per frame */
typedef struct {
    Player p;
    float last_x;
    int held, valid;
} DemoSnap;
static DemoSnap s_snap[NSNAP];
static Player s_ahead;
static float s_ahead_last_x;
static int s_ahead_held, s_ahead_on;

/* Is the button down at x? A press the player stepped over entirely since
 * the last tick (when the run was nudged forward) still counts. */
static int demo_button(float x, float last_x)
{
    for (int i = 0; i < NPRESS; i++) {
        const DemoPress *d = &DEMO_PRESSES[i];
        if ((x >= d->x0 && x < d->x1) || (last_x < d->x0 && x >= d->x1)) return 1;
    }
    return 0;
}

static void step(Runner *r, const Level *L)
{
    int h = demo_button(r->p->x, r->last_x);
    r->last_x = r->p->x;
    sim_tick(r->p, L, h, h && !r->held);
    r->held = h;
    int k = (int)floorf(r->p->x / SNAP_EVERY);
    if (k >= 0 && k < NSNAP && !s_snap[k].valid && floorf(r->last_x / SNAP_EVERY) < k && !r->p->dead) {
        DemoSnap *sn = &s_snap[k];
        sn->p = *r->p;
        sn->last_x = r->last_x;
        sn->held = r->held;
        sn->valid = 1;
    }
}

/* Where the player is on the loop (in [DEMO_WRAP - DEMO_LOOP, DEMO_WRAP))
 * at this beat of the menu song: beat 0 is x = 0. */
static float x_of_beat(float beat)
{
    float x = fmodf(beat * SIM_SPEEDS[1] * 60.0f / audio_song_bpm(SONG_MENU), (float)DEMO_LOOP);
    if (x < 0.0f) x += DEMO_LOOP;
    if (x < DEMO_WRAP - DEMO_LOOP) x += DEMO_LOOP;
    return x;
}

/* Play up to x (no effects on the way), from the last snapshot before it. */
static void seek(PlayState *ps, float x)
{
    if (x >= DEMO_LOOP) x -= DEMO_LOOP; /* the same place on the first time round */
    Runner r = {&ps->p, 0.0f, 0};
    sim_reset(&ps->p, ps->L);
    r.last_x = ps->p.x;
    for (int k = mini((int)(x / SNAP_EVERY), NSNAP - 1); k >= 0; k--) {
        const DemoSnap *sn = &s_snap[k];
        if (sn->valid && sn->p.x <= x) {
            ps->p = sn->p;
            r.last_x = sn->last_x;
            r.held = sn->held;
            break;
        }
    }
    while (ps->p.x < x && !ps->p.dead) step(&r, ps->L);
    if (ps->p.dead) { /* a bad press table: stop following the music */
        s_broken = 1;
        sim_reset(&ps->p, ps->L);
        r.last_x = ps->p.x;
        r.held = 0;
    }
    s_main_last_x = r.last_x;
    s_main_held = r.held;
    play_place(ps);
}

static void shift(PlayState *ps, float dx)
{
    ps->p.x += dx;
    ps->prev_x += dx;
    ps->cam_x += dx;
    ps->prev_cam_x += dx;
    s_main_last_x += dx;
    for (int i = 0; i < ps->trail_n; i++) ps->trail_x[i] += dx;
    memset(ps->p.used, 0, sizeof(ps->p.used)); /* orbs and pads work again */
    fx_shift(FX_WORLD, dx);
}

int demo_tick(PlayState *ps, const float *beat)
{
    if (s_broken) beat = NULL;
    if (!ps->L) {
        ps->L = level_parse(demo_level_src());
        if (!ps->L) return 0;
        sim_reset(&s_ahead, ps->L);
        s_ahead_last_x = s_ahead.x;
        s_ahead_held = 0;
        s_ahead_on = 1;
        seek(ps, beat ? x_of_beat(*beat) : 0.0f);
    }
    if (s_ahead_on) {
        Runner r = {&s_ahead, s_ahead_last_x, s_ahead_held};
        for (int i = 0; i < AHEAD_TICKS && s_ahead.x < DEMO_LOOP && !s_ahead.dead; i++) step(&r, ps->L);
        s_ahead_last_x = r.last_x;
        s_ahead_held = r.held;
        s_ahead_on = s_ahead.x < DEMO_LOOP && !s_ahead.dead;
    }
    play_begin_tick(ps);
    if (beat) {
        /* follow the music: jump to it if far off (the song restarted),
         * else steer gently, which also hides the audio clock's jitter */
        float target = x_of_beat(*beat), d = target - ps->p.x;
        d -= DEMO_LOOP * floorf(d / DEMO_LOOP + 0.5f);
        if (fabsf(d) > 1.5f) seek(ps, target);
        else ps->p.x += clampf(d * 0.03f, -0.01f, 0.01f);
    }
    Runner r = {&ps->p, s_main_last_x, s_main_held};
    step(&r, ps->L);
    s_main_last_x = r.last_x;
    s_main_held = r.held;
    play_end_tick(ps);
    if (ps->p.x >= DEMO_WRAP) shift(ps, -(float)DEMO_LOOP);
    if (ps->p.dead) {
        seek(ps, beat ? x_of_beat(*beat) : DEMO_WRAP - DEMO_LOOP);
        return 1;
    }
    return 0;
}

void demo_render(const PlayState *ps, const Palette *pal)
{
    if (!ps->L) {
        draw_menu_backdrop(pal, g_game.t * 6.0f, beat_pulse());
        return;
    }
    View v;
    play_view(ps, &v);
    v.cam_y += DEMO_CAM_DROP;
    v.pal = pal;
    render_background(&v);
    render_ground(&v, 0.0f, CORRIDOR_H, 0.0f);
    render_level(&v, ps->L, &ps->p, 0);
    play_draw_player(ps, &v);
    fx_draw(FX_WORLD, v.cam_x, v.cam_y, (1.0f - g_game.alpha) * TICK_DT);
}

void demo_free(PlayState *ps)
{
    level_free(ps->L);
    ps->L = NULL;
}
