/*
 * The Game Boy Advance's sound: the core's audio.h, played from the songs
 * and sound effects the core's synthesizer rendered at build time (gba_tool,
 * src/host/gba_audio.c), so the GBA's music is the PC's: in stereo, the
 * left channel on Direct Sound A and the right on Direct Sound B. Or, with
 * the options' OUTPUT on SPEAKER, the speaker's mix of them (mono, on both).
 *
 * The output. Timer 0 overflows every PERIOD cycles, exactly FRAME times a
 * frame (the headphones' mix: 798 cycles, 352 samples, 21024 Hz; the
 * speaker's: 1254, 224, 13379 Hz), and each overflow plays a sample from
 * each FIFO; DMA 1 and DMA 2 keep FIFO A and FIFO B filled, 16 bytes at a
 * time, from buffers of a frame's samples. A frame plays 1/60 s of song
 * time (the display runs at 59.73 Hz, so the music is 0.45% slower and
 * lower than on the other platforms, and exactly in step with the game,
 * which ticks once a frame). Timer 0 is started once, at the start of a
 * scanline (Out.line), and runs on: a frame being a whole number of
 * samples, it keeps its place against the picture, and so do the DMAs'
 * requests (one every 16 samples, when a sample played leaves a FIFO 4 of
 * its 7 words free: FIFO_REQ). Each vertical blank comes between two of
 * them, where the DMAs have read their buffers exactly to the end:
 * audio_gba_vblank() points them at the buffers mixed at the vertical
 * blank before, and the FIFOs, never emptied, play the old buffers' last
 * 23 samples and go on into the new ones. The two requests are 6.2k cycles
 * before and 6.5k after the vertical blank's start (the speaker's mix:
 * 10.2k, 9.9k), so the interrupt may come thousands of cycles late (it
 * comes about 300 late); gba_test checks it never comes near either.
 * (Nintendo's and Maxmod's mixers do the same. Restarting the timer and
 * emptying the FIFOs at each vertical blank, as this did at first, made a
 * frame's samples depend on when the interrupt came, and on what emptying
 * a FIFO does to the sample being played, which is not known for the
 * hardware.) Then audio_gba_mix() mixes the next frame into the other
 * buffers. Silence is a buffer of zeros in IWRAM, like the others: the
 * DMAs never read the cartridge. The output is 8 bits at 65536 Hz
 * (SOUNDBIAS; the BIOS's 9 at 32768 add nothing to 8-bit samples, and a
 * finer grid shifts the samples less), the bias where the BIOS puts it.
 *
 * Changing the mix restarts the output at its rate: the sound fades out,
 * a frame of silence plays, the FIFOs (holding silence) are emptied and
 * the timer started again, and the sound fades back in where it was.
 *
 * The songs are 8-bit samples, the GBA's own (left and right interleaved),
 * the sound effects too (mono, heard in the middle): mixing adds the sound
 * effects to the music and applies the volumes, nothing is decoded. Each
 * stored sample is the synth's at volume 8, so at the default volume the
 * music comes out as it was stored. A sum past 8 bits (volumes above 8,
 * sound effects over loud music) is bent into them rather than cut off
 * (gba_soft_clip: the same up to 100, then up to 127), which would crackle.
 *
 * The game calls the audio.h functions in its main loop; they leave
 * requests (with the interrupts off for a few instructions) and keep what
 * the game asks back (the song, its time) themselves. The mixer takes the
 * songs and sound effects a tick asks for at the vertical blank that shows
 * the tick's picture (main.c's g_frames.ready), which is later than the
 * next one when the tick or its drawing ran long (a level being loaded):
 * they are heard with the picture. Time: a picture shown at a vertical
 * blank is drawn on the screen from 68 lines later, its middle 148 lines
 * (228.5 samples; the speaker's mix 145.4) later; what is mixed then
 * starts at the next vertical blank, and is heard 23 samples after it
 * (above). So the frame mixed at the vertical blank that shows the tick
 * that starts a song begins AHEAD samples into it (Out.ahead: 352 + 23.2 -
 * 228.5, 224 + 22.9 - 145.4, to a whole number of 4 samples), and the music
 * is heard as the middle of the screen shows the game (the PC skips its
 * output latency so); the player's audio delay plays it that much earlier.
 *
 * Changes in the music (start, stop, pause, the jump back of a loop) fade
 * over FADE samples. A song loops by jumping back by its loop's length
 * (gba_audio.c says where its samples hold the same music twice), with
 * the continuation fading out as the music from the jump target fades in;
 * the length is not a whole number of samples, so the jumps are whole
 * numbers of 4 samples whose sum stays within 2 samples of the loops' (no
 * drift).
 *
 * In IWRAM: audio_gba_vblank(), the buffers and the loops that write the
 * output. The rest runs from the cartridge: it is little work, or rare
 * (requests, fades, starting the output).
 */
#include "audio_gba.h"
#include "audio.h"
#include "frame.h"
#include "gen.h"

#define FRAME_MAX 352             /* samples a frame, at most (the headphones' mix) */
#define FADE 64                   /* samples a fade takes (3 ms; the speaker's mix 5) */
#define VOICES 4                  /* sound effects at once */
#define SFX_QUEUE 8
#define FIFO_DMA (DMA_DST_FIXED | DMA_REPEAT | DMA_32 | DMA_AT_SPECIAL | DMA_ENABLE)
#define SOUNDBIAS_LEVEL 0x03FE
#define SOUNDBIAS_8BIT 0x4000     /* 8 bits at 65536 Hz */
#define SOFT_KNEE 100             /* gba_soft_clip: the same up to this (gba_audio.c) */
/* Direct Sound A on the left, B on the right, both at full volume on Timer 0 */
#define SOUNDCNT_H (SNDA_VOL_100 | SNDA_L | SNDB_VOL_100 | SNDB_R)

/* A mix's output. With the timer started at the start of scanline `line`
 * (its first sample PERIOD + 2 cycles later), the DMAs' requests (after
 * the samples 16 n + 1, as the FIFOs begin with 16 bytes: FIFO_REQ) fall
 * 14 PERIOD before and 2 PERIOD after a vertical blank's start, plus the
 * start's lead over it, (line - 160) * 1232 + 2: the vertical blank is
 * about in the middle (300 cycles after it, when the interrupt restarts
 * the DMAs: line = 160 + (6 PERIOD + 150) / 1232). */
typedef struct {
    uint16_t frame;  /* samples a frame */
    uint16_t period; /* Timer 0's: cycles a sample (280896 / frame) */
    uint16_t ahead;  /* AHEAD (see the top) */
    uint16_t line;   /* Timer 0 starts with this scanline */
    uint32_t count;  /* Timer 1 counts the samples to a whole number of frames, this many */
} Out;
static const Out OUT[2] = {
    {352, 798, 148, 164, 352 * 186},  /* the headphones' */
    {224, 1254, 100, 166, 224 * 292}, /* the speaker's */
};

typedef struct {
    const GbaSong *song;      /* NULL: none */
    const GbaTrack *t;        /* its mix playing */
    int32_t pos;              /* the next sample mixed; < 0: silence first */
    int32_t jump_at;          /* from here the song jumps back (at a frame's start) */
    int32_t residue;          /* sum of the jumps - jumps * loop, in 1/256 samples */
} Music;

typedef struct {
    const int8_t *at;         /* the next frame's samples */
    uint32_t left;            /* frames */
    uint32_t serial;          /* when it started (the oldest goes first) */
    int up;                   /* 3 - its shift: the samples to 8 times as stored */
} Voice;

/* the output buffers ([buffer][left, right]; the speaker's mix: [buffer][0]
 * for both); the DMAs play s_playing */
IWRAM_BSS ALIGN4 static int8_t s_out[2][2][FRAME_MAX];
IWRAM_BSS ALIGN4 static int16_t s_acc[FRAME_MAX]; /* the sound effects, summed, 8 times louder */
static uint8_t s_zeroed[2] = {1, 1}; /* s_out[k] holds zeros (silence: not written again) */
static const int8_t *s_playing[2] = {s_out[0][0], s_out[0][1]}, *s_next[2] = {s_out[0][0], s_out[0][1]};
static uint8_t s_ready;
static uint8_t s_shown; /* this vertical blank shows a new picture: take the requests made for it */
static const Out *s_o = &OUT[GBA_HEADPHONES];
static uint8_t s_mix = GBA_HEADPHONES;
/* changing the mix: none, the sound fading out, silence mixed, the output
 * to restart (at the next vertical blank), restarted (the positions to
 * move to the new mix's samples) */
enum { SW_NONE, SW_FADE, SW_SILENCE, SW_RESTART, SW_NEW };
static uint8_t s_switch, s_from; /* (s_from: the mix before the restart) */
static volatile uint8_t s_want;
static uint8_t s_fresh; /* the output just started (the next restart is not recorded) */

/* mixer state (interrupt only) */
static Music s_mus;
static Music s_old;            /* the music fading out this frame (s_fading) */
static uint8_t s_fading, s_fade_in, s_live; /* s_live: the music was playing in the last frame */
static Voice s_voice[VOICES];
static uint32_t s_serial;

/* requests (main loop -> mixer) */
static volatile uint8_t s_req;
static volatile int8_t s_req_song;
static volatile float s_req_sec; /* where it starts, in seconds of song time */
static volatile uint8_t s_sfx_q[SFX_QUEUE];
static volatile uint8_t s_sfx_head, s_sfx_tail;
static volatile uint8_t s_paused;
static volatile int32_t s_mus_gain = 64, s_sfx_gain = 64; /* volume squared: 8 */

/* what the game asks back */
static volatile int8_t s_song = -1;
volatile int32_t g_audio_emit; /* the next sample mixed: song time + ahead + delay (samples, on through loops) */
volatile GbaAudioStat g_audio_stat;
static float s_delay_sec;      /* the player's audio delay */
static int32_t s_delay;        /* the same in samples of the mix playing */

static inline uint16_t irq_off(void)
{
    uint16_t ime = REG_IME;
    REG_IME = 0;
    return ime;
}

static inline void irq_restore(uint16_t ime)
{
    REG_IME = ime;
}

/* A sum (the 8-bit scale) as an 8-bit sample: past SOFT_KNEE, bent */
static inline int8_t clip8(int32_t v)
{
    if ((uint32_t)(v + SOFT_KNEE) > 2 * SOFT_KNEE) {
        if ((uint32_t)(v + 512) > 1023) v = (v >> 31) ^ 511;
        return gba_soft_clip[v + 512];
    }
    return (int8_t)v;
}

/* Seconds -> samples of song time in mix o, a multiple of 4. */
static int32_t to_samples(const Out *o, float sec)
{
    float w = sec * (float)(o->frame * 15);
    return (int32_t)(w >= 0.0f ? w + 0.5f : w - 0.5f) * 4;
}

/* ------------------------------------------------------------------ */
/* Start-up and the output                                             */
/* ------------------------------------------------------------------ */

/* BIOS SoundBias (SWI 19h): the bias level to 200h, a step at a time (no
 * click) */
static void bios_sound_bias_mid(void)
{
    register uint32_t r0 __asm__("r0") = 1;
#ifdef __thumb__
    __asm__ volatile("swi 0x19" : "+r"(r0) : : "r1", "r2", "r3", "memory");
#else
    __asm__ volatile("swi 0x190000" : "+r"(r0) : : "r1", "r2", "r3", "memory");
#endif
}

/* The output (re)started in mix `mix` (with the interrupts off): the DMAs
 * on s_next, the FIFOs emptied and given 16 bytes of silence (so that the
 * DMAs read whole buffers from the first frame on), Timer 0 from the start
 * of the mix's scanline, Timer 1 counting its samples. At start-up that
 * waits up to a frame; in the vertical blank's interrupt (a change of
 * mix), until its line, 4 or 6 lines on. */
__attribute__((long_call)) static void output_start(int mix)
{
    const Out *o = &OUT[mix];
    int i;
    REG_TM_CNT(0) = 0;
    REG_TM_CNT(1) = 0;
    REG_DMA_CNT_H(1) = 0;
    REG_DMA_CNT_H(2) = 0;
    REG_SOUNDCNT_H = SOUNDCNT_H | SNDA_RESET | SNDB_RESET;
    for (i = 0; i < 4; i++) {
        REG32(REG_FIFO_A) = 0;
        REG32(REG_FIFO_B) = 0;
    }
    REG_DMA_SAD(1) = (uint32_t)s_next[0];
    REG_DMA_SAD(2) = (uint32_t)s_next[1];
    REG_DMA_CNT_H(1) = FIFO_DMA;
    REG_DMA_CNT_H(2) = FIFO_DMA;
    s_playing[0] = s_next[0];
    s_playing[1] = s_next[1];
    REG_TM_D(0) = (uint16_t)(65536 - o->period);
    REG_TM_D(1) = (uint16_t)(65536 - o->count);
    REG_TM_CNT(1) = TM_CASCADE | TM_ENABLE;
    while (REG_VCOUNT == o->line) {
    }
    while (REG_VCOUNT != o->line) {
    }
    REG_TM_CNT(0) = TM_ENABLE;
    s_o = o;
    s_mix = (uint8_t)mix;
    s_fresh = 1;
}

void audio_gba_init(void)
{
    uint16_t ime;
    REG_SOUNDCNT_X = SNDSTAT_ENABLE;
    REG_SOUNDCNT_L = 0;
    /* the bias in the middle (where the BIOS leaves it; a loader might
     * not), and the output at 8 bits and 65536 Hz (see the top) */
    if ((REG_SOUNDBIAS & SOUNDBIAS_LEVEL) != 0x200) bios_sound_bias_mid();
    REG_SOUNDBIAS = (uint16_t)((REG_SOUNDBIAS & SOUNDBIAS_LEVEL) | SOUNDBIAS_8BIT);
    REG_DMA_DAD(1) = REG_FIFO_A;
    REG_DMA_DAD(2) = REG_FIFO_B;
    /* (the vertical blank's interrupt, held meanwhile, comes right after:
     * before the first request) */
    ime = irq_off();
    output_start(GBA_HEADPHONES);
    s_ready = 1;
    irq_restore(ime);
}

IWRAM_CODE void audio_gba_vblank(void)
{
    if (!s_ready) return;
    s_shown = g_frames.ready; /* (main.c clears it after this, as it shows the picture) */
    if (s_switch == SW_RESTART && REG_VCOUNT >= 160 && REG_VCOUNT < OUT[s_want].line) {
        /* (in time for the new mix's line; else at the next vertical
         * blank, the silence played on meanwhile) */
        s_from = s_mix;
        output_start(s_want);
        s_switch = SW_NEW;
        return;
    }
    /* the DMAs on the buffers mixed at the last vertical blank (see the
     * top: they have read the ones before to the end, and will not ask for
     * more for thousands of cycles) */
    REG_DMA_CNT_H(1) = 0;
    REG_DMA_CNT_H(2) = 0;
    REG_DMA_SAD(1) = (uint32_t)s_next[0];
    REG_DMA_SAD(2) = (uint32_t)s_next[1];
    REG_DMA_CNT_H(1) = FIFO_DMA;
    REG_DMA_CNT_H(2) = FIFO_DMA;
    s_playing[0] = s_next[0];
    s_playing[1] = s_next[1];
    if (s_fresh) {
        s_fresh = 0;
    } else {
        g_audio_stat.played = (uint16_t)(REG_TM_D(1) - (uint16_t)(65536 - s_o->count));
        g_audio_stat.frame = s_o->frame;
        g_audio_stat.count++;
    }
}

/* ------------------------------------------------------------------ */
/* Mixing                                                              */
/* ------------------------------------------------------------------ */

/* The sound effects of the voices, summed into s_acc at their volume, in
 * eighths of an output step: S = (the sum, each at 8 times the level it
 * plays at, as stored 1 to 8 times louder) * sg / 64, so that what they add
 * to the output is S / 8 (at most 6350: an int16). */
static IWRAM_CODE void sfx_sum(Voice *const *v, int n, int frame, int32_t sg)
{
    int16_t *acc = s_acc;
    const int8_t *a = v[0]->at;
    int i, k, up = v[0]->up;
    for (i = 0; i < frame; i++) acc[i] = (int16_t)(a[i] << up);
    for (k = 1; k < n; k++) {
        a = v[k]->at;
        up = v[k]->up;
        for (i = 0; i < frame; i++) acc[i] = (int16_t)(acc[i] + (a[i] << up));
    }
    if (sg != 64)
        for (i = 0; i < frame; i++) acc[i] = (int16_t)(acc[i] * sg >> 6);
}

/* out[from..frame) from the music at src (as stored) and the sound effects
 * (acc: sfx_sum's, or NULL), at the music's gain mg (64: as stored); or
 * NULL: the speaker's mix, in ol alone. Output = music * mg / 64 + S / 8,
 * worked out as (music * mg + S * 8) / 64. */
static IWRAM_CODE void mix_run(int8_t *ol, int8_t *or, int from, int frame, const int8_t *src, int channels,
                               const int16_t *acc, int32_t mg)
{
    int i;
    if (src && !acc && mg == 64 && !(from & 3)) {
        /* the music alone at the default volume: the samples as stored, a
         * word (4 of each side) at a time (src, from: multiples of 4) */
        const uint32_t *s = (const uint32_t *)src;
        uint32_t *l = (uint32_t *)(ol + from), *r = or ? (uint32_t *)(or + from) : 0;
        if (channels == 2) {
            for (i = from; i < frame; i += 4) {
                uint32_t a = s[0], b = s[1]; /* L0 R0 L1 R1, L2 R2 L3 R3 */
                s += 2;
                *l++ = (a & 0xFF) | ((a >> 8) & 0xFF00) | ((b & 0xFF) << 16) | ((b << 8) & 0xFF000000u);
                *r++ = ((a >> 8) & 0xFF) | ((a >> 16) & 0xFF00) | ((b & 0xFF00) << 8) | (b & 0xFF000000u);
            }
        } else if (or) {
            for (i = from; i < frame; i += 4) *l++ = *r++ = *s++;
        } else {
            for (i = from; i < frame; i += 4) *l++ = *s++;
        }
        return;
    }
    if (src && channels == 2) {
        const uint16_t *s = (const uint16_t *)src;
        if (acc && mg == 64) {
            /* (the default volume, effects playing: an add a side) */
            for (i = from; i < frame; i++) {
                uint32_t lr = *s++;
                int32_t a = acc[i];
                ol[i] = clip8(((int8_t)lr * 8 + a) >> 3);
                or[i] = clip8(((int8_t)(lr >> 8) * 8 + a) >> 3);
            }
        } else if (acc) {
            for (i = from; i < frame; i++) {
                uint32_t lr = *s++;
                int32_t a = acc[i] * 8;
                ol[i] = clip8(((int8_t)lr * mg + a) >> 6);
                or[i] = clip8(((int8_t)(lr >> 8) * mg + a) >> 6);
            }
        } else {
            for (i = from; i < frame; i++) {
                uint32_t lr = *s++;
                ol[i] = clip8((int8_t)lr * mg >> 6);
                or[i] = clip8((int8_t)(lr >> 8) * mg >> 6);
            }
        }
    } else if (src) {
        for (i = from; i < frame; i++) {
            int32_t v = *src++ * mg;
            if (acc) v += acc[i] * 8;
            ol[i] = clip8(v >> 6);
        }
        if (or)
            for (i = from; i < frame; i++) or[i] = ol[i];
    } else {
        for (i = from; i < frame; i++) ol[i] = clip8(acc[i] >> 3);
        if (or)
            for (i = from; i < frame; i++) or[i] = ol[i];
    }
}

/* The start of a frame whose music fades (in: a song started or resumed,
 * the start of a loop; out: a song stopped, paused or changed, the end of a
 * loop) or begins after silence: out[0..end), sample by sample with the
 * fades' weights; returns end. (ARM code in IWRAM, the pointers taken out
 * of the loop: a fade frame was 20,000 cycles from the cartridge.) */
static IWRAM_CODE int music_head(int play, int8_t *ol, int8_t *or, int frame, const int16_t *acc, int32_t mg)
{
    const Music *m = &s_mus, *o = &s_old;
    int i0 = play && m->pos < 0 ? (int)-m->pos : 0; /* where the song begins in the frame */
    int end = s_fading ? FADE : 0, i, fade_in = s_fade_in, fading = s_fading;
    const int8_t *nd = play ? m->t->data : 0, *od = fading ? o->t->data : 0;
    int nc = play ? m->t->channels : 0, oc = fading ? o->t->channels : 0;
    int32_t np = m->pos, op = o->pos;
    if (play && i0 + (fade_in ? FADE : 0) > end) end = i0 + (fade_in ? FADE : 0);
    if (end > frame) end = frame;
    for (i = 0; i < end; i++) {
        int32_t ml = 0, mr = 0, a = acc ? acc[i] << 9 : 0;
        if (play && i >= i0) {
            int32_t w = fade_in && i - i0 < FADE ? i - i0 : FADE, p = np + i;
            if (nc == 2) {
                ml = nd[2 * p] * w;
                mr = nd[2 * p + 1] * w;
            } else {
                ml = mr = nd[p] * w;
            }
        }
        if (fading && i < FADE && op + i >= 0) {
            int32_t w = FADE - i, p = op + i;
            if (oc == 2) {
                ml += od[2 * p] * w;
                mr += od[2 * p + 1] * w;
            } else {
                ml += od[p] * w;
                mr += od[p] * w;
            }
        }
        if (or) {
            ol[i] = clip8((ml * mg + a) >> 12);
            or[i] = clip8((mr * mg + a) >> 12);
        } else {
            ol[i] = clip8(((ml + mr) * mg / 2 + a) >> 12);
        }
    }
    return end;
}

/* The next jump back of the music: a whole number of 4 samples, as close to
 * the loop's length as keeps the jumps' sum within 2 samples of it. */
static int32_t next_jump(Music *m)
{
    int32_t target = (int32_t)m->t->loop - m->residue;
    int32_t d = (target + 512) >> 10 << 2;
    m->residue += d * 256 - (int32_t)m->t->loop;
    return d;
}

/* The song in the mix playing, from pos (its loop's jumps begun anew). */
static void music_place(Music *m, int32_t pos)
{
    m->t = &m->song->mix[s_mix];
    m->pos = pos;
    m->residue = 0;
    m->jump_at = (int32_t)m->t->samples - s_o->frame - FADE;
    while (m->pos >= m->jump_at) m->pos -= next_jump(m);
}

/* What was playing fades out over the next frame's start (if it was heard,
 * and not already: a song asked for as the music pauses is not). */
static void fade_out_current(void)
{
    if (s_live && s_mus.song && !s_fading) {
        s_old = s_mus;
        s_fading = 1;
    }
}

/* The requests the game left for the picture now shown: sound effects to
 * start, a song to start or stop. */
static void take_requests(void)
{
    Music *m = &s_mus;
    if ((uint8_t)(s_sfx_head - s_sfx_tail) > VOICES) /* (the others would be replaced at once) */
        s_sfx_tail = (uint8_t)(s_sfx_head - VOICES);
    while (s_sfx_tail != s_sfx_head) {
        int id = s_sfx_q[s_sfx_tail % SFX_QUEUE], k, pick = 0;
        s_sfx_tail++;
        if (id >= GBA_SFX_COUNT || s_switch) continue;
        for (k = 1; k < VOICES; k++) /* a free voice, else the oldest */
            if (s_voice[pick].left && (!s_voice[k].left || s_voice[k].serial < s_voice[pick].serial)) pick = k;
        s_voice[pick].at = gba_sfx[id].mix[s_mix].data;
        s_voice[pick].left = gba_sfx[id].mix[s_mix].samples / s_o->frame;
        s_voice[pick].up = 3 - gba_sfx[id].mix[s_mix].shift;
        s_voice[pick].serial = s_serial++;
    }
    if (s_req) {
        int song = s_req_song;
        fade_out_current();
        m->song = song >= 0 && song < GBA_SONG_COUNT ? &gba_songs[song] : 0;
        s_fade_in = 1;
        if (m->song) music_place(m, to_samples(s_o, s_req_sec) + s_delay + s_o->ahead);
        s_req = 0;
    }
}

/* The output just restarted in another mix (SW_NEW): the song's place and
 * the time told to the game in its samples. */
static void move_to_mix(const Out *from)
{
    const Out *to = s_o;
    float t = (float)(g_audio_emit - from->ahead - s_delay) * (1.0f / (from->frame * 60));
    s_delay = to_samples(to, s_delay_sec);
    g_audio_emit = to_samples(to, t) + s_delay + to->ahead;
    if (s_mus.song) {
        float p = (float)(s_mus.pos - from->ahead) * (1.0f / (from->frame * 60));
        music_place(&s_mus, to_samples(to, p) + to->ahead);
    }
    s_fade_in = 1;
}

void audio_gba_mix(void)
{
    Music *m = &s_mus;
    Voice *act[VOICES];
    const int16_t *acc = 0;
    int8_t *ol, *or;
    int32_t mg, sg;
    int music, play, n = 0, k, done = 0, frame, buf;
    if (!s_ready) return;
    if (s_switch == SW_NEW) {
        move_to_mix(&OUT[s_from]);
        s_switch = SW_NONE;
    } else if (s_switch == SW_NONE && s_want != s_mix) {
        s_switch = SW_FADE;
    }
    frame = s_o->frame;
    buf = s_playing[0] == s_out[0][0] ? 1 : 0;
    ol = s_out[buf][0];
    or = s_mix == GBA_SPEAKER ? 0 : s_out[buf][1];
    mg = s_mus_gain;
    sg = s_sfx_gain;
    if (s_shown) take_requests();
    music = m->song && !s_paused && !s_switch;
    if (s_live && !music) { /* paused, stopped or the mix changing: fade out */
        fade_out_current();
    } else if (!s_live && music) { /* resumed */
        s_fade_in = 1;
    }
    if (music && m->pos >= m->jump_at) { /* the loop's end fades out as it starts again */
        fade_out_current();
        m->pos -= next_jump(m);
        s_fade_in = 1;
    }

    /* the sound effects, summed (the mix changing: fading out, then gone) */
    for (k = 0; k < VOICES; k++)
        if (s_voice[k].left) act[n++] = &s_voice[k];
    if (n) {
        sfx_sum(act, n, frame, sg);
        acc = s_acc;
        for (k = 0; k < n; k++) {
            act[k]->left--;
            act[k]->at += frame;
            if (s_switch) act[k]->left = 0;
        }
        if (s_switch)
            for (k = 0; k < frame; k++) s_acc[k] = (int16_t)(k < FADE ? s_acc[k] * (FADE - k) / FADE : 0);
    }

    /* the music: what fades or begins after silence first, then the rest
     * straight into the output */
    play = music;
    if (music && m->pos <= -frame) { /* silence before the song, all frame */
        m->pos += frame;
        play = 0;
    }
    if (s_fading || (play && (s_fade_in || m->pos < 0))) done = music_head(play, ol, or, frame, acc, mg);
    s_fading = 0;
    if (play) s_fade_in = 0;
    if (done < frame) {
        if (play) {
            const GbaTrack *t = m->t;
            mix_run(ol, or, done, frame, t->data + (m->pos + done) * t->channels, t->channels, acc, mg);
        } else if (acc) {
            mix_run(ol, or, done, frame, 0, 1, acc, mg);
        } else if (!done) {
            /* silence: zeros in the buffer (once) */
            if (!s_zeroed[buf]) {
                uint32_t *z = (uint32_t *)s_out[buf];
                for (k = 0; k < (int)(sizeof(s_out[0]) / 4); k++) z[k] = 0;
            }
        } else {
            for (k = done; k < frame; k++) {
                ol[k] = 0;
                if (or) or[k] = 0;
            }
        }
    }
    s_zeroed[buf] = (uint8_t)(!done && !play && !acc);
    if (play) m->pos += frame;
    s_live = (uint8_t)music;
    if (music && !s_req) g_audio_emit += frame; /* (while a song waits to begin, g_audio_emit is its start) */
    s_next[0] = ol;
    s_next[1] = or ? or : ol;
    if (s_switch == SW_FADE) s_switch = SW_SILENCE;
    else if (s_switch == SW_SILENCE && !s_fading) s_switch = SW_RESTART;
}

void audio_set_output(int speaker)
{
    s_want = (uint8_t)(speaker ? GBA_SPEAKER : GBA_HEADPHONES);
}

/* ------------------------------------------------------------------ */
/* audio.h                                                             */
/* ------------------------------------------------------------------ */

void audio_init(void)
{
    /* the frontend calls audio_gba_init() */
}

void audio_mix(int16_t *out, int frames)
{
    /* the GBA mixes in its vertical blank interrupt */
    int i;
    for (i = 0; i < 2 * frames; i++) out[i] = 0;
}

void audio_set_latency(float sec)
{
    /* fixed on the GBA: AHEAD (see the top) */
    (void)sec;
}

void audio_set_user_delay(float sec)
{
    uint16_t ime = irq_off();
    s_delay_sec = sec;
    s_delay = to_samples(s_o, sec);
    irq_restore(ime);
}

void audio_play_song(int song, float start_sec)
{
    uint16_t ime = irq_off();
    s_req_song = (int8_t)song;
    s_req_sec = start_sec;
    s_req = 1;
    s_song = (int8_t)song;
    g_audio_emit = to_samples(s_o, start_sec) + s_delay + s_o->ahead;
    irq_restore(ime);
}

void audio_stop_song(void)
{
    uint16_t ime = irq_off();
    s_req_song = -1;
    s_req = 1;
    s_song = -1;
    irq_restore(ime);
}

void audio_sfx(int id)
{
    uint8_t head = s_sfx_head;
    if ((uint8_t)(head - s_sfx_tail) >= SFX_QUEUE) return;
    s_sfx_q[head % SFX_QUEUE] = (uint8_t)id;
    s_sfx_head = (uint8_t)(head + 1);
}

void audio_set_volume(int music, int sfx)
{
    /* the synth's curve, (volume / 10)^2, against volume 8, which the
     * songs were rendered at: gain v^2, 64 being as rendered */
    music = music < 0 ? 0 : music > 10 ? 10 : music;
    sfx = sfx < 0 ? 0 : sfx > 10 ? 10 : sfx;
    s_mus_gain = music * music;
    s_sfx_gain = sfx * sfx;
}

void audio_pause(int paused)
{
    s_paused = (uint8_t)(paused != 0);
}

float audio_song_time(void)
{
    int32_t emit, base;
    int rate;
    uint16_t ime;
    if (s_song < 0) return 0.0f;
    ime = irq_off();
    emit = g_audio_emit;
    base = s_o->ahead + s_delay;
    rate = s_o->frame * 60;
    irq_restore(ime);
    return (float)(emit - base) / (float)rate;
}

float audio_song_beat(void)
{
    int song = s_song;
    if (song < 0 || song >= GBA_SONG_COUNT) return 0.0f;
    return audio_song_time() * gba_songs[song].bpm * (1.0f / 60.0f);
}

int audio_current_song(void)
{
    return s_song;
}

int audio_song_count(void)
{
    return GBA_SONG_COUNT;
}

const char *audio_song_name(int song)
{
    return song >= 0 && song < GBA_SONG_COUNT ? gba_songs[song].name : "";
}

float audio_song_bpm(int song)
{
    return song >= 0 && song < GBA_SONG_COUNT ? gba_songs[song].bpm : 120.0f;
}

float audio_song_length(int song)
{
    return song >= 0 && song < GBA_SONG_COUNT ? gba_songs[song].length : 0.0f;
}

/* The tool helpers: the GBA has no note data. */
int audio_pattern_steps(const char *data, int drum)
{
    (void)data;
    (void)drum;
    return 0;
}

int audio_song_bars(int song)
{
    return (int)(audio_song_length(song) * audio_song_bpm(song) * (1.0f / 240.0f) + 0.5f);
}

int audio_song_notes(int song, const AudioNote **notes, uint32_t *len_steps, uint32_t *loop_step)
{
    (void)song;
    *notes = 0;
    *len_steps = 0;
    *loop_step = 0;
    return 0;
}
