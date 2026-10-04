/*
 * Music and sound effects on the Game Boy's four sound channels.
 *
 * Songs are byte streams per channel (musicdata.h), read from their ROM
 * bank once per game tick, so the music stays on the level's beat. A sound
 * effect borrows channel 1 (blips) or 4 (noise) for a moment; the song's
 * events on it go on underneath and it comes back with its next note.
 */
#include "gbc.h"

uint8_t music_beat;

typedef struct {
    const uint8_t *p;
    uint16_t wait;
    uint8_t inst;
    uint8_t note; /* sounding note, 0 = none */
    uint8_t age;  /* ticks since the note started */
} Chan;

static Chan s_ch[4];
static const GbSong *s_song;
static uint8_t s_song_id = 0xff;
static uint8_t s_playing, s_paused;
static uint16_t s_beat_acc;
static uint8_t s_sfx_ticks[4]; /* ticks a sound effect still holds the channel */

/* Pulse instruments: duty (NRx1), envelope (NRx2), vibrato */
static const uint8_t INST_DUTY[GI_WAVE_SAW] = {0x40, 0x80, 0x80, 0x80, 0x00, 0x40, 0x80, 0x80};
static const uint8_t INST_ENV[GI_WAVE_SAW] = {0x81, 0x81, 0xb0, 0xa5, 0xa0, 0x93, 0x57, 0x90};
static const uint8_t INST_VIB[GI_WAVE_SAW] = {0, 0, 1, 1, 1, 0, 0, 1};

/* Channel 3 waveforms (32 4-bit samples) and output levels (NR32) */
static const uint8_t WAVES[3][16] = {
    {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}, /* saw */
    {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, /* square */
    {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10}, /* triangle */
};
static const uint8_t WAVE_LEVEL[3] = {0x40, 0x40, 0x20};

/* Drums on channel 4: envelope, then the noise setting (NR43) for the
 * first three ticks (a kick falls in pitch). */
static const uint8_t DRUM_ENV[DR_COUNT] = {0, 0xf1, 0xa1, 0xb2, 0xd2, 0xb3, 0x72, 0x51, 0x71, 0x63};
static const uint8_t DRUM_POLY[DR_COUNT][3] = {
    {0, 0, 0},
    {0x54, 0x65, 0x76}, /* kick */
    {0x55, 0x66, 0x77}, /* soft kick */
    {0x34, 0x45, 0x46}, /* snare */
    {0x24, 0x35, 0x46}, /* accented snare */
    {0x33, 0x33, 0x34}, /* clap */
    {0x45, 0x45, 0x45}, /* roll */
    {0x10, 0x10, 0x10}, /* hat */
    {0x00, 0x00, 0x00}, /* accented hat */
    {0x11, 0x11, 0x11}, /* open hat */
};

static uint16_t note_freq(uint8_t note)
{
    if (note < 36) note = 36;
    if (note > 119) note = 119;
    return gbc_freq[note - 36];
}

static void wave_load(uint8_t w)
{
    uint8_t i;
    NR30_REG = 0;
    for (i = 0; i < 16; i++) ((volatile uint8_t *)0xff30)[i] = WAVES[w][i];
    NR30_REG = 0x80;
}

static void ch_off(uint8_t c)
{
    switch (c) {
    case 0: NR12_REG = 0; break;
    case 1: NR22_REG = 0; break;
    case 2: NR32_REG = 0; break;
    default: NR42_REG = 0; break;
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
    if (s_sfx_ticks[c]) return;
    if (c < 2) {
        pulse_on(c, ch->inst, note_freq(ch->note));
    } else if (c == 2) {
        /* the wave channel plays an octave below the pulse channels' register value */
        uint16_t f = note_freq((uint8_t)(ch->note + 12));
        uint8_t w = ch->inst >= GI_WAVE_SAW ? ch->inst - GI_WAVE_SAW : 0;
        NR30_REG = 0x80;
        NR32_REG = WAVE_LEVEL[w];
        NR33_REG = (uint8_t)f;
        NR34_REG = 0x80 | (uint8_t)(f >> 8);
    } else {
        uint8_t d = ch->note < DR_COUNT ? ch->note : DR_HAT;
        NR41_REG = 0;
        NR42_REG = DRUM_ENV[d];
        NR43_REG = DRUM_POLY[d][0];
        NR44_REG = 0x80;
    }
}

/* Per tick while a note sounds: drum pitch, lead vibrato. */
static void note_update(uint8_t c)
{
    Chan *ch = &s_ch[c];
    if (!ch->note || s_sfx_ticks[c]) return;
    if (ch->age < 255) ch->age++;
    if (c == 3) {
        if (ch->age < 3) NR43_REG = DRUM_POLY[ch->note < DR_COUNT ? ch->note : DR_HAT][ch->age];
    } else if (c < 2 && INST_VIB[ch->inst] && ch->age > 12) {
        static const int8_t VIB[8] = {0, 2, 3, 2, 0, -2, -3, -2};
        uint16_t f = note_freq(ch->note);
        uint8_t depth = (uint8_t)((2048 - f) >> 7); /* about a quarter tone */
        f += (int16_t)(VIB[ch->age & 7] * depth) / 2;
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
            if (c == 2 && ch->inst >= GI_WAVE_SAW) wave_load(ch->inst - GI_WAVE_SAW);
            continue;
        }
        if (b == MS_OFF) {
            ch->note = 0;
            if (!s_sfx_ticks[c]) ch_off(c);
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
    for (c = 0; c < 4; c++)
        if (!s_sfx_ticks[c]) ch_off(c);
}

void music_pause(uint8_t paused)
{
    uint8_t c;
    s_paused = paused;
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
    if (s_sfx_ticks[3] && !--s_sfx_ticks[3]) NR42_REG = 0;
    if (!s_sfx) return;
    if (s_sfx_left == 0) {
        if (!s_sfx->ticks) {
            s_sfx = 0;
            s_sfx_ticks[0] = 0;
            NR12_REG = 0;
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
    if (s_beat_acc < s_song->bpm) music_beat = 1;
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
        SWITCH_ROM_MBC5(bank);
    }
}
