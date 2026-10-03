/*
 * Memory card saves via libmc: mc0:/PULSEDASH/ (falls back to mc1).
 * The folder gets an icon.sys + 3D icon so it shows up properly in the
 * PS2 browser.
 *
 * Writing takes a few hundred milliseconds on a real card, so saves are
 * written by a thread below the game loop and the audio thread, in their
 * idle time: the game hands over a copy and carries on (the level-complete
 * save used to freeze the finish). Only that thread uses libmc after the
 * save is read at boot.
 */
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <libmc.h>

#include "ps2_platform.h"
#include "../core/platform.h"

/* IOP-side open flags understood by mcman. */
#define MC_RDONLY 0x0001
#define MC_WRONLY 0x0002
#define MC_CREAT 0x0200

#define SAVE_DIR "/PULSEDASH"
#define SAVE_FILE "/PULSEDASH/SAVE.DAT"
#define ICON_SYS "/PULSEDASH/icon.sys"
#define ICON_FILE "/PULSEDASH/pulse.ico"

#define SAVE_THREAD_PRIORITY 0x60 /* below the audio thread (audio_ps2.c) */
#define SAVE_STACK 0x4000

static int s_mc_ok;
static uint8_t s_io[4096] __attribute__((aligned(64)));
static uint8_t s_icon[48 * 1024] __attribute__((aligned(64)));

/* the newest save waiting to be written */
static uint8_t s_pending[sizeof(s_io)];
static int s_pending_size, s_have_pending;
static int s_lock = -1, s_wake = -1;
static u8 s_stack[SAVE_STACK] __attribute__((aligned(16)));

static void write_save(const void *buf, int size);

static void save_thread(void *arg)
{
    (void)arg;
    static uint8_t buf[sizeof(s_io)];
    for (;;) {
        WaitSema(s_wake);
        WaitSema(s_lock);
        int size = s_pending_size;
        memcpy(buf, s_pending, (size_t)size);
        s_have_pending = 0;
        SignalSema(s_lock);
        write_save(buf, size);
    }
}

static int make_sema(int count, int max)
{
    ee_sema_t sema;
    memset(&sema, 0, sizeof(sema));
    sema.init_count = count;
    sema.max_count = max;
    return CreateSema(&sema);
}

int save_ps2_init(int embedded)
{
    if (embedded && irx_load_memcard() < 0) return -1;
    if (mcInit(embedded ? MC_TYPE_XMC : MC_TYPE_MC) < 0) {
        printf("pulsedash: mcInit failed\n");
        return -1;
    }
    s_lock = make_sema(1, 1);
    s_wake = make_sema(0, 1);
    ee_thread_t th;
    memset(&th, 0, sizeof(th));
    th.func = (void *)save_thread;
    th.stack = s_stack;
    th.stack_size = SAVE_STACK;
    th.gp_reg = &_gp;
    th.initial_priority = SAVE_THREAD_PRIORITY;
    int tid = s_lock >= 0 && s_wake >= 0 ? CreateThread(&th) : -1;
    if (tid < 0 || StartThread(tid, NULL) < 0) {
        printf("pulsedash: save thread failed\n");
        return -1;
    }
    s_mc_ok = 1;
    return 0;
}

static int mc_wait(void)
{
    int ret = -1;
    mcSync(0, NULL, &ret);
    return ret;
}

/* 0/-1 means a usable formatted card is present. */
static int card_ready(int port)
{
    int type = 0, free_kb = 0, format = 0;
    mcGetInfo(port, 0, &type, &free_kb, &format);
    int ret = mc_wait();
    return (ret == 0 || ret == -1) && type == MC_TYPE_PS2 && format == MC_FORMATTED;
}

static int read_file(int port, const char *path, void *buf, int size)
{
    mcOpen(port, 0, path, MC_RDONLY);
    int fd = mc_wait();
    if (fd < 0) return -1;
    mcRead(fd, s_io, size);
    int n = mc_wait();
    mcClose(fd);
    mc_wait();
    if (n > 0) memcpy(buf, s_io, (size_t)n);
    return n;
}

static int write_file(int port, const char *path, const void *buf, int size)
{
    mcOpen(port, 0, path, MC_WRONLY | MC_CREAT);
    int fd = mc_wait();
    if (fd < 0) return -1;
    mcWrite(fd, buf, size);
    int n = mc_wait();
    mcClose(fd);
    mc_wait();
    return n == size ? 0 : -1;
}

int plat_save_read(void *buf, int size)
{
    if (!s_mc_ok || size > (int)sizeof(s_io)) return -1;
    for (int port = 0; port < 2; port++) {
        if (!card_ready(port)) continue;
        int n = read_file(port, SAVE_FILE, buf, size);
        if (n > 0) return n;
    }
    return -1;
}

/* Title in Shift-JIS full-width characters, as the browser expects. */
static void put_sjis_title(mcIcon *ic, const char *s)
{
    uint8_t *out = (uint8_t *)ic->title;
    int i = 0;
    for (; *s && i < 32; s++) {
        unsigned char c = (unsigned char)*s;
        uint16_t code;
        if (c >= 'A' && c <= 'Z') code = (uint16_t)(0x8260 + (c - 'A'));
        else if (c >= 'a' && c <= 'z') code = (uint16_t)(0x8281 + (c - 'a'));
        else if (c >= '0' && c <= '9') code = (uint16_t)(0x824F + (c - '0'));
        else code = 0x8140; /* space */
        out[i * 2] = (uint8_t)(code >> 8);
        out[i * 2 + 1] = (uint8_t)(code & 0xFF);
        i++;
    }
}

static void write_icon_files(int port)
{
    static mcIcon ic __attribute__((aligned(64)));
    static const iconIVECTOR bg[4] = {{40, 60, 160, 0}, {120, 40, 200, 0}, {20, 20, 80, 0}, {60, 20, 120, 0}};
    static const iconFVECTOR light_dir[3] = {{0.5f, 0.5f, 0.5f, 0.0f}, {0.0f, -0.4f, -0.1f, 0.0f}, {-0.5f, -0.5f, 0.5f, 0.0f}};
    static const iconFVECTOR light_col[3] = {{0.3f, 0.3f, 0.3f, 0.0f}, {0.4f, 0.4f, 0.4f, 0.0f}, {0.5f, 0.5f, 0.5f, 0.0f}};
    static const iconFVECTOR ambient = {0.5f, 0.5f, 0.5f, 0.0f};

    memset(&ic, 0, sizeof(ic));
    memcpy(ic.head, "PS2D", 4);
    put_sjis_title(&ic, "PULSE DASH");
    ic.nlOffset = 0;
    ic.trans = 0x60;
    memcpy(ic.bgCol, bg, sizeof(bg));
    memcpy(ic.lightDir, light_dir, sizeof(light_dir));
    memcpy(ic.lightCol, light_col, sizeof(light_col));
    memcpy(ic.lightAmbient, ambient, sizeof(ambient));
    strcpy((char *)ic.view, "pulse.ico");
    strcpy((char *)ic.copy, "pulse.ico");
    strcpy((char *)ic.del, "pulse.ico");
    write_file(port, ICON_SYS, &ic, sizeof(ic));

    int n = icon_ps2_build(s_icon, (int)sizeof(s_icon));
    if (n > 0) write_file(port, ICON_FILE, s_icon, n);
}

/* On the save thread. */
static void write_save(const void *buf, int size)
{
    for (int port = 0; port < 2; port++) {
        if (!card_ready(port)) continue;
        mcMkDir(port, 0, SAVE_DIR);
        int r = mc_wait();
        if (r == 0) write_icon_files(port); /* folder was just created */
        memcpy(s_io, buf, (size_t)size);
        if (write_file(port, SAVE_FILE, s_io, size) == 0) return;
    }
    printf("pulsedash: saving failed\n");
}

/* Queue the save for the save thread; a newer one replaces one still waiting. */
int plat_save_write(const void *buf, int size)
{
    if (!s_mc_ok || size > (int)sizeof(s_pending)) return -1;
    WaitSema(s_lock);
    memcpy(s_pending, buf, (size_t)size);
    s_pending_size = size;
    int wake = !s_have_pending;
    s_have_pending = 1;
    SignalSema(s_lock);
    if (wake) SignalSema(s_wake);
    return 0;
}

const char *plat_name(void) { return "PS2"; }
