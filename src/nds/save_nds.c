/*
 * Saves: the core's SaveData (src/core/save.c), byte for byte the save of
 * the other versions, in a file: PULSEDASH.DAT next to the game's .nds on
 * the flash card's memory card or the DSi's SD card (the FAT file system,
 * through the flash card's DLDI driver or the DSi's SD slot; the loader
 * gives the game its path), or in /data/pulsedash/ when it doesn't say
 * where the game is. Each save is written to PULSEDASH.TMP first and then
 * takes the save's place, so a battery running out mid-write leaves a
 * whole save behind (PULSEDASH.TMP is read if PULSEDASH.DAT is missing).
 *
 * Without a file system (an emulator without an SD card for homebrew) the
 * game plays on without saving.
 *
 * Writing takes a few tens of milliseconds, all at once: the music is
 * written ahead first for as long as the sound's ring holds
 * (audio_nds_hold), so that it plays on through it.
 */
#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "nds_platform.h"
#include "../core/platform.h"

static int s_ok;
static char s_dat[256], s_tmp[256];

/* (for the emulator test) */
volatile uint32_t g_nds_saves_written;

int save_nds_init(int argc, char **argv)
{
    if (!fatInitDefault()) return 0;
    const char *slash = argc > 0 && argv[0] ? strrchr(argv[0], '/') : NULL;
    if (slash && strchr(argv[0], ':')) {
        snprintf(s_dat, sizeof(s_dat), "%.*s/PULSEDASH.DAT", (int)(slash - argv[0]), argv[0]);
        snprintf(s_tmp, sizeof(s_tmp), "%.*s/PULSEDASH.TMP", (int)(slash - argv[0]), argv[0]);
    } else {
        mkdir("/data", 0777);
        mkdir("/data/pulsedash", 0777);
        snprintf(s_dat, sizeof(s_dat), "/data/pulsedash/PULSEDASH.DAT");
        snprintf(s_tmp, sizeof(s_tmp), "/data/pulsedash/PULSEDASH.TMP");
    }
    s_ok = 1;
    return 1;
}

static int read_file(const char *path, void *buf, int size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)size, f);
    fclose(f);
    return n;
}

int plat_save_read(void *buf, int size)
{
    if (!s_ok) return -1;
    int n = read_file(s_dat, buf, size);
    return n >= 0 ? n : read_file(s_tmp, buf, size);
}

int plat_save_write(const void *buf, int size)
{
    if (!s_ok) return -1;
    audio_nds_hold();
    FILE *f = fopen(s_tmp, "wb");
    int n = -1;
    if (f) {
        n = (int)fwrite(buf, 1, (size_t)size, f);
        if (fclose(f)) n = -1;
    }
    if (n != size) return -1;
    remove(s_dat);
    if (rename(s_tmp, s_dat)) return -1;
    g_nds_saves_written++;
    return 0;
}

const char *plat_name(void)
{
    return "DS";
}
