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
#include <stdio.h>

#include "ps2_platform.h"
#include "../core/audio.h"
#include "../core/game.h"

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

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    reset_iop();

    if (gfx_ps2_init() < 0) {
        printf("pulsedash: gsKit init failed\n");
        SleepThread();
    }
    pad_ps2_init();
    save_ps2_init();
    audio_init();
    audio_ps2_init();
    game_init();

    const float frame_dt = gfx_ps2_is_pal() ? 1.0f / 50.0f : 1.0f / 60.0f;
    float acc = 0.0f;
    for (;;) {
        uint32_t held = pad_ps2_read();
        acc += frame_dt;
        int n = 0;
        while (acc >= TICK_DT * 0.999f && n < 3) {
            game_tick(held);
            acc -= TICK_DT;
            n++;
        }
        if (n == 3) acc = 0.0f;
        gfx_ps2_begin();
        game_render();
        gfx_ps2_end();
    }
    return 0;
}
