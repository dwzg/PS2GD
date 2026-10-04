/*
 * Audio output on the PSP's SRC channel, which takes the synthesizer's
 * 48 kHz directly and resamples it in hardware.
 *
 * A thread synthesizes a chunk while the previous one plays, then blocks
 * handing it over until the hardware has room: two chunks of 512 frames,
 * about 11 ms each. It runs below the game loop, which sleeps through most
 * of each frame (waiting for the GE and the vblank), so mixing fills that
 * time and never delays a flip; the loop's few milliseconds of work a frame
 * are well inside a chunk.
 */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psputils.h>
#include <stdio.h>

#include "psp_platform.h"
#include "../core/audio.h"

#define CHUNK_FRAMES 512 /* a multiple of 64, as the hardware wants */
#define AUDIO_THREAD_PRIORITY 0x28 /* below the game loop (main_psp.c, 0x20) */
#define AUDIO_STACK 0x10000

static int16_t s_buf[2][CHUNK_FRAMES * 2] __attribute__((aligned(64)));
static volatile unsigned s_mix_us;

static int audio_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    for (int i = 0;; i ^= 1) {
#ifdef PD_PERF
        unsigned t0 = sceKernelGetSystemTimeLow();
        audio_mix(s_buf[i], CHUNK_FRAMES);
        s_mix_us += sceKernelGetSystemTimeLow() - t0;
#else
        audio_mix(s_buf[i], CHUNK_FRAMES);
#endif
        sceKernelDcacheWritebackRange(s_buf[i], sizeof(s_buf[i]));
        /* returns once the hardware has taken the chunk; the other buffer
         * is free again by then */
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, s_buf[i]);
    }
    return 0;
}

unsigned audio_psp_mix_us(void)
{
    return s_mix_us;
}

int audio_psp_init(void)
{
    int r = sceAudioSRCChReserve(CHUNK_FRAMES, AUDIO_RATE, 2);
    if (r < 0) {
        printf("pulsedash: sceAudioSRCChReserve failed (%08x)\n", (unsigned)r);
        return -1;
    }
    /* a chunk is mixed up to one chunk before it starts playing, then
     * plays for one more; plus the resampler */
    audio_set_latency(1.5f * CHUNK_FRAMES / AUDIO_RATE + 0.005f);

    SceUID th = sceKernelCreateThread("pd_audio", audio_thread, AUDIO_THREAD_PRIORITY, AUDIO_STACK,
                                      PSP_THREAD_ATTR_USER, NULL);
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        printf("pulsedash: audio thread failed\n");
        return -1;
    }
    return 0;
}
