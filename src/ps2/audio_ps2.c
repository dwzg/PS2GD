/*
 * Audio output through audsrv. A dedicated high-priority EE thread renders
 * synth chunks whenever audsrv signals that its ring buffer has room.
 */
#include <kernel.h>
#include <stdio.h>
#include <audsrv.h>
#include <ps2_audio_driver.h>

#include "ps2_platform.h"
#include "../core/audio.h"

#define CHUNK_FRAMES 768
#define CHUNK_BYTES (CHUNK_FRAMES * 4)
#define AUDIO_STACK 0x10000

static int16_t s_buf[CHUNK_FRAMES * 2] __attribute__((aligned(64)));
static u8 s_stack[AUDIO_STACK] __attribute__((aligned(16)));
static int s_sema = -1;

static int fill_cb(void *arg)
{
    (void)arg;
    iSignalSema(s_sema);
    return 0;
}

static void audio_thread(void *arg)
{
    (void)arg;
    for (;;) {
        WaitSema(s_sema);
        audio_mix(s_buf, CHUNK_FRAMES);
        audsrv_play_audio((const char *)s_buf, CHUNK_BYTES);
    }
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

    ee_sema_t sema;
    sema.init_count = 0;
    sema.max_count = 1;
    sema.option = 0;
    s_sema = CreateSema(&sema);
    if (s_sema < 0) return -1;

    /* prime the ring buffer with silence */
    memset(s_buf, 0, sizeof(s_buf));
    audsrv_play_audio((const char *)s_buf, CHUNK_BYTES);
    audsrv_play_audio((const char *)s_buf, CHUNK_BYTES);

    ee_thread_t th;
    memset(&th, 0, sizeof(th));
    th.func = (void *)audio_thread;
    th.stack = s_stack;
    th.stack_size = AUDIO_STACK;
    th.gp_reg = &_gp;
    th.initial_priority = 0x20; /* above the main thread */
    int tid = CreateThread(&th);
    if (tid < 0) return -1;
    StartThread(tid, NULL);

    audsrv_on_fillbuf(CHUNK_BYTES, fill_cb, NULL);
    return 0;
}
