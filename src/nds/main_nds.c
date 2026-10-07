/*
 * Nintendo DS entry point.
 *
 * Frame pacing: the DS shows 59.83 frames a second, and the game ticks
 * once a frame, as on the Game Boy Advance: every frame moves the same,
 * where ticking at 60 Hz would take two ticks in one frame every six
 * seconds. The game and its music run 0.29% slower than on the other
 * versions, in step with each other (the songs were recorded so,
 * src/host/nds_audio.c). A frame that runs late is caught up with extra
 * ticks. Each frame the loop reads the pad, ticks, draws, writes the
 * sound ahead, then waits for the vertical blank, where the 3D engine
 * takes the frame's polygons (glFlush) and draws them as the next frame
 * is shown.
 */
#include <nds.h>

#include "nds_platform.h"
#include "../core/audio.h"
#include "../core/game.h"
#include "../core/game_internal.h"

#define MAX_TICKS_PER_FRAME 5
/* the DS's refresh: 33513982 Hz / (263 lines * 355 dots * 6 cycles) */
#define FRAME_CYCLES 560190u /* bus cycles a frame */

/* For the emulator test (scripts/nds-emu-test.py reads it from memory):
 * per-frame figures, in cycles of the 33.5 MHz bus clock. */
typedef struct {
    uint32_t magic;          /* 'PDNS' */
    uint32_t frames;         /* frames drawn */
    uint32_t late;           /* frames that missed their vertical blank */
    uint32_t work_last;      /* the last frame's ticks and drawing */
    uint32_t work_max;       /* the most since boot */
    uint32_t polys_max;      /* the most polygons in a frame since boot */
    uint32_t dropped;        /* primitives left out since boot */
    uint32_t attempts;       /* game_attempts_started() */
    uint32_t ticks;          /* game ticks since boot */
    uint32_t tick_last;      /* of work_last: the ticks */
    uint32_t draw_last;      /* and the drawing (game_render) */
    uint32_t audio_last;     /* and the sound (audio_nds_update) */
    uint32_t tick_max, draw_max, audio_max; /* the most each took since boot */
    uint32_t sum;                /* all the frames' work, for the mean */
    uint32_t save_ok, sound_ok;  /* the save and the sound found their places */
    /* the last 8 late frames: which, on what screen, and what each part took */
    struct {
        uint32_t frame, screen, tick, draw, audio, bottom;
    } late_at[8];
} NdsStats;
volatile NdsStats g_nds_stats = {.magic = 0x534E4450u};

/* Where the emulator test (src/host/nds_test.c) finds the game's state:
 * the addresses of what it reads, then two sizes (its Player and Progress
 * must be laid out as the ROM's). */
__attribute__((used)) const void *const g_test_info[] = {
    &g_game.screen, &g_game.fade, &g_game.sel_level, &g_game.play.phase, &g_game.play.p,
    &g_game.save.progress, &g_game.play.paused, &g_game.play.results_sel, &g_game.menu_sel,
    &g_game.play.level_idx, &g_game.play.practice, &g_game.play.phase_t, &g_game.sel_scroll,
    (const void *)&g_audio_song, (const void *)&g_audio_heard, (const void *)&g_nds_stats,
    (const void *)sizeof(Player), (const void *)sizeof(Progress),
};

static volatile uint32_t s_vblanks;

static void on_vblank(void)
{
    s_vblanks++;
}

/* a free-running count of the bus clock: timers 2 and 3, cascaded */
static void clock_init(void)
{
    TIMER2_CR = 0;
    TIMER3_CR = 0;
    TIMER2_DATA = 0;
    TIMER3_DATA = 0;
    TIMER3_CR = TIMER_ENABLE | TIMER_CASCADE;
    TIMER2_CR = TIMER_ENABLE | TIMER_DIV_1;
}

uint32_t nds_clock(void);
static uint32_t clock_now(void)
{
    return nds_clock();
}

uint32_t nds_clock(void)
{
    uint16_t hi, lo, hi2;
    do {
        hi = TIMER3_DATA;
        lo = TIMER2_DATA;
        hi2 = TIMER3_DATA;
    } while (hi != hi2);
    return ((uint32_t)hi << 16) | lo;
}

static uint32_t read_pad(void)
{
    scanKeys();
    uint32_t k = keysHeld(), b = 0;
    if (k & KEY_A) b |= BTN_CROSS;
    if (k & KEY_B) b |= BTN_CIRCLE;
    if (k & KEY_Y) b |= BTN_SQUARE;
    if (k & KEY_X) b |= BTN_TRIANGLE;
    if (k & KEY_UP) b |= BTN_UP;
    if (k & KEY_DOWN) b |= BTN_DOWN;
    if (k & KEY_LEFT) b |= BTN_LEFT;
    if (k & KEY_RIGHT) b |= BTN_RIGHT;
    if (k & KEY_L) b |= BTN_L1;
    if (k & KEY_R) b |= BTN_R1;
    if (k & KEY_START) b |= BTN_START;
    if (k & KEY_SELECT) b |= BTN_SELECT;
    /* the touch screen jumps: as L2, a jump button that does nothing in
     * the menus */
    if (k & KEY_TOUCH) b |= BTN_L2;
    return b;
}

int main(int argc, char **argv)
{
    /* on a DSi (or a 3DS) that runs the game as a DSi game, the ARM9 at its
     * 133 MHz, twice the DS's: the same game with time to spare */
    if (isDSiMode()) setCpuClock(true);
    /* the top screen: the 3D engine on BG0; the bottom one: a bitmap the
     * CPU draws (bottom_nds.c) */
    videoSetMode(MODE_0_3D);
    lcdMainOnTop();
    gfx_nds_init();
    bottom_nds_init();
    irqSet(IRQ_VBLANK, on_vblank);
    irqEnable(IRQ_VBLANK);
    clock_init();

    int save_ok = save_nds_init(argc, argv);
    int snd_ok = audio_nds_init();
    g_nds_stats.save_ok = (uint32_t)save_ok;
    g_nds_stats.sound_ok = (uint32_t)snd_ok;
    game_init();
    gfx_nds_prepare_outlines();

    swiWaitForVBlank();
    uint32_t shown = s_vblanks;
    for (;;) {
        uint32_t t0 = clock_now();
        /* a tick a frame shown: one, or as many as the last frame took
         * (it was late: the game catches up) */
        uint32_t now = s_vblanks, elapsed = now - shown;
        shown = now;
        int n = elapsed < 1 ? 1 : elapsed > MAX_TICKS_PER_FRAME ? MAX_TICKS_PER_FRAME : (int)elapsed;
        uint32_t held = read_pad();
        for (int k = 0; k < n; k++) game_tick(held);
        g_nds_stats.ticks += (uint32_t)n;
        uint32_t t1 = clock_now();
        gfx_nds_begin();
        game_render(1.0f);
        gfx_nds_end();
        g_nds_stats.tick_last = t1 - t0;
        g_nds_stats.draw_last = clock_now() - t1;
        /* the sound written ahead; the songs read ahead while the frame
         * has time for it (a read takes up to a few milliseconds) */
        uint32_t t2 = clock_now();
        audio_nds_update(t0 + FRAME_CYCLES - FRAME_CYCLES / 8);
        g_nds_stats.audio_last = clock_now() - t2;
        /* the bottom screen, when what it shows changed and the frame has
         * time for it */
        /* the bottom screen, drawn in what is left of the frame (but for
         * an eighth, against the clock's reading and what runs after) */
        uint32_t t3 = clock_now();
        bottom_nds_update(t0 + FRAME_CYCLES - FRAME_CYCLES / 8);
        uint32_t work = clock_now() - t0, bottom = clock_now() - t3;
        /* (the maxima leave out the first second: start-up) */
        if (g_nds_stats.frames < 60) g_nds_stats.tick_max = g_nds_stats.draw_max = g_nds_stats.audio_max = 0;
        if (g_nds_stats.tick_last > g_nds_stats.tick_max) g_nds_stats.tick_max = g_nds_stats.tick_last;
        if (g_nds_stats.draw_last > g_nds_stats.draw_max) g_nds_stats.draw_max = g_nds_stats.draw_last;
        if (g_nds_stats.audio_last > g_nds_stats.audio_max) g_nds_stats.audio_max = g_nds_stats.audio_last;

        swiWaitForVBlank();
        gfx_nds_vblank();
        bottom_nds_vblank();

        g_nds_stats.frames++;
        if (s_vblanks - now > 1) {
            int k = (int)(g_nds_stats.late % 8);
            g_nds_stats.late_at[k].frame = g_nds_stats.frames;
            g_nds_stats.late_at[k].screen = (uint32_t)g_game.screen;
            g_nds_stats.late_at[k].tick = g_nds_stats.tick_last;
            g_nds_stats.late_at[k].draw = g_nds_stats.draw_last;
            g_nds_stats.late_at[k].audio = g_nds_stats.audio_last;
            g_nds_stats.late_at[k].bottom = bottom;
            g_nds_stats.late++;
        }
        g_nds_stats.work_last = work;
        if (work > g_nds_stats.work_max && g_nds_stats.frames >= 60) g_nds_stats.work_max = work;
        g_nds_stats.attempts = game_attempts_started();
        g_nds_stats.sum += work;
        uint32_t polys = (uint32_t)gfx_nds_max_polys();
        if (polys > g_nds_stats.polys_max) g_nds_stats.polys_max = polys;
        g_nds_stats.dropped = (uint32_t)gfx_nds_dropped();
    }
    return 0;
}
