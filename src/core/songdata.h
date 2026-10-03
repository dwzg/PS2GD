/*
 * Song description format used by songs.c and compiled by audio.c.
 *
 * A song has up to SONG_TRACKS tracks. Tracks 0-2 are drums (kick, snare/clap,
 * hats) and use one character per 16th step:
 *   kick:  x = hit, X = accent
 *   snare: x = snare, X = accent, c = clap, r = soft roll hit
 *   hats:  x = closed, X = accent, o = open
 *   any:   . = nothing
 * Tracks 3-7 are melodic (bass, pad, arp, lead, extra) and use whitespace
 * separated tokens, one per 16th step unless a length is given:
 *   C4  D#3  Eb5      a note (octave 4 contains middle C)
 *   C4+E4+G4          several notes at once
 *   C4:4              note lasting 4 steps
 *   .                 extend the previous note/rest by one step
 *   -   -:8           rest (for 1 or 8 steps)
 * Arrangements list pattern names per track, each instance occupying
 * ceil(len/16) bars:  "intro verse*4 verse+5*2 . .*4"
 *   NAME*N repeat, NAME+T / NAME-T transpose (semitones), "." one empty bar.
 */
#ifndef PD_SONGDATA_H
#define PD_SONGDATA_H

#define SONG_TRACKS 8

enum {
    TR_KICK = 0,
    TR_SNARE,
    TR_HAT,
    TR_BASS,
    TR_PAD,
    TR_ARP,
    TR_LEAD,
    TR_EXTRA
};

/* Instrument ids for melodic tracks */
enum {
    INS_KICK = 0,
    INS_SNARE,
    INS_CLAP,
    INS_HAT,
    INS_BASS_SAW,
    INS_BASS_SQUARE,
    INS_BASS_SUB,
    INS_PAD_SUPER,
    INS_PAD_SOFT,
    INS_PLUCK,
    INS_PLUCK_SAW,
    INS_BELL,
    INS_LEAD_SUPER,
    INS_LEAD_SQUARE,
    INS_LEAD_PULSE,
    INS_CHIP_TRI,
    /* sound effects */
    INS_SFX_BLIP,
    INS_SFX_NOISE,
    INS_SFX_THUMP,
    INS_COUNT
};

typedef struct {
    const char *name;
    const char *data;
} PatternDef;

typedef struct {
    const char *title;
    float bpm;
    int loop_bar; /* bar to jump back to when the arrangement ends */
    const PatternDef *patterns;
    const char *arrange[SONG_TRACKS];
    int inst[SONG_TRACKS]; /* instrument for melodic tracks (3..7) */
    float vol[SONG_TRACKS];
} SongDef;

extern const SongDef *const g_songs[];
extern const PatternDef g_common_patterns[];
extern const int g_song_count;

#endif
