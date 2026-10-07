/*
 * Music and sound effects on the Game Boy's four sound channels.
 *
 * Songs are byte streams per channel (musicdata.h), read from their ROM
 * bank once per game tick, so the music stays on the level's beat. A sound
 * effect borrows channel 1 (blips) or 4 (noise) for a moment; the song's
 * events on it go on underneath, and a note held on channel 1 sounds again
 * when the effect ends.
 *
 * The tick itself (music_tick: reading the streams, starting notes, the
 * vibrato, the kick) is assembly, in music_asm.s: it runs first in every
 * frame and can't wait, and SDCC's code for it took 10 scanlines a tick
 * on average and 32 when every channel started a note (3 and 8 now). This
 * file sets songs up, stops and pauses them, plays the sound effects, and
 * has the tables the tick reads.
 *
 * Panning: channel 2 by its instrument (m_inst_pan), the rest in the
 * middle. Channel 3's DAC stays on: its waveform is only written while the
 * channel is stopped (music_asm.s), as switching the DAC off and on clicks
 * on the hardware.
 */
#include "gbc.h"

uint8_t music_beat, music_bar_beat;

/* A channel's state, as music_asm.s reads it (16 bytes, offsets fixed). */
typedef struct {
    uint16_t wait;        /* 0: ticks to the channel's next events */
    const uint8_t *p;     /* 2: its next events */
    uint8_t note;         /* 4: sounding note, 0 = none */
    uint8_t age;          /* 5: ticks since the note started */
    uint8_t inst;         /* 6 */
    uint16_t freq;        /* 7: the note's frequency register value */
    const int8_t *vib;    /* 9: its vibrato (a VIB_BY_DEPTH row), 0 = none */
    const uint8_t *loop;  /* 11: where the stream goes on after its end */
    uint8_t sfx;          /* 13: ticks a sound effect still holds the channel */
    uint8_t unused[2];
} Chan;
typedef char chan_is_16_bytes[sizeof(Chan) == 16 ? 1 : -1];

Chan m_ch[4];
uint8_t m_playing, m_paused;
uint8_t m_bank, m_bpm, m_beat_n;
uint16_t m_beat_acc;
uint8_t m_kick_t;    /* the kick's tick + 1, 0 = none */
uint8_t m_kick_soft;
uint8_t m_kick_next; /* channel 4's next note is a kick */
uint8_t m_wave = 0xff; /* the waveform in wave RAM */
uint8_t m_wave_dac;  /* times a waveform went in with the DAC off (see wave_load) */
uint8_t m_nr51;      /* the channels' panning */
static uint8_t s_song_id = 0xff;

/* in music_asm.s: channel 3's note again (the bass, after a pause), and
 * channel 4's next note read for m_kick_next (the song's bank mapped) */
void m_bass_on(void);
void m_kick_peek(void);

/* Pulse instruments: duty (NRx1), envelope (NRx2), vibrato. Leads at 8-9
 * of 15: pulse waves are loud, and the drums have to come through (on a
 * GBA's speaker too). */
const uint8_t m_inst_duty[GI_WAVE_SAW] = {0x40, 0x80, 0x80, 0x80, 0x00, 0x40, 0x80, 0x80};
const uint8_t m_inst_env[GI_WAVE_SAW] = {0x71, 0x71, 0x90, 0x85, 0x90, 0x83, 0x57, 0x80};
const uint8_t m_inst_vib[GI_WAVE_SAW] = {0, 0, 1, 1, 1, 0, 0, 1};
/* Channel 2's panning (NR51's bits for it) by instrument, after the other
 * versions' mix: the pluck to the left, the saw pluck and the bell to the
 * right, the rest in the middle. Channel 1 (the melody), the bass and the
 * drums are always in the middle. A channel on one side only is half as
 * loud in a Game Boy's speaker (it plays both sides): those are a little
 * louder on channel 2 (envelopes). */
const uint8_t m_inst_pan[GI_WAVE_SAW] = {0x20, 0x02, 0x22, 0x22, 0x22, 0x02, 0x22, 0x22};
const uint8_t m_inst_env2[GI_WAVE_SAW] = {0xa1, 0xa1, 0x90, 0x85, 0x90, 0xb3, 0x57, 0x80};

/* Channel 3 waveforms (32 4-bit samples). The bass voices have their first
 * harmonics, tapering like the other versions' filtered saw and square
 * (saw 1-8, square 1-9); the kick has a sine of its own (the bass's
 * waveform comes back with its next note). The bass plays at full level
 * (NR32 0x20): the other versions' bass is their loudest melodic part,
 * about twice their leads. */
const uint8_t m_waves[4][16] = {
    {0x8c, 0xff, 0xee, 0xdc, 0xcb, 0xba, 0xa9, 0x98, 0x87, 0x66, 0x55, 0x44, 0x33, 0x21, 0x10, 0x03}, /* saw */
    {0x8c, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xec, 0x83, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13}, /* square */
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10}, /* triangle */
    {0x89, 0xac, 0xde, 0xef, 0xff, 0xee, 0xdc, 0xa9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56}, /* kick: sine */
};

/* Drums on channel 4: the length (NR41: 64 - its length in 1/256 s),
 * envelope (NR42), and the noise setting (NR43) for the first three ticks.
 * The noise is clocked at 4 kHz or faster: slower, a 15-bit noise
 * crackles. For a kick it is only the click; its tone is on channel 3.
 * Snares and hats at 8-13 of 15, louder than the leads. */
const uint8_t m_drum_nr41[DR_COUNT] = {64 - 1, 64 - 5, 64 - 4, 64 - 36, 64 - 44, 64 - 28, 64 - 12, 64 - 12, 64 - 14, 64 - 64};
const uint8_t m_drum_env[DR_COUNT] = {0, 0xa1, 0x81, 0xb1, 0xd1, 0xb1, 0x91, 0x81, 0xa1, 0x83};
const uint8_t m_drum_poly[DR_COUNT][3] = {
    {0, 0, 0},
    {0x34, 0x44, 0x44}, /* kick: the click (8, then 4 kHz) */
    {0x34, 0x44, 0x44}, /* soft kick */
    {0x44, 0x33, 0x34}, /* snare: darker for a tick (its body), then 11, 9 kHz */
    {0x43, 0x32, 0x33}, /* accented snare */
    {0x22, 0x23, 0x33}, /* clap */
    {0x34, 0x34, 0x34}, /* roll */
    {0x10, 0x10, 0x10}, /* hat */
    {0x00, 0x00, 0x00}, /* accented hat */
    {0x11, 0x11, 0x11}, /* open hat */
};

/* The kick is a falling tone on channel 3 like the other versions' (a sine
 * falling to 46 Hz, 38 for the soft one, in 0.1 s), tick by tick: register
 * values and output levels. It starts an octave above theirs, at 330 Hz:
 * a small speaker plays little below 300 Hz, and the start is the punch.
 * The bass stops for it (the other versions duck the bass after a kick)
 * and starts again after. */
const uint16_t m_kick_freq[2][8] = {
    {1849, 1718, 1537, 1322, 1109, 933, 809, 729}, /* 330, 199, 128, 90, 70, 59, 53, 50 Hz */
    {1812, 1656, 1438, 1178, 920, 705, 552, 455},  /* 278 .. 41 Hz */
};
const uint8_t m_kick_level[2][8] = {
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x40, 0x40, 0x60}, /* 100%, 50%, 25% */
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x60, 0x60, 0x60},
};

/* Vibrato, about a quarter tone: VIB[age & 7] * depth / 2 for each depth
 * ((2048 - frequency register) >> 7) a note can have */
const int8_t m_vib_by_depth[16][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},       {0, 1, 1, 1, 0, -1, -1, -1},     {0, 2, 3, 2, 0, -2, -3, -2},
    {0, 3, 4, 3, 0, -3, -4, -3},    {0, 4, 6, 4, 0, -4, -6, -4},     {0, 5, 7, 5, 0, -5, -7, -5},
    {0, 6, 9, 6, 0, -6, -9, -6},    {0, 7, 10, 7, 0, -7, -10, -7},   {0, 8, 12, 8, 0, -8, -12, -8},
    {0, 9, 13, 9, 0, -9, -13, -9},  {0, 10, 15, 10, 0, -10, -15, -10}, {0, 11, 16, 11, 0, -11, -16, -11},
    {0, 12, 18, 12, 0, -12, -18, -12}, {0, 13, 19, 13, 0, -13, -19, -13}, {0, 14, 21, 14, 0, -14, -21, -14},
    {0, 15, 22, 15, 0, -15, -22, -15},
};

static uint16_t note_freq(uint8_t note)
{
    if (note < 36) note = 36;
    if (note > 119) note = 119;
    return gbc_freq[note - 36];
}

/* Silence a channel. The pulse and noise channels restart at volume 0
 * rather than switching their DAC off (NRx2 = 0): on the hardware that
 * pops, every time a held note ends. Channel 3 is muted and stopped by its
 * length counter (within 4 ms), its DAC left on: a stopped channel's wave
 * RAM can be written (music_asm.s). */
static void ch_off(uint8_t c)
{
    switch (c) {
    case 0:
        NR12_REG = 0x08;
        NR14_REG = 0x80;
        break;
    case 1:
        NR22_REG = 0x08;
        NR24_REG = 0x80;
        break;
    case 2:
        NR32_REG = 0;
        NR31_REG = 0xff;
        NR34_REG = 0x40 | (uint8_t)(m_ch[2].freq >> 8);
        break;
    default:
        NR42_REG = 0x08;
        NR44_REG = 0x80;
        break;
    }
}

static void pulse_on(uint8_t c, uint8_t duty, uint8_t env, uint16_t f)
{
    if (c == 0) {
        NR10_REG = 0;
        NR11_REG = duty;
        NR12_REG = env;
        NR13_REG = (uint8_t)f;
        NR14_REG = 0x80 | (uint8_t)(f >> 8);
    } else {
        NR21_REG = duty;
        NR22_REG = env;
        NR23_REG = (uint8_t)f;
        NR24_REG = 0x80 | (uint8_t)(f >> 8);
    }
}

/* A song's note held on a channel that was silent for a moment (a sound
 * effect had it, or the music was paused) sounds again, as loud as its
 * envelope has got to by now: the lead comes back after a coin. Returns
 * whether it did. */
static uint8_t note_back(uint8_t c)
{
    Chan *ch = &m_ch[c];
    uint8_t env, vol, per;
    if (!m_playing || m_paused || !ch->note || ch->sfx) return 0;
    if (c == 2) {
        if (m_kick_t) return 0;
        m_bass_on();
        return 1;
    }
    env = (c ? m_inst_env2 : m_inst_env)[ch->inst];
    vol = env >> 4;
    per = env & 7;
    if (per) {
        /* an envelope step every per/64 s, a tick is 1/60 s */
        uint8_t steps = (uint8_t)(((uint16_t)ch->age + (ch->age >> 4)) / per);
        if (steps >= vol) return 0;
        vol -= steps;
    }
    pulse_on(c, m_inst_duty[ch->inst], (uint8_t)((vol << 4) | (env & 15)), ch->freq);
    return 1;
}

void music_init(void)
{
    NR52_REG = 0x80; /* sound on */
    NR50_REG = 0x77;
    m_nr51 = 0xff;
    NR51_REG = 0xff;
    NR30_REG = 0x80; /* channel 3's DAC: on for good */
    m_playing = 0;
}

void music_play(uint8_t song)
{
    const GbSong *s = &gbc_songs[song];
    uint8_t c, bank;
    music_stop();
    s_song_id = song;
    if (!s->bank) return;
    for (c = 0; c < 4; c++) {
        Chan *ch = &m_ch[c];
        ch->p = s->ch[c];
        ch->loop = s->loop[c];
        ch->wait = 0;
        ch->note = 0;
        ch->inst = 0;
        ch->vib = 0;
    }
    m_bank = s->bank;
    m_bpm = s->bpm;
    m_beat_acc = 0;
    m_beat_n = 0;
    m_paused = 0;
    m_playing = 1;
    m_nr51 = 0xff;
    NR51_REG = 0xff;
    NR50_REG = 0x77;
    /* (the bass doesn't start on the first tick if a kick does) */
    bank = _current_bank;
    SWITCH_ROM_MBC5(m_bank);
    m_kick_peek();
    SWITCH_ROM_MBC5(bank);
}

void music_ensure(uint8_t song)
{
    if (!m_playing || s_song_id != song) music_play(song);
}

void music_stop(void)
{
    uint8_t c;
    m_playing = 0;
    m_kick_t = 0;
    for (c = 0; c < 4; c++) {
        m_ch[c].note = 0;
        if (!m_ch[c].sfx) ch_off(c);
    }
}

void music_pause(uint8_t paused)
{
    uint8_t c;
    m_paused = paused;
    m_kick_t = 0;
    for (c = 0; c < 4; c++) {
        if (m_ch[c].sfx) continue;
        /* going on: the notes held through the pause sound again */
        if (paused || c == 3 || !note_back(c)) ch_off(c);
    }
}

/* --- sound effects --- */

typedef struct {
    uint8_t note, ticks;
} SfxNote;

static const SfxNote SFX_COIN_N[] = {{88, 4}, {95, 8}, {0, 0}};
static const SfxNote SFX_CHECK_N[] = {{84, 3}, {91, 5}, {0, 0}};
static const SfxNote SFX_MOVE_N[] = {{79, 3}, {0, 0}};
static const SfxNote SFX_SELECT_N[] = {{84, 3}, {91, 6}, {0, 0}};
static const SfxNote SFX_BACK_N[] = {{79, 3}, {72, 5}, {0, 0}};
static const SfxNote SFX_COMPLETE_N[] = {{72, 6}, {76, 6}, {79, 6}, {84, 6}, {88, 6}, {91, 6}, {96, 18}, {0, 0}};
static const SfxNote SFX_ORB_N[] = {{91, 2}, {0, 0}};

static const SfxNote *s_sfx;
static uint8_t s_sfx_left;

void sfx_play(uint8_t id)
{
    if (id == SFX_DEATH) {
        m_ch[3].sfx = 40;
        NR41_REG = 0;
        NR42_REG = 0xf3;
        NR43_REG = 0x55;
        NR44_REG = 0x80;
        return;
    }
    switch (id) {
    case SFX_COIN: s_sfx = SFX_COIN_N; break;
    case SFX_CHECKPOINT: s_sfx = SFX_CHECK_N; break;
    case SFX_MOVE: s_sfx = SFX_MOVE_N; break;
    case SFX_SELECT: s_sfx = SFX_SELECT_N; break;
    case SFX_BACK: s_sfx = SFX_BACK_N; break;
    case SFX_COMPLETE: s_sfx = SFX_COMPLETE_N; break;
    default: s_sfx = SFX_ORB_N; break;
    }
    s_sfx_left = 0;
    m_ch[0].sfx = 1;
}

/* (called by music_tick first) */
void sfx_tick(void)
{
    if (m_ch[3].sfx && !--m_ch[3].sfx) ch_off(3);
    if (!s_sfx) return;
    if (s_sfx_left == 0) {
        if (!s_sfx->ticks) {
            s_sfx = 0;
            m_ch[0].sfx = 0;
            if (!note_back(0)) ch_off(0);
            return;
        }
        pulse_on(0, m_inst_duty[GI_PLUCK_SAW], 0xc2, note_freq(s_sfx->note));
        s_sfx_left = s_sfx->ticks;
        s_sfx++;
    }
    s_sfx_left--;
    m_ch[0].sfx = 1;
}
