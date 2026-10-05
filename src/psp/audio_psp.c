/*
 * Audio output on the PSP's SRC channel, which takes the synthesizer's
 * 48 kHz directly and resamples it in hardware.
 *
 * Two threads share a ring of chunks of 512 frames, about 11 ms each. The
 * mixer synthesizes them ahead and runs below the game loop, so mixing
 * fills the loop's idle time (it sleeps through much of each frame, waiting
 * for the GE and the vblank) and never delays a flip. The feeder hands them
 * to the hardware and runs above the loop: it only waits, and wakes for a
 * moment as each chunk starts playing. With two chunks mixed ahead of the
 * one handed over, the loop can keep the CPU for over 20 ms before the
 * hardware runs dry. (The mixer handed its chunks over itself at first,
 * one mixed while the other played: when the level select's drawing kept
 * the CPU longer than that on the console, the music crackled and fell
 * behind, its clock being the samples mixed.)
 */
#include <pspkernel.h>
#include <pspaudio.h>
#include <psputils.h>
#include <stdio.h>

#include "psp_platform.h"
#include "../core/audio.h"

#define CHUNK_FRAMES 512 /* a multiple of 64, as the hardware wants */
#define CHUNKS 4 /* playing, handed over, two mixed ahead */
#define MIX_PRIORITY 0x28 /* below the game loop (main_psp.c, 0x20) */
#define FEED_PRIORITY 0x18 /* above it */
#define MIX_STACK 0x10000
#define FEED_STACK 0x1000

static int16_t s_buf[CHUNKS][CHUNK_FRAMES * 2] __attribute__((aligned(64)));
/* semaphores: chunks the mixer may write, chunks mixed and not handed over
 * yet */
static SceUID s_free, s_mixed;
static SceUID s_mix_thread;

static int mix_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    for (int i = 0;; i = (i + 1) % CHUNKS) {
        sceKernelWaitSema(s_free, 1, NULL);
        audio_mix(s_buf[i], CHUNK_FRAMES);
        sceKernelDcacheWritebackRange(s_buf[i], sizeof(s_buf[i]));
        sceKernelSignalSema(s_mixed, 1);
    }
    return 0;
}

static int feed_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    for (int i = 0;; i = (i + 1) % CHUNKS) {
        sceKernelWaitSema(s_mixed, 1, NULL);
        /* returns once the chunk is playing: the one before it is done
         * (before the first, the ring's last one is still unused) */
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX, s_buf[i]);
        sceKernelSignalSema(s_free, 1);
    }
    return 0;
}

unsigned audio_psp_mix_us(void)
{
    /* the mixer thread's own time (the game loop interrupts it) */
    SceKernelThreadRunStatus st;
    st.size = sizeof(st);
    if (s_mix_thread <= 0 || sceKernelReferThreadRunStatus(s_mix_thread, &st) < 0) return 0;
    return st.runClocks.low;
}

int audio_psp_init(void)
{
    int r = sceAudioSRCChReserve(CHUNK_FRAMES, AUDIO_RATE, 2);
    if (r < 0) {
        printf("pulsedash: sceAudioSRCChReserve failed (%08x)\n", (unsigned)r);
        return -1;
    }
    /* a chunk is mixed as the one CHUNKS - 1 before it starts playing (the
     * mixer keeps the ring full: it has the CPU whenever the loop sleeps),
     * so the end of what has been mixed is heard CHUNKS - 1 to CHUNKS
     * chunks later; plus the resampler */
    audio_set_latency((CHUNKS - 0.5f) * CHUNK_FRAMES / AUDIO_RATE + 0.005f);

    s_free = sceKernelCreateSema("pd_audio_free", 0, CHUNKS - 1, CHUNKS, NULL);
    s_mixed = sceKernelCreateSema("pd_audio_mixed", 0, 0, CHUNKS, NULL);
    s_mix_thread = sceKernelCreateThread("pd_mix", mix_thread, MIX_PRIORITY, MIX_STACK, PSP_THREAD_ATTR_USER, NULL);
    SceUID feed = sceKernelCreateThread("pd_feed", feed_thread, FEED_PRIORITY, FEED_STACK, PSP_THREAD_ATTR_USER, NULL);
    if (s_free < 0 || s_mixed < 0 || s_mix_thread < 0 || feed < 0 || sceKernelStartThread(s_mix_thread, 0, NULL) < 0 ||
        sceKernelStartThread(feed, 0, NULL) < 0) {
        printf("pulsedash: audio threads failed\n");
        return -1;
    }
    return 0;
}
