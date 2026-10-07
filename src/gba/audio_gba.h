/*
 * The Game Boy Advance's sound (audio_gba.c): the core's audio.h, played
 * from the songs and sound effects gba_tool rendered with the core's own
 * synthesizer (src/host/gba_audio.c), as 8-bit samples.
 *
 * main.c calls audio_gba_init() at start-up, then, at every vertical
 * blank, audio_gba_vblank() first thing (it points the sound's DMAs at
 * the frame's sound, and sees in g_frames.ready whether a new picture is
 * shown: the songs and sound effects asked for in its ticks start then)
 * and audio_gba_mix() (it mixes the next frame's), both in the
 * interrupt.
 */
#ifndef PD_AUDIO_GBA_H
#define PD_AUDIO_GBA_H

#include <stdint.h>
#include "gba.h"

/* The two mixes of the sound (the options' OUTPUT): the headphones' is
 * the other platforms' music, in stereo at 21120 samples a second of song
 * time (352 a frame); the speaker's is made for the GBA's speaker, in
 * mono at 13440 (224 a frame): see gba_audio.c. */
enum { GBA_HEADPHONES = 0, GBA_SPEAKER = 1 };

/* A song's samples in one mix, as gba_tool made them. */
typedef struct {
    const int8_t *data; /* 8-bit samples: left and right interleaved, or one channel */
    uint32_t samples;   /* per channel; a whole number of frames */
    uint32_t loop;      /* the length of the part that loops, in 1/256 samples */
    int channels;       /* 2, or 1 (the speaker's; the headphones' if the songs had to be stored in mono) */
} GbaTrack;

/* A song (the ROM's table, gba_songs in gen.c). */
typedef struct {
    const char *name;
    GbaTrack mix[2];    /* [GBA_HEADPHONES], [GBA_SPEAKER] */
    float bpm, length;  /* audio_song_bpm(), audio_song_length() */
} GbaSong;

/* A sound effect in one mix: mono. */
typedef struct {
    const int8_t *data;
    uint32_t samples;   /* a whole number of frames */
    int shift;          /* stored 2^shift times as loud as it plays (up to 8: 3) */
} GbaSfxMix;

/* A sound effect (gba_sfx in gen.c, in the order of the SFX_* ids). */
typedef struct {
    GbaSfxMix mix[2];
} GbaSfx;

/* The song's position: the next sample mixed, counted from its start plus
 * the sound's delay (samples at the mix's rate; on through its loops).
 * (For gba_test, which checks the runs keep time with it.) */
extern volatile int32_t g_audio_emit;

/* Where the output stood at each vertical blank, as the DMAs were pointed
 * at the next buffers (for gba_test: it must be after they read the last
 * ones to the end, before they ask for more; audio_gba.c's top). */
typedef struct {
    uint16_t played;    /* samples played since the output started, from a whole number of frames */
    uint16_t frame;     /* samples a frame (the mix's) */
    uint32_t count;     /* how many were recorded */
} GbaAudioStat;
extern volatile GbaAudioStat g_audio_stat;

void audio_gba_init(void);
IWRAM_CODE void audio_gba_vblank(void);
/* (runs from the cartridge: a long call from the interrupt handler) */
__attribute__((long_call)) void audio_gba_mix(void);
/* (audio.h's audio_set_output picks the mix: a change takes 3 frames, the
 * sound fading out, a frame of silence and the output restarted at the
 * mix's rate, the sound fading back in.) */

#endif
