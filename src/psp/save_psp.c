/*
 * Saves: SAVE.DAT in the game's folder, next to EBOOT.PBP.
 *
 * A thread below the game loop and the audio thread writes them, so a slow
 * memory stick never holds up a frame: the game hands over a copy and
 * carries on. Each save is written to SAVE.TMP first and then takes
 * SAVE.DAT's place, so a battery running out mid-write leaves a whole save
 * behind (SAVE.TMP is read if SAVE.DAT is missing).
 */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

#include "psp_platform.h"
#include "../core/platform.h"

#define SAVE_THREAD_PRIORITY 0x30 /* below the game loop and the audio thread */
#define SAVE_STACK 0x4000

static char s_dat[256], s_tmp[256];
static int s_ok;

/* the newest save waiting to be written */
static uint8_t s_pending[1024];
static int s_pending_size;
static volatile int s_have_pending, s_busy;
static SceUID s_lock = -1, s_wake = -1;

static int read_file(const char *path, void *buf, int size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0) return -1;
    int n = sceIoRead(fd, buf, (SceSize)size);
    sceIoClose(fd);
    return n;
}

static void write_save(const void *buf, int size)
{
    SceUID fd = sceIoOpen(s_tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    int n = -1;
    if (fd >= 0) {
        n = sceIoWrite(fd, buf, (SceSize)size);
        sceIoClose(fd);
    }
    if (n != size) {
        printf("pulsedash: saving failed (%s)\n", s_tmp);
        return;
    }
    sceIoRemove(s_dat);
    if (sceIoRename(s_tmp, s_dat) < 0) printf("pulsedash: saving failed (%s)\n", s_dat);
}

static int save_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    static uint8_t buf[sizeof(s_pending)];
    for (;;) {
        sceKernelWaitSema(s_wake, 1, NULL);
        sceKernelWaitSema(s_lock, 1, NULL);
        int size = s_pending_size;
        memcpy(buf, s_pending, (size_t)size);
        s_busy = 1; /* before clearing the other: save_psp_flush() reads both unlocked */
        s_have_pending = 0;
        sceKernelSignalSema(s_lock, 1);
        write_save(buf, size);
        s_busy = 0;
    }
    return 0;
}

int save_psp_init(const char *eboot_path)
{
    /* the folder argv[0] names, e.g. ms0:/PSP/GAME/PulseDash/EBOOT.PBP */
    const char *slash = eboot_path ? strrchr(eboot_path, '/') : NULL;
    int dir = slash ? (int)(slash - eboot_path) + 1 : 0;
    if (dir + 9 > (int)sizeof(s_dat)) dir = 0;
    snprintf(s_dat, sizeof(s_dat), "%.*sSAVE.DAT", dir, eboot_path ? eboot_path : "");
    snprintf(s_tmp, sizeof(s_tmp), "%.*sSAVE.TMP", dir, eboot_path ? eboot_path : "");

    s_lock = sceKernelCreateSema("pd_save_lock", 0, 1, 1, NULL);
    s_wake = sceKernelCreateSema("pd_save_wake", 0, 0, 1, NULL);
    SceUID th = s_lock >= 0 && s_wake >= 0
                    ? sceKernelCreateThread("pd_save", save_thread, SAVE_THREAD_PRIORITY, SAVE_STACK,
                                            PSP_THREAD_ATTR_USER, NULL)
                    : -1;
    if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
        printf("pulsedash: save thread failed\n");
        return -1;
    }
    s_ok = 1;
    return 0;
}

int plat_save_read(void *buf, int size)
{
    if (!s_ok) return -1;
    int n = read_file(s_dat, buf, size);
    if (n <= 0) n = read_file(s_tmp, buf, size); /* cut short while replacing SAVE.DAT */
    return n > 0 ? n : -1;
}

/* Queue the save for the save thread; a newer one replaces one still waiting. */
int plat_save_write(const void *buf, int size)
{
    if (!s_ok || size > (int)sizeof(s_pending)) return -1;
    sceKernelWaitSema(s_lock, 1, NULL);
    memcpy(s_pending, buf, (size_t)size);
    s_pending_size = size;
    int wake = !s_have_pending;
    s_have_pending = 1;
    sceKernelSignalSema(s_lock, 1);
    if (wake) sceKernelSignalSema(s_wake, 1);
    return 0;
}

void save_psp_flush(int timeout_ms)
{
    for (int t = 0; s_ok && (s_have_pending || s_busy) && t < timeout_ms; t += 10) sceKernelDelayThread(10000);
}

const char *plat_name(void) { return "PSP"; }
