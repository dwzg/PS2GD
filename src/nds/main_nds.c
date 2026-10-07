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
#include <stdio.h>

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
    /* the last 8 late frames: which, on what screen, and what each part took */
    struct {
        uint32_t frame, screen, tick, draw, audio;
    } late_at[8];
} NdsStats;
volatile NdsStats g_nds_stats = {.magic = 0x534E4450u};

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

static uint32_t clock_now(void)
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
    /* the top screen: the 3D engine on BG0; the bottom one: text for now */
    videoSetMode(MODE_0_3D);
    lcdMainOnTop();
    consoleDemoInit();
    gfx_nds_init();
    irqSet(IRQ_VBLANK, on_vblank);
    irqEnable(IRQ_VBLANK);
    clock_init();

    int save_ok = save_nds_init(argc, argv);
    int snd_ok = audio_nds_init();
    printf("Pulse Dash (DS)\nsave: %s, sound: %s\n", save_ok ? "ok" : "none", snd_ok ? "ok" : "none");
    game_init();
    gfx_nds_prepare_outlines();

    swiWaitForVBlank();
    uint32_t shown = s_vblanks;
    uint32_t sum = 0;
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
        audio_nds_update(t2 - t0 < FRAME_CYCLES / 2);
        uint32_t work = clock_now() - t0;
        g_nds_stats.audio_last = clock_now() - t2;
        /* (the maxima leave out the first second: start-up) */
        if (g_nds_stats.frames < 60) g_nds_stats.tick_max = g_nds_stats.draw_max = g_nds_stats.audio_max = 0;
        if (g_nds_stats.tick_last > g_nds_stats.tick_max) g_nds_stats.tick_max = g_nds_stats.tick_last;
        if (g_nds_stats.draw_last > g_nds_stats.draw_max) g_nds_stats.draw_max = g_nds_stats.draw_last;
        if (g_nds_stats.audio_last > g_nds_stats.audio_max) g_nds_stats.audio_max = g_nds_stats.audio_last;

        swiWaitForVBlank();
        gfx_nds_vblank();

        g_nds_stats.frames++;
        if (s_vblanks - now > 1) {
            int k = (int)(g_nds_stats.late % 8);
            g_nds_stats.late_at[k].frame = g_nds_stats.frames;
            g_nds_stats.late_at[k].screen = (uint32_t)g_game.screen;
            g_nds_stats.late_at[k].tick = g_nds_stats.tick_last;
            g_nds_stats.late_at[k].draw = g_nds_stats.draw_last;
            g_nds_stats.late_at[k].audio = g_nds_stats.audio_last;
            g_nds_stats.late++;
        }
        g_nds_stats.work_last = work;
        if (work > g_nds_stats.work_max && g_nds_stats.frames >= 60) g_nds_stats.work_max = work;
        g_nds_stats.attempts = game_attempts_started();
        sum += work;
        if (g_nds_stats.frames % 300 == 0) {
            int polys = gfx_nds_max_polys();
            if ((uint32_t)polys > g_nds_stats.polys_max) g_nds_stats.polys_max = (uint32_t)polys;
            g_nds_stats.dropped = (uint32_t)gfx_nds_dropped();
            char status[96];
            game_status(status, sizeof(status));
            printf("%lu: %lu%% avg, %lu%% max, late %lu\n polys %d, %s\n", (unsigned long)g_nds_stats.frames,
                   (unsigned long)(sum / 300 * 100 / FRAME_CYCLES), (unsigned long)(g_nds_stats.work_max * 100 / FRAME_CYCLES),
                   (unsigned long)g_nds_stats.late, polys, status);
            sum = 0;
        }
    }
    return 0;
}
