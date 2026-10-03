/*
 * PlayStation 2 entry point.
 *
 * Boot sequence: reset the IOP to a clean state, load the controller,
 * memory card and audio modules (embedded IRX images via ps2_drivers),
 * then run the game loop locked to vsync. Game logic always ticks at
 * 60 Hz; on PAL (50 Hz) consoles an accumulator runs extra ticks.
 */
#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <loadfile.h>
#include <stdio.h>

#include <gsKit.h>

#include "ps2_platform.h"
#include "../core/audio.h"
#include "../core/game.h"

#define MAIN_THREAD_PRIORITY 0x40

/* Counts vertical blanks so the loop knows how much real time passed. */
static volatile unsigned s_vblanks;

static int vblank_handler(int cause)
{
    (void)cause;
    s_vblanks++;
    ExitHandler();
    return 0;
}

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

    /*
     * The game loop busy-waits for vsync inside gsKit, so it must run below
     * the audio thread and the SDK's helper threads or it would starve them.
     */
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

    const float frame_dt = gfx_ps2_is_pal() ? 1.0f / 50.0f : 1.0f / 60.0f;
    float acc = 0.0f;
    gsKit_add_vsync_handler(vblank_handler);
    unsigned last_vblank = s_vblanks;
    unsigned frame = 0;
    char status[128];
    for (;;) {
        if ((++frame % 600) == 0) {
            game_status(status, sizeof(status));
            int ps, pr, po;
            pad_ps2_debug(&ps, &pr, &po);
            int al, aa, aq;
            audio_ps2_debug(&al, &aa, &aq);
            printf("pulsedash: frame %u %s pad(open=%d state=%d raw=%04x) audio(chunks=%u loops=%d avail=%d queued=%d)\n",
                   frame, status, po, ps, pr, audio_ps2_chunks(), al, aa, aq);
        }
        uint32_t held = pad_ps2_read();
        /* advance by the real number of vblanks (frame skipping keeps the
         * game and music in sync if a frame ever runs long) */
        unsigned now = s_vblanks;
        unsigned elapsed = now - last_vblank;
        last_vblank = now;
        if (elapsed < 1) elapsed = 1;
        if (elapsed > 4) elapsed = 4;
        acc += frame_dt * (float)elapsed;
        int n = 0;
        while (acc >= TICK_DT * 0.999f && n < 5) {
            game_tick(held);
            acc -= TICK_DT;
            n++;
        }
        if (n == 5) acc = 0.0f;
        gfx_ps2_begin();
        game_render();
        gfx_ps2_end();
    }
    return 0;
}
