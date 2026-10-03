/*
 * Software synthesizer + sequencer. All music and sound effects are
 * generated at runtime from the note data in songs.c, so the game ships
 * without any sample files.
 *
 * Threading: audio_mix() runs on the audio thread/callback. The other calls
 * come from the game thread and are passed over through a lock-free queue.
 */
#ifndef PD_AUDIO_H
#define PD_AUDIO_H

#include "common.h"

#define AUDIO_RATE 48000

enum {
    SFX_DEATH = 0,
    SFX_COIN,
    SFX_CHECKPOINT,
    SFX_MENU_MOVE,
    SFX_MENU_SELECT,
    SFX_MENU_BACK,
    SFX_COMPLETE,
    SFX_START,
    SFX_COUNT
};

/* Song ids: 0 = menu loop, 1 = practice loop, 2.. = level tracks. */
enum { SONG_MENU = 0, SONG_PRACTICE = 1, SONG_FIRST_LEVEL = 2 };

void audio_init(void);

/* Render interleaved stereo int16 frames. Called from the audio thread. */
void audio_mix(int16_t *out, int frames);

/* Output latency of the frontend (time from audio_mix() to the speaker).
 * Songs start this far in, so what is heard lines up with game time. */
void audio_set_latency(float sec);

/* Start a song so that `start_sec` into it is heard right now. */
void audio_play_song(int song, float start_sec);
void audio_stop_song(void);
void audio_sfx(int id);
void audio_set_volume(int music_0_10, int sfx_0_10);
/* Freeze the music (sound effects keep playing). */
void audio_pause(int paused);

/* Position of the current song that is being heard, in seconds and beats. */
float audio_song_time(void);
float audio_song_beat(void);
int audio_current_song(void);

int audio_song_count(void);
const char *audio_song_name(int song);
float audio_song_bpm(int song);
/* Length of a song's arrangement in seconds (before looping). */
float audio_song_length(int song);

/* Tool helpers: length of a pattern in 16th steps; arrangement length in bars. */
int audio_pattern_steps(const char *data, int drum);
int audio_song_bars(int song);

#endif
