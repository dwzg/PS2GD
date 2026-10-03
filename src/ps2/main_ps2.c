/*
 * PlayStation 2 entry point.
 *
 * Boot sequence: reset the IOP to a clean state, load the controller,
 * memory card and audio modules, then run the game loop.
 *
 * Frame pacing: the loop sleeps on a semaphore that the vblank interrupt
 * signals and flips right at the start of the vblank. It runs above the
 * audio thread, so the synthesizer only uses the time the loop spends
 * asleep and can never push a flip past the vblank. Game logic ticks at a
 * fixed 60 Hz; each frame is drawn interpolated to the moment it will be on
 * screen, which keeps the scrolling even at 59.94 Hz (NTSC) and 50 Hz (PAL).
 */
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <loadfile.h>
#include <stdio.h>
#include <string.h>

#include <gsKit.h>

#include "ps2_platform.h"
#include "../core/audio.h"
#include "../core/game.h"

#define MAIN_THREAD_PRIORITY 0x20 /* above the audio thread (audio_ps2.c) */
#define MAX_TICKS_PER_FRAME 5

static volatile unsigned s_vblanks;
static int s_vblank_sema = -1;

static int vblank_handler(int cause)
{
    (void)cause;
    s_vblanks++;
    iSignalSema(s_vblank_sema);
    ExitHandler();
    return 0;
}

/* Sleep until the next vblank starts; returns the vblank count. */
static unsigned wait_vblank(void)
{
    /* forget vblanks that passed while the frame was being built (the frame
     * missed them) so it is shown at the start of the next one */
    while (PollSema(s_vblank_sema) >= 0) {
    }
    WaitSema(s_vblank_sema);
    return s_vblanks;
}

#ifdef PD_PERF
/* Per-phase EE cycle statistics, printed every few seconds (make PERF=1). */
enum { PF_TICK, PF_DRAW, PF_SUBMIT, PF_IDLE, PF_FLIP, PF_COUNT };
static const char *const PF_NAMES[PF_COUNT] = {"tick", "draw", "submit", "idle", "flip"};
static unsigned s_pf_sum[PF_COUNT], s_pf_max[PF_COUNT];

static void perf_add(int phase, unsigned cycles)
{
    s_pf_sum[phase] += cycles;
    if (cycles > s_pf_max[phase]) s_pf_max[phase] = cycles;
}

static void perf_report(unsigned frames, unsigned late, float refresh)
{
    static unsigned last_mix;
    unsigned mix = audio_ps2_mix_cycles();
    float frame_cycles = PS2_EE_HZ / refresh;
    char status[128];
    game_status(status, sizeof(status));
    printf("pulsedash: perf %u frames, %u late |", frames, late);
    for (int i = 0; i < PF_COUNT; i++) {
        printf(" %s %.1f/%.1f%%", PF_NAMES[i], 100.0f * s_pf_sum[i] / frames / frame_cycles,
               100.0f * s_pf_max[i] / frame_cycles);
        s_pf_sum[i] = s_pf_max[i] = 0;
    }
    printf(" | audio %.1f%% | %s\n", 100.0f * (mix - last_mix) / frames / frame_cycles, status);
    last_mix = mix;
}
#define PERF_MARK(var) unsigned var = ps2_cycles()
#define PERF_ADD(phase, from, to) perf_add(phase, (to) - (from))
#else
#define PERF_MARK(var)
#define PERF_ADD(phase, from, to)
#endif

static void reset_iop(void)
{
    sceSifInitRpc(0);
    while (!SifIopReset("", 0)) {
    }
    while (!SifIopSync()) {
    }
    sceSifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
}

/*
 * Controller and memory card modules come from the console's BIOS, which
 * every PS2 (and emulator) provides. If that fails, fall back to the open
 * source modules embedded in the ELF.
 */
static int load_bios_io_modules(void)
{
    static const char *mods[] = {"rom0:SIO2MAN", "rom0:PADMAN", "rom0:MCMAN", "rom0:MCSERV"};
    for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
        int ret = SifLoadModule(mods[i], 0, NULL);
        if (ret < 0) {
            printf("pulsedash: %s failed (%d)\n", mods[i], ret);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    reset_iop();

    ChangeThreadPriority(GetThreadId(), MAIN_THREAD_PRIORITY);
    printf("pulsedash: boot\n");
    if (gfx_ps2_init() < 0) {
        printf("pulsedash: gsKit init failed\n");
        SleepThread();
    }
    int embedded = load_bios_io_modules() < 0;
    if (embedded) {
        /* a partial BIOS load would clash with the embedded modules */
        reset_iop();
    }
    int pad_ok = pad_ps2_init(embedded);
    int mc_ok = save_ps2_init(embedded);
    audio_init();
    int snd_ok = audio_ps2_init();
    printf("pulsedash: init modules=%s pad=%d memcard=%d audio=%d pal=%d\n", embedded ? "embedded" : "bios", pad_ok,
           mc_ok, snd_ok, gfx_ps2_is_pal());
    game_init();

    ee_sema_t sema;
    memset(&sema, 0, sizeof(sema));
    sema.init_count = 0;
    sema.max_count = 255;
    s_vblank_sema = CreateSema(&sema);
    gsKit_add_vsync_handler(vblank_handler);

    const float frame_dt = 1.0f / gfx_ps2_refresh_hz();
    /* how far the simulation runs ahead of the frame being drawn, in seconds
     * (0 <= ahead < one tick) */
    float ahead = 0.0f;
    unsigned shown = wait_vblank();
    unsigned elapsed = 1; /* vblanks between the last two flips */
#ifdef PD_PERF
    unsigned perf_frames = 0, perf_late = 0, attempts = 0;
#endif
    for (;;) {
        PERF_MARK(c0);
        uint32_t held = pad_ps2_read();
        /* this frame goes up `elapsed` vblanks after the previous one (one,
         * unless the previous frame missed its vblank) */
        ahead -= frame_dt * (float)(elapsed < 4 ? elapsed : 4);
        int n = 0;
        while (ahead < 0.0f && n < MAX_TICKS_PER_FRAME) {
            game_tick(held);
            ahead += TICK_DT;
            n++;
        }
        if (ahead < 0.0f) ahead = 0.0f; /* long stall (memory card): skip ahead */
        PERF_MARK(c1);
        gfx_ps2_begin();
        game_render(1.0f - ahead / TICK_DT);
        PERF_MARK(c2);
        gfx_ps2_submit();
        PERF_MARK(c3);
        unsigned now = wait_vblank();
        PERF_MARK(c4);
        gfx_ps2_flip();
        elapsed = now - shown;
        shown = now;
#ifdef PD_PERF
        unsigned c5 = ps2_cycles();
        PERF_ADD(PF_TICK, c0, c1);
        PERF_ADD(PF_DRAW, c1, c2);
        PERF_ADD(PF_SUBMIT, c2, c3);
        PERF_ADD(PF_IDLE, c3, c4);
        PERF_ADD(PF_FLIP, c4, c5);
        perf_late += elapsed > 1;
        if (game_attempts_started() != attempts) {
            /* scripted test runs (scripts/emu-test.sh) time their input from this */
            attempts = game_attempts_started();
            printf("PD_MARK attempt %u\n", attempts);
        }
        if (++perf_frames == 300) {
            perf_report(perf_frames, perf_late, gfx_ps2_refresh_hz());
            perf_frames = perf_late = 0;
        }
#endif
    }
    return 0;
}
