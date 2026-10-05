/*
 * PSP entry point.
 *
 * Frame pacing: the GE draws each frame's display list as it is built; the
 * loop waits for it to finish (and to smooth the frame, when there is time
 * for that before the vblank: gfx_gu.c), sleeps until the next vblank
 * starts and flips there. Game logic ticks at a fixed 60 Hz; each frame is drawn
 * interpolated to the moment it will be on screen (the LCD runs at
 * 59.94 Hz). The loop runs at the main thread's priority (0x20), above the
 * thread that mixes the audio and the save thread, so nothing delays a flip
 * past the vblank (the one that hands the audio over, above it, only wakes
 * for a moment: audio_psp.c).
 *
 * HOME > Quit runs exit_callback, which must not return before the game
 * is done (the system powers down then): it has the loop finish its frame
 * and write any unsaved progress, then exits the game itself.
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <psppower.h>
#include <psphprm.h>
#include <stdio.h>

#include "psp_platform.h"
#include "../core/audio.h"
#include "../core/game.h"
#include "../core/game_internal.h"

PSP_MODULE_INFO("PulseDash", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(4096);

#define MAX_TICKS_PER_FRAME 5

static volatile int s_quit, s_saved;

static int exit_callback(int arg1, int arg2, void *common)
{
    (void)arg1;
    (void)arg2;
    (void)common;
    s_quit = 1;
    for (int ms = 0; !s_saved && ms < 4000; ms += 10) sceKernelDelayThread(10000);
    printf("pulsedash: exit\n");
    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    int cb = sceKernelCreateCallback("pd_exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

#ifdef PD_PERF
/* Per-phase timings in microseconds, printed every few seconds (make PERF=1). */
enum { PF_TICK, PF_DRAW, PF_GE, PF_IDLE, PF_FLIP, PF_COUNT };
static const char *const PF_NAMES[PF_COUNT] = {"tick", "draw", "ge", "idle", "flip"};
static unsigned s_pf_sum[PF_COUNT], s_pf_max[PF_COUNT];

static void perf_add(int phase, unsigned us)
{
    s_pf_sum[phase] += us;
    if (us > s_pf_max[phase]) s_pf_max[phase] = us;
}

static void perf_report(unsigned frames, unsigned late, float refresh)
{
    static unsigned last_mix;
    unsigned mix = audio_psp_mix_us();
    float frame_us = 1000000.0f / refresh;
    char status[128];
    game_status(status, sizeof(status));
    printf("pulsedash: perf %u frames, %u late |", frames, late);
    for (int i = 0; i < PF_COUNT; i++) {
        printf(" %s %.1f/%.1f%%", PF_NAMES[i], 100.0f * s_pf_sum[i] / frames / frame_us,
               100.0f * s_pf_max[i] / frame_us);
        s_pf_sum[i] = s_pf_max[i] = 0;
    }
    printf(" | audio %.1f%% | %s\n", 100.0f * (mix - last_mix) / frames / frame_us, status);
    last_mix = mix;
    /* the smoothing (gfx_gu.c): frames drawn twice, the second drawing's
     * and the blend's GE time (mean/most, % of a frame), frames that had no
     * time for it or ended too close to the vblank */
    GfxPspStats st;
    gfx_psp_stats(&st);
    unsigned n = st.frames ? st.frames : 1;
    printf("pulsedash: smooth %u/%u frames | again %.1f/%.1f%% blend %.1f/%.1f%% | %u skipped, %u close | %u batches, "
           "%u clears\n",
           st.frames, frames, 100.0f * st.again_us / n / frame_us, 100.0f * st.again_max_us / frame_us,
           100.0f * st.blend_us / n / frame_us, 100.0f * st.blend_max_us / frame_us, st.skipped, st.close,
           st.batches_max, st.clears);
}
#define PERF_MARK(var) unsigned var = sceKernelGetSystemTimeLow()
#define PERF_ADD(phase, from, to) perf_add(phase, (to) - (from))
#else
#define PERF_MARK(var)
#define PERF_ADD(phase, from, to)
#endif

static int s_auto_out = -1; /* OUTPUT on AUTO: the mix set, -1 none yet */

int main(int argc, char *argv[])
{
    SceUID cb = sceKernelCreateThread("pd_callbacks", callback_thread, 0x11, 0x1000, PSP_THREAD_ATTR_USER, NULL);
    if (cb >= 0) sceKernelStartThread(cb, 0, NULL);
    /* the fastest clocks: steady frames matter more than battery here */
    scePowerSetClockFrequency(333, 333, 166);

    printf("pulsedash: boot (%s)\n", argc > 0 ? argv[0] : "?");
    gfx_psp_init();
    pad_psp_init();
    int save_ok = save_psp_init(argc > 0 ? argv[0] : NULL);
    audio_init();
    int snd_ok = audio_psp_init();
    printf("pulsedash: init memstick=%d audio=%d\n", save_ok, snd_ok);
    game_init();

    const float refresh = sceDisplayGetFramePerSec();
    const float frame_dt = 1.0f / (refresh > 1.0f ? refresh : 59.94f);
    const unsigned frame_us = (unsigned)(frame_dt * 1000000.0f);
    /* how far the simulation runs ahead of the frame being drawn, in seconds
     * (0 <= ahead < one tick) */
    float ahead = 0.0f;
    sceDisplayWaitVblankStart();
    unsigned vblank_us = sceKernelGetSystemTimeLow();
    unsigned shown = sceDisplayGetVcount();
    unsigned elapsed = 1; /* vblanks between the last two flips */
    int home = 0;         /* the HOME dialog is up (pad_psp_home) */
#ifdef PD_PERF
    unsigned perf_frames = 0, perf_late = 0, attempts = 0;
#endif
    while (!s_quit) {
        PERF_MARK(t0);
        uint32_t held = pad_psp_read();
        if (pad_psp_home() != home) {
            /* while the system's HOME dialog is up the game stands still
             * and is silent, a run paused (game_suspend); its time stands
             * still too: no ticks, and none made up when the dialog goes */
            home = !home;
            game_suspend(home);
            audio_suspend(home);
            elapsed = 1; /* nor a vblank missed while it was up */
        }
        /* this frame goes up `elapsed` vblanks after the previous one (one,
         * unless the previous frame missed its vblank) */
        if (!home) ahead -= frame_dt * (float)(elapsed < 4 ? elapsed : 4);
        int n = 0;
        while (ahead < 0.0f && n < MAX_TICKS_PER_FRAME) {
            game_tick(held);
            ahead += TICK_DT;
            n++;
        }
        if (ahead < 0.0f) ahead = 0.0f; /* long stall (suspend): skip ahead */
        /* OUTPUT on AUTO: the speakers' mix unless headphones are plugged
         * in (looked at 6 times a second; the mix changes only when it
         * changes) */
        if (g_game.save.speaker == OUTPUT_AUTO && (shown % 10 == 0 || s_auto_out < 0)) {
            int spk = !sceHprmIsHeadphoneExist();
            if (spk != s_auto_out) audio_set_output(spk);
            s_auto_out = spk;
        } else if (g_game.save.speaker != OUTPUT_AUTO) {
            s_auto_out = -1;
        }
        PERF_MARK(t1);
        gfx_psp_begin(vblank_us + frame_us);
        game_render(1.0f - ahead / TICK_DT);
        gfx_psp_submit();
        PERF_MARK(t2);
        gfx_psp_sync();
        PERF_MARK(t3);
        sceDisplayWaitVblankStart();
        vblank_us = sceKernelGetSystemTimeLow();
        unsigned now = sceDisplayGetVcount();
        PERF_MARK(t4);
        gfx_psp_flip();
        elapsed = now - shown;
        shown = now;
#ifdef PD_PERF
        PERF_MARK(t5);
        PERF_ADD(PF_TICK, t0, t1);
        PERF_ADD(PF_DRAW, t1, t2);
        PERF_ADD(PF_GE, t2, t3);
        PERF_ADD(PF_IDLE, t3, t4);
        PERF_ADD(PF_FLIP, t4, t5);
        perf_late += elapsed > 1;
        if (game_attempts_started() != attempts) {
            /* scripted test runs (scripts/psp-emu-test.sh) time their input from this */
            attempts = game_attempts_started();
            printf("PD_MARK attempt %u\n", attempts);
        }
        if (++perf_frames == 300) {
            perf_report(perf_frames, perf_late, refresh);
            if (gfx_psp_dropped()) printf("pulsedash: %u primitives dropped\n", gfx_psp_dropped());
            perf_frames = perf_late = 0;
        }
#endif
    }
    /* HOME > Quit: save, then exit_callback ends the game */
    game_flush_save();
    save_psp_flush(3000);
    s_saved = 1;
    sceKernelSleepThread();
    return 0;
}
