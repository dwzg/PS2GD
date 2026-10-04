/*
 * Songs on the Game Boy Color: the game's own songs (src/core/songs.c),
 * arranged by gbc_tool for the four sound channels:
 *
 *   channel 1 (pulse)  the lead, else the verse melody, else the pad's top note
 *   channel 2 (pulse)  the arpeggio
 *   channel 3 (wave)   the bass
 *   channel 4 (noise)  the drums (kick over snare over hats)
 *
 * Each channel is a byte stream, timed in 1/60 s ticks like the game, so
 * the music stays on the level's beat whatever the frame rate:
 *   0x00          note off
 *   0x01..0x7f    note on (a MIDI note number; a drum on channel 4)
 *   0x80..0xef    wait 1..112 ticks
 *   0xf0 i        instrument i
 *   0xf1 lo hi    wait lo + 256 * hi ticks
 *   0xff          end: continue at the channel's loop point
 */
#ifndef GB_MUSICDATA_H
#define GB_MUSICDATA_H

#include <stdint.h>

#define MS_OFF 0x00
#define MS_WAIT 0x80 /* + ticks - 1 */
#define MS_WAIT_MAX 112
#define MS_INST 0xf0
#define MS_LONGWAIT 0xf1
#define MS_END 0xff

/* Instruments */
enum {
    GI_PLUCK = 0,  /* pulse, quick decay */
    GI_PLUCK_SAW,
    GI_LEAD,       /* pulse, held, vibrato */
    GI_LEAD_SQUARE,
    GI_LEAD_THIN,  /* 12.5% pulse */
    GI_BELL,
    GI_PAD,        /* soft, slow decay */
    GI_CHIP,
    GI_WAVE_SAW,   /* channel 3 waveforms */
    GI_WAVE_SQUARE,
    GI_WAVE_TRI,
    GI_COUNT
};

/* Drums on channel 4 */
enum {
    DR_KICK = 1,
    DR_KICK_SOFT,
    DR_SNARE,
    DR_SNARE_ACC,
    DR_CLAP,
    DR_ROLL,
    DR_HAT,
    DR_HAT_ACC,
    DR_HAT_OPEN,
    DR_COUNT
};

typedef struct {
    uint8_t bank;
    uint8_t bpm;
    const uint8_t *ch[4];
    const uint8_t *loop[4];
} GbSong;

#endif
