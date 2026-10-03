/*
 * Audio output through audsrv.
 *
 * A dedicated EE thread wakes every few milliseconds, tops audsrv's ring
 * buffer up to a small target with freshly synthesized chunks, and sleeps
 * again. Polling avoids depending on IOP->EE callbacks, and the target keeps
 * latency around 50 ms. It runs below the game loop, which sleeps until each
 * vblank, so mixing fills the idle time and never delays a flip.
 */
#include <kernel.h>
#include <delaythread.h>
#include <stdio.h>
#include <audsrv.h>
#include <ps2_audio_driver.h>

#include "ps2_platform.h"
#include "../core/audio.h"

#define CHUNK_FRAMES 512
#define CHUNK_BYTES (CHUNK_FRAMES * 4)
#define TARGET_QUEUED (CHUNK_BYTES * 4)
#define AUDIO_STACK 0x10000
#define AUDIO_THREAD_PRIORITY 0x40 /* below the game loop (main_ps2.c) */

static int16_t s_buf[CHUNK_FRAMES * 2] __attribute__((aligned(64)));
static u8 s_stack[AUDIO_STACK] __attribute__((aligned(16)));
static volatile unsigned s_chunks;
static volatile int s_loops, s_last_avail, s_last_queued;
static volatile unsigned s_mix_cycles;

static void audio_thread(void *arg)
{
    (void)arg;
    int capacity = 0;
    for (;;) {
        /* audsrv_queued() is unreliable, so derive the fill level from the
         * largest free space seen (= the ring buffer size). */
        int avail = audsrv_available();
        if (avail > capacity) capacity = avail;
        int queued = capacity - avail;
        s_loops++;
        s_last_avail = avail;
        s_last_queued = queued;
        while (queued < TARGET_QUEUED && avail >= CHUNK_BYTES) {
#ifdef PD_PERF
            unsigned c0 = ps2_cycles();
            audio_mix(s_buf, CHUNK_FRAMES);
            s_mix_cycles += ps2_cycles() - c0;
#else
            audio_mix(s_buf, CHUNK_FRAMES);
#endif
            audsrv_play_audio((const char *)s_buf, CHUNK_BYTES);
            queued += CHUNK_BYTES;
            avail -= CHUNK_BYTES;
            s_chunks++;
        }
        DelayThread(4000);
    }
}

unsigned audio_ps2_chunks(void)
{
    return s_chunks;
}

unsigned audio_ps2_mix_cycles(void)
{
    return s_mix_cycles;
}

void audio_ps2_debug(int *loops, int *avail, int *queued)
{
    *loops = s_loops;
    *avail = s_last_avail;
    *queued = s_last_queued;
}

int audio_ps2_init(void)
{
    int ret = init_audio_driver();
    if (ret < 0) {
        printf("pulsedash: audio driver failed (%d)\n", ret);
        return -1;
    }
    struct audsrv_fmt_t fmt;
    fmt.freq = AUDIO_RATE;
    fmt.bits = 16;
    fmt.channels = 2;
    if (audsrv_set_format(&fmt) != AUDSRV_ERR_NOERROR) {
        printf("pulsedash: audsrv_set_format failed\n");
        return -1;
    }
    audsrv_set_volume(MAX_VOLUME);
    /* a mixed chunk waits behind ~3.5 queued chunks, then audsrv's own
     * SPU2 transfer buffers */
    audio_set_latency(3.5f * CHUNK_FRAMES / AUDIO_RATE + 0.01f);

    ee_thread_t th;
    memset(&th, 0, sizeof(th));
    th.func = (void *)audio_thread;
    th.stack = s_stack;
    th.stack_size = AUDIO_STACK;
    th.gp_reg = &_gp;
    th.initial_priority = AUDIO_THREAD_PRIORITY;
    int tid = CreateThread(&th);
    if (tid < 0) return -1;
    StartThread(tid, NULL);
    return 0;
}
