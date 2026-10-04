/*
 * Music and sound effects on the Game Boy's four sound channels.
 *
 * Songs are byte streams per channel (musicdata.h), read from their ROM
 * bank once per game tick, so the music stays on the level's beat. A sound
 * effect borrows channel 1 (blips) or 4 (noise) for a moment; the song's
 * events on it go on underneath and it comes back with its next note.
 */
#include "gbc.h"

uint8_t music_beat, music_bar_beat;
static uint8_t s_beat_n; /* the next beat's place in its bar */

typedef struct {
    const uint8_t *p;
    uint16_t wait;
    uint8_t inst;
    uint8_t note; /* sounding note, 0 = none */
    uint8_t age;  /* ticks since the note started */
    uint16_t freq;           /* the pulse channels' note: register value */
    const int8_t *vib;       /* and its vibrato (VIB_BY_DEPTH row), 0 = none */
} Chan;

static Chan s_ch[4];
static const GbSong *s_song;
static uint8_t s_song_id = 0xff;
static uint8_t s_playing, s_paused;
static uint16_t s_beat_acc;
static uint8_t s_sfx_ticks[4]; /* ticks a sound effect still holds the channel */

/* Pulse instruments: duty (NRx1), envelope (NRx2), vibrato. Leads at 8-9
 * of 15: pulse waves are loud, and the drums have to come through (on a
 * GBA's speaker too). */
static const uint8_t INST_DUTY[GI_WAVE_SAW] = {0x40, 0x80, 0x80, 0x80, 0x00, 0x40, 0x80, 0x80};
static const uint8_t INST_ENV[GI_WAVE_SAW] = {0x71, 0x71, 0x90, 0x85, 0x90, 0x83, 0x57, 0x80};
static const uint8_t INST_VIB[GI_WAVE_SAW] = {0, 0, 1, 1, 1, 0, 0, 1};

/* Channel 3 waveforms (32 4-bit samples) and the bass's output levels
 * (NR32). The bass voices have their first harmonics, tapering like the
 * other versions' filtered saw and square (saw 1-8, square 1-9); the kick
 * has a sine of its own (the bass's waveform comes back with its next
 * note). */
#define WAVE_KICK 3
static const uint8_t WAVES[4][16] = {
    {0x8c, 0xff, 0xee, 0xdc, 0xcb, 0xba, 0xa9, 0x98, 0x87, 0x66, 0x55, 0x44, 0x33, 0x21, 0x10, 0x03}, /* saw */
    {0x8c, 0xef, 0xff, 0xff, 0xff, 0xff, 0xff, 0xec, 0x83, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x13}, /* square */
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10}, /* triangle */
    {0x89, 0xac, 0xde, 0xef, 0xff, 0xee, 0xdc, 0xa9, 0x86, 0x53, 0x21, 0x10, 0x00, 0x11, 0x23, 0x56}, /* kick: sine */
};
/* (all at full level: the other versions' bass is their loudest melodic
 * part, about twice their leads) */
static const uint8_t WAVE_LEVEL[3] = {0x20, 0x20, 0x20};

/* Drums on channel 4: envelope (NR42), the noise setting (NR43) for the
 * first three ticks, and how long it sounds (the length counter, 1/256 s).
 * The noise is clocked at 4 kHz or faster: slower, a 15-bit noise
 * crackles. For a kick it is only the click; its tone is on channel 3.
 * Snares and hats at 8-13 of 15, louder than the leads. */
static const uint8_t DRUM_ENV[DR_COUNT] = {0, 0xa1, 0x81, 0xb1, 0xd1, 0xb1, 0x91, 0x81, 0xa1, 0x83};
static const uint8_t DRUM_POLY[DR_COUNT][3] = {
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
static const uint8_t DRUM_LEN[DR_COUNT] = {1, 5, 4, 36, 44, 28, 12, 12, 14, 64};

/* The kick is a falling tone on channel 3 like the other versions' (a sine
 * falling to 46 Hz, 38 for the soft one, in 0.1 s), tick by tick: register
 * values and output levels. It starts an octave above theirs, at 330 Hz:
 * a small speaker plays little below 300 Hz, and the start is the punch.
 * The bass stops for it (the other versions duck the bass after a kick)
 * and starts again after. */
#define KICK_TICKS 8
static const uint16_t KICK_FREQ[2][KICK_TICKS] = {
    {1849, 1718, 1537, 1322, 1109, 933, 809, 729}, /* 330, 199, 128, 90, 70, 59, 53, 50 Hz */
    {1812, 1656, 1438, 1178, 920, 705, 552, 455},  /* 278 .. 41 Hz */
};
static const uint8_t KICK_LEVEL[2][KICK_TICKS] = {
    {0x20, 0x20, 0x20, 0x20, 0x20, 0x40, 0x40, 0x60}, /* 100%, 50%, 25% */
    {0x40, 0x40, 0x40, 0x40, 0x40, 0x60, 0x60, 0x60},
};
static uint8_t s_kick_t; /* the kick's tick + 1, 0 = none */
static uint8_t s_kick_soft;

/* Vibrato, about a quarter tone: VIB[age & 7] * depth / 2 for each depth
 * ((2048 - frequency register) >> 7) a note can have */
static const int8_t VIB_BY_DEPTH[16][8] = {
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

static uint8_t s_wave = 0xff; /* the waveform in wave RAM */

/* (only when it changes, and right before the channel starts a note: the
 * channel is off while its waveform is written) */
static void wave_load(uint8_t w)
{
    uint8_t i;
    if (w == s_wave) return;
    s_wave = w;
    NR30_REG = 0;
    for (i = 0; i < 16; i++) ((volatile uint8_t *)0xff30)[i] = WAVES[w][i];
    NR30_REG = 0x80;
}

/* Silence a channel. The pulse and noise channels restart at volume 0
 * rather than switching their DAC off (NRx2 = 0): on the hardware that
 * pops, every time a held note ends. */
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
    case 2: NR32_REG = 0; break;
    default:
        NR42_REG = 0x08;
        NR44_REG = 0x80;
        break;
    }
}

static void pulse_on(uint8_t c, uint8_t inst, uint16_t f)
{
    if (c == 0) {
        NR10_REG = 0;
        NR11_REG = INST_DUTY[inst];
        NR12_REG = INST_ENV[inst];
        NR13_REG = (uint8_t)f;
        NR14_REG = 0x80 | (uint8_t)(f >> 8);
    } else {
        NR21_REG = INST_DUTY[inst];
        NR22_REG = INST_ENV[inst];
        NR23_REG = (uint8_t)f;
        NR24_REG = 0x80 | (uint8_t)(f >> 8);
    }
}

static void note_on(uint8_t c)
{
    Chan *ch = &s_ch[c];
    if (c < 2) {
        /* (kept for the vibrato, also while a sound effect has the channel) */
        ch->freq = note_freq(ch->note);
        ch->vib = INST_VIB[ch->inst] ? VIB_BY_DEPTH[(uint8_t)((2048 - ch->freq) >> 7) & 15] : 0;
    }
    if (s_sfx_ticks[c] || (c == 2 && s_kick_t)) return;
    if (c < 2) {
        pulse_on(c, ch->inst, ch->freq);
    } else if (c == 2) {
        /* the wave channel plays an octave below the pulse channels' register value */
        uint16_t f = note_freq((uint8_t)(ch->note + 12));
        uint8_t w = ch->inst >= GI_WAVE_SAW ? ch->inst - GI_WAVE_SAW : 0;
        wave_load(w);
        NR30_REG = 0x80;
        NR32_REG = WAVE_LEVEL[w];
        NR33_REG = (uint8_t)f;
        NR34_REG = 0x80 | (uint8_t)(f >> 8);
    } else {
        uint8_t d = ch->note < DR_COUNT ? ch->note : DR_HAT;
        if (d == DR_KICK || d == DR_KICK_SOFT) {
            s_kick_t = 1;
            s_kick_soft = d == DR_KICK_SOFT;
        }
        NR41_REG = (uint8_t)(64 - DRUM_LEN[d]);
        NR42_REG = DRUM_ENV[d];
        NR43_REG = DRUM_POLY[d][0];
        NR44_REG = 0xc0; /* with the length counter */
    }
}

/* Per tick while a note sounds: drum pitch, lead vibrato. */
static void note_update(uint8_t c)
{
    Chan *ch = &s_ch[c];
    uint8_t age = ch->age;
    if (!ch->note || s_sfx_ticks[c]) return;
    if (age < 255) ch->age++;
    if (c == 3) {
        /* (the tick it started on played setting 0) */
        if (age && age < 3) NR43_REG = DRUM_POLY[ch->note < DR_COUNT ? ch->note : DR_HAT][age];
    } else if (c < 2 && ch->vib && ch->age > 12) {
        uint16_t f = ch->freq + ch->vib[ch->age & 7];
        if (c == 0) {
            NR13_REG = (uint8_t)f;
            NR14_REG = (uint8_t)(f >> 8);
        } else {
            NR23_REG = (uint8_t)f;
            NR24_REG = (uint8_t)(f >> 8);
        }
    }
}

/* Read a channel's events up to its next wait (the song's bank is mapped). */
/* A tick of the kick on channel 3; after it, the bass's note again. */
static void kick_tick(void)
{
    uint8_t k = s_kick_t - 1;
    uint16_t f;
    if (k >= KICK_TICKS) {
        s_kick_t = 0;
        if (s_ch[2].note) note_on(2);
        else NR32_REG = 0;
        return;
    }
    f = KICK_FREQ[s_kick_soft][k];
    if (!k) {
        wave_load(WAVE_KICK);
        NR30_REG = 0x80;
    }
    NR32_REG = KICK_LEVEL[s_kick_soft][k];
    NR33_REG = (uint8_t)f;
    NR34_REG = (uint8_t)((k ? 0 : 0x80) | (f >> 8));
    s_kick_t++;
}

static void chan_step(uint8_t c)
{
    Chan *ch = &s_ch[c];
    for (;;) {
        uint8_t b = *ch->p++;
        if (b == MS_END) {
            ch->p = s_song->loop[c];
            continue;
        }
        if (b >= MS_WAIT && b < MS_INST) {
            ch->wait = (uint16_t)(b - MS_WAIT + 1);
            return;
        }
        if (b == MS_LONGWAIT) {
            ch->wait = ch->p[0] | ((uint16_t)ch->p[1] << 8);
            ch->p += 2;
            return;
        }
        if (b == MS_INST) {
            ch->inst = *ch->p++;
            continue;
        }
        if (b == MS_OFF) {
            ch->note = 0;
            if (!s_sfx_ticks[c] && !(c == 2 && s_kick_t)) ch_off(c);
            continue;
        }
        ch->note = b;
        ch->age = 0;
        note_on(c);
    }
}

void music_init(void)
{
    NR52_REG = 0x80; /* sound on */
    NR50_REG = 0x77;
    NR51_REG = 0xff;
    s_playing = 0;
}

void music_play(uint8_t song)
{
    uint8_t c;
    music_stop();
    s_song = &gbc_songs[song];
    s_song_id = song;
    if (!s_song->bank) return;
    for (c = 0; c < 4; c++) {
        s_ch[c].p = s_song->ch[c];
        s_ch[c].wait = 0;
        s_ch[c].note = 0;
        s_ch[c].inst = 0;
    }
    s_beat_acc = 0;
    s_beat_n = 0;
    s_paused = 0;
    s_playing = 1;
    NR50_REG = 0x77;
}

void music_ensure(uint8_t song)
{
    if (!s_playing || s_song_id != song) music_play(song);
}

void music_stop(void)
{
    uint8_t c;
    s_playing = 0;
    s_kick_t = 0;
    for (c = 0; c < 4; c++)
        if (!s_sfx_ticks[c]) ch_off(c);
}

void music_pause(uint8_t paused)
{
    uint8_t c;
    s_paused = paused;
    s_kick_t = 0;
    if (paused)
        for (c = 0; c < 4; c++)
            if (!s_sfx_ticks[c]) ch_off(c);
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
        s_sfx_ticks[3] = 40;
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
    s_sfx_ticks[0] = 1;
}

static void sfx_tick(void)
{
    if (s_sfx_ticks[3] && !--s_sfx_ticks[3]) ch_off(3);
    if (!s_sfx) return;
    if (s_sfx_left == 0) {
        if (!s_sfx->ticks) {
            s_sfx = 0;
            s_sfx_ticks[0] = 0;
            ch_off(0);
            return;
        }
        pulse_on(0, GI_PLUCK_SAW, note_freq(s_sfx->note));
        NR12_REG = 0xc2;
        NR14_REG = 0x80 | (uint8_t)(note_freq(s_sfx->note) >> 8);
        s_sfx_left = s_sfx->ticks;
        s_sfx++;
    }
    s_sfx_left--;
    s_sfx_ticks[0] = 1;
}

void music_tick(void)
{
    uint8_t c;
    music_beat = 0;
    sfx_tick();
    if (!s_playing || s_paused) return;
    /* a beat every 3600 / bpm ticks */
    if (s_beat_acc < s_song->bpm) {
        music_beat = 1;
        music_bar_beat = s_beat_n;
        s_beat_n = (uint8_t)((s_beat_n + 1) & 3);
    }
    s_beat_acc += s_song->bpm;
    if (s_beat_acc >= 3600) s_beat_acc -= 3600;
    {
        uint8_t bank = _current_bank;
        SWITCH_ROM_MBC5(s_song->bank);
        for (c = 0; c < 4; c++) {
            Chan *ch = &s_ch[c];
            if (ch->wait && --ch->wait) {
                note_update(c);
                continue;
            }
            chan_step(c);
            note_update(c);
        }
        if (s_kick_t) kick_tick();
        SWITCH_ROM_MBC5(bank);
    }
}
