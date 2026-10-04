/* Persistent progress (memory card on PS2, a file on PC). */
#ifndef PD_SAVE_H
#define PD_SAVE_H

#include "common.h"
#include "progress.h"

#define SAVE_MAX_LEVELS PROGRESS_LEVELS
#define SAVE_AUDIO_DELAY_MAX 20 /* +-200 ms */
#define SAVE_MAGIC 0x50445356u /* "PDSV" */
#define SAVE_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    Progress progress; /* bests, coins, attempts (progress.h) */
    uint8_t icon, col1, col2;
    uint8_t music_vol, sfx_vol;
    int8_t audio_delay; /* extra sound delay set in the options, 10 ms units */
    uint8_t reserved[2];
    uint32_t checksum;
} SaveData;

void save_defaults(SaveData *s);
/* Returns 1 if a valid save was loaded, 0 if defaults were used. */
int save_load(SaveData *s);
int save_store(SaveData *s);

#endif
