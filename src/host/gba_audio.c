/*
 * The Game Boy Advance ROM's songs and sound effects (gba_tool export,
 * audiocheck).
 *
 * The GBA can't run the core's synthesizer (floats throughout), so it
 * plays what the synthesizer plays on the PC: each song and sound effect
 * is rendered here by src/core/audio.c, as `pd_tool wav` renders it (48 kHz
 * stereo, at volume 8, the default), resampled to 21120 Hz (352 samples a
 * tick: 1/60 s of song time, which the GBA plays in one frame,
 * audio_gba.c) and stored as 8-bit samples, the GBA's own: the songs in
 * stereo (left and right interleaved; the GBA plays them on its two Direct
 * Sound channels), the sound effects in mono, made up to 8 times louder
 * (a power of two, as far as the loudest sample allows: they are quiet,
 * and 8 bits would lose them) for the GBA to make quieter again as it
 * mixes them. Each sample is the render
 * rounded to 8 bits with the rounding error of the one before taken off
 * (first-order noise shaping: the 8-bit noise moves up, away from where
 * the music is), so nothing is decoded on the GBA: its mixer adds the
 * sound effects and applies the volumes.
 *
 * Each is stored a second time mixed for the GBA's speaker (the options'
 * OUTPUT): a tiny speaker plays next to nothing below a few hundred Hz,
 * yet bass takes most of the music's level, so on it the headphones' mix
 * is quiet and what it can play is coarse in 8 bits. The speaker's mix is
 * in mono (the speaker is one), at 13440 Hz (224 samples a tick: it plays
 * little above that's half, and the cartridge has room for that, not for
 * 21120), its bass cut below SPK_HP (4th-order high-pass), compressed (the
 * quiet parts brought up) and limited to SPK_CEIL (a look-ahead limiter),
 * and rounded to 8 bits without noise shaping (at this rate the shaped
 * noise would rise where the speaker plays best). The sound effects get
 * the same high-pass and the gain the compression gave the songs.
 *
 * The songs take 42 KB a second in stereo and 13 KB in the speaker's mix.
 * If they ever outgrow what the cartridge has room for (SONG_BUDGET), the
 * headphones' mix is stored in mono (half), and the GBA plays both
 * channels from one.
 *
 * Songs loop as the sequencer loops them, back to their loop bar (S) at
 * the end of the arrangement (L). The menu, practice and metronome loops
 * keep [0, 2L - S): the first pass and a second pass of the loop, played
 * on from the first, so the jump back by L - S lands where the loop sounds
 * as it does after itself (its echoes and releases). The levels' songs,
 * which loop only while the results are shown, keep [0, L) and 2 seconds
 * after it (the loop's start, with the end's tails ringing out): the GBA
 * crossfades from there to the same point of the first pass.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gba_gen.h"
#include "../core/audio.h"

#define FRAME 352          /* samples a tick (audio_gba.c's frame, headphones) */
#define RATE (FRAME * 60)  /* samples per second of song time: 21120 */
#define UP 11              /* RATE / AUDIO_RATE = 11 / 25 */
#define DOWN 25
#define CUTOFF 9700.0      /* Hz; stops at about 10.5 kHz, RATE's half */
#define S_FRAME 224        /* the speaker's mix: samples a tick */
#define S_RATE (S_FRAME * 60) /* 13440 */
#define S_UP 7             /* S_RATE / AUDIO_RATE = 7 / 25 */
#define S_CUTOFF 6150.0    /* stops at about 6.7 kHz */
#define TAPS_HALF 80       /* the resampling filters: 160 taps at 48 kHz */
#define KAISER_BETA 7.86
/* the speaker's mix (see the top) */
#define SPK_HP 300.0       /* Hz: the high-pass' corner */
#define SPK_RATIO 3.0      /* the compressor: above the song's average level */
#define SPK_KNEE 6.0       /* dB */
#define SPK_ATTACK 0.005   /* s */
#define SPK_RELEASE 0.2
#define SPK_RMS_TIME 0.01  /* s: the level the compressor follows */
#define SPK_RMS -13.0      /* dB under full scale: the mix's level after it */
#define SPK_CEIL (120.0 * 256.0) /* the limiter's (int16 scale: 120 of 127) */
#define SPK_LOOKAHEAD 32   /* samples (2.4 ms) */
#define SPK_LIM_RELEASE 0.06 /* s */
#define SPK_LIMIT_MAX 2.0  /* the most the limiter may take off the loudest 0.1% */
#define LEVEL_TAIL 2.0     /* seconds kept after a level song's arrangement */
#define SFX_MAX 3.0        /* seconds, at most, of a sound effect */
/* A sound effect ends where it stays below this (int16): half a step of
 * the 8-bit output at volume 8. What comes after (the echo's repeats, 42 dB
 * and more below full scale) would only hold one of the GBA's 4 voices. */
#define SFX_SILENT 128
/* What the songs may take of the cartridge's 32 MB: the rest is the code,
 * the graphics and the sound effects (under 1 MB), with room to spare */
#define SONG_BUDGET (30u << 20)
/* The bar (SNR of the 8-bit samples against the render, the shaped noise
 * counted in full): these songs get 32 to 35 dB, the sound effects 26 to 33
 * (the loud ones can't be stored louder); less means something is broken. */
#define MIN_SNR_SONG 30.0
#define MIN_SNR_SFX 25.0
/* the speaker's mix, rounded (its reference is the mix before rounding):
 * 38 to 40 dB */
#define MIN_SNR_SPK 34.0

static const char *const SFX_NAMES[SFX_COUNT] = {
    "death", "coin", "checkpoint", "menu move", "menu select", "menu back", "complete", "start",
};

typedef struct {
    int16_t *pcm[2];    /* the reference, resampled: left, right (sound effects: [0] only) */
    uint32_t samples;   /* a multiple of the frame */
    int channels;       /* stored: 2 (left, right interleaved) or 1 */
    int shift;          /* stored that many times twice as loud (sound effects) */
    int8_t *data;       /* samples * channels */
    uint32_t loop;      /* the loop's length in 1/256 samples (songs) */
    int rounded;        /* stored rounded rather than noise-shaped (the speaker's mix) */
} Track;

/* what the speaker's mix of the songs did, for the sound effects' gain */
static double s_spk_gain_sum;
static int s_spk_gains;

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "gba_audio: out of memory\n");
        exit(1);
    }
    return p;
}

/* A silent synthesizer at volume 8 with nothing queued: whatever ran
 * before in this process (the commands the game's code queued, sound
 * effects' later notes) is played out, then the voices and the echo are
 * cleared. The echo's time is the last song's: the menu's, where the
 * sound effects mostly play. */
static void synth_reset(void)
{
    static int16_t buf[2 * 4800];
    int i;
    audio_set_latency(0.0f);
    audio_set_user_delay(0.0f);
    audio_pause(0);
    audio_play_song(SONG_MENU, 0.0f);
    audio_stop_song();
    for (i = 0; i < 10; i++) audio_mix(buf, 4800);
    audio_init();
    audio_set_volume(8, 8);
}

/* Renders `frames` frames (48 kHz) of what is playing: left and right. */
static void render(uint32_t frames, float **left, float **right)
{
    static int16_t buf[2 * 4800];
    float *l = xmalloc(frames * sizeof(float)), *r = xmalloc(frames * sizeof(float));
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done < 4800 ? frames - done : 4800, i;
        audio_mix(buf, (int)n);
        for (i = 0; i < n; i++) {
            l[done + i] = (float)buf[2 * i];
            r[done + i] = (float)buf[2 * i + 1];
        }
        done += n;
    }
    *left = l;
    *right = r;
}

/* ------------------------------------------------------------------ */
/* Resampling: 48 kHz -> 21120 Hz (13440), windowed-sinc polyphase       */
/* ------------------------------------------------------------------ */

static float s_fir[UP][2 * TAPS_HALF], s_fir_s[S_UP][2 * TAPS_HALF];

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    int k;
    for (k = 1; k < 40; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

static void fir_init(float (*fir)[2 * TAPS_HALF], int up, double cutoff)
{
    const double fc = cutoff / AUDIO_RATE;
    int ph, j;
    for (ph = 0; ph < up; ph++) {
        double sum = 0.0, h[2 * TAPS_HALF];
        /* output n is at input time (n * DOWN) / up: whole part i0, phase
         * ph / up; tap j is input sample i0 - TAPS_HALF + 1 + j */
        for (j = 0; j < 2 * TAPS_HALF; j++) {
            double x = (double)(j - TAPS_HALF + 1) - (double)ph / up;
            double u = x / TAPS_HALF, w = 0.0;
            double s = x == 0.0 ? 1.0 : sin(2.0 * M_PI * fc * x) / (2.0 * M_PI * fc * x);
            if (u > -1.0 && u < 1.0) w = bessel_i0(KAISER_BETA * sqrt(1.0 - u * u)) / bessel_i0(KAISER_BETA);
            h[j] = s * w;
            sum += h[j];
        }
        for (j = 0; j < 2 * TAPS_HALF; j++) fir[ph][j] = (float)(h[j] / sum);
    }
}

/* Output sample i at RATE (up = UP) or S_RATE (S_UP) from `in` (48 kHz,
 * nin frames; zeros before it). */
static float resample_at(const float *in, uint32_t nin, uint32_t i, int up, float (*fir)[2 * TAPS_HALF])
{
    uint64_t t = (uint64_t)i * DOWN;
    int64_t base = (int64_t)(t / (unsigned)up) - TAPS_HALF + 1;
    const float *h = fir[t % (unsigned)up];
    float acc = 0.0f;
    int j;
    for (j = 0; j < 2 * TAPS_HALF; j++) {
        int64_t k = base + j;
        if (k >= 0 && k < (int64_t)nin) acc += in[k] * h[j];
    }
    return acc;
}

/* n samples at RATE from `in`, rounded to int16. */
static int16_t *resample(const float *in, uint32_t nin, uint32_t n)
{
    int16_t *out = xmalloc(n * sizeof(int16_t));
    uint32_t i;
    for (i = 0; i < n; i++) {
        long v = lrintf(resample_at(in, nin, i, UP, s_fir));
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* The speaker's mix                                                   */
/* ------------------------------------------------------------------ */

/* A 2nd-order Butterworth-family high-pass section (the RBJ cookbook's) */
typedef struct {
    double b0, b1, b2, a1, a2, x1, x2, y1, y2;
} Biquad;

static void hp_init(Biquad *f, double fc, double q)
{
    double w = 2.0 * M_PI * fc / AUDIO_RATE, al = sin(w) / (2.0 * q), c = cos(w), a0 = 1.0 + al;
    memset(f, 0, sizeof(*f));
    f->b0 = (1.0 + c) / 2.0 / a0;
    f->b1 = -(1.0 + c) / a0;
    f->b2 = f->b0;
    f->a1 = -2.0 * c / a0;
    f->a2 = (1.0 - al) / a0;
}

static double hp_run(Biquad *f, double x)
{
    double y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1;
    f->x1 = x;
    f->y2 = f->y1;
    f->y1 = y;
    return y;
}

/* n samples at S_RATE of the render (48 kHz; left and right, or left
 * only): in mono, below SPK_HP cut (4th-order Butterworth: two sections) */
static float *speaker_base(const float *l, const float *r, uint32_t nin, uint32_t n)
{
    float *m = xmalloc(nin * sizeof(float)), *out = xmalloc(n * sizeof(float));
    Biquad f1, f2;
    uint32_t i;
    hp_init(&f1, SPK_HP, 0.54119610);
    hp_init(&f2, SPK_HP, 1.30656296);
    for (i = 0; i < nin; i++) m[i] = (float)hp_run(&f2, hp_run(&f1, r ? 0.5 * (l[i] + r[i]) : l[i]));
    for (i = 0; i < n; i++) out[i] = resample_at(m, nin, i, S_UP, s_fir_s);
    free(m);
    return out;
}

static double rms_of(const float *x, uint32_t n)
{
    double sum = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++) sum += (double)x[i] * x[i];
    return n ? sqrt(sum / n) : 0.0;
}

/* The loudest |x| but for the loudest 0.1% */
static double peak_999(const float *x, uint32_t n)
{
    /* (a histogram of |x| in 1/16 dB steps from 60 dB under full scale) */
    static uint32_t hist[1024];
    uint32_t i, skip = n / 1000, seen = 0;
    int k;
    memset(hist, 0, sizeof(hist));
    for (i = 0; i < n; i++) {
        double a = fabs(x[i]) / 32768.0;
        int b = a > 1e-3 ? (int)((20.0 * log10(a) + 60.0) * 16.0) : 0;
        hist[b < 0 ? 0 : b > 1023 ? 1023 : b]++;
    }
    for (k = 1023; k > 0; k--) {
        seen += hist[k];
        if (seen > skip) break;
    }
    return 32768.0 * pow(10.0, (k / 16.0 - 60.0) / 20.0);
}

/* The compressor, then the gain that brings the mix to SPK_RMS (no more
 * than leaves SPK_LIMIT_MAX to the limiter), then the limiter: x in place.
 * Returns the gain the whole gave the song's level. */
static double speaker_dynamics(float *x, uint32_t n)
{
    const double fs = S_RATE, full = 32768.0;
    const double a_rms = 1.0 - exp(-1.0 / (SPK_RMS_TIME * fs)), a_att = 1.0 - exp(-1.0 / (SPK_ATTACK * fs)),
                 a_rel = 1.0 - exp(-1.0 / (SPK_RELEASE * fs)), a_lim = 1.0 - exp(-1.0 / (SPK_LIM_RELEASE * fs));
    double in_rms = rms_of(x, n), thr, e = 0.0, g = 0.0, make, p, out_rms;
    float *gmin, *gain;
    uint32_t i;
    int j;
    if (in_rms < 1.0) return 1.0;
    /* the threshold: the song's own average level */
    thr = 20.0 * log10(in_rms / full);
    e = in_rms * in_rms;
    for (i = 0; i < n; i++) {
        double ldb, over, want;
        e += a_rms * ((double)x[i] * x[i] - e);
        ldb = 10.0 * log10(e / (full * full) + 1e-12);
        over = ldb - thr;
        if (over <= -SPK_KNEE / 2) want = 0.0;
        else if (over >= SPK_KNEE / 2) want = -(1.0 - 1.0 / SPK_RATIO) * over;
        else want = -(1.0 - 1.0 / SPK_RATIO) * (over + SPK_KNEE / 2) * (over + SPK_KNEE / 2) / (2.0 * SPK_KNEE);
        g += (want < g ? a_att : a_rel) * (want - g);
        x[i] = (float)(x[i] * pow(10.0, g / 20.0));
    }
    /* the level, and what the limiter is left */
    make = full * pow(10.0, SPK_RMS / 20.0) / rms_of(x, n);
    p = peak_999(x, n) * make;
    if (p > SPK_CEIL * SPK_LIMIT_MAX) make *= SPK_CEIL * SPK_LIMIT_MAX / p;
    for (i = 0; i < n; i++) x[i] = (float)(x[i] * make);
    /* the limiter: each sample's gain the least any sample from it to
     * SPK_LOOKAHEAD on needs, averaged over the SPK_LOOKAHEAD + 1 before it
     * (so it falls ahead of a peak and is under what the peak needs), and
     * coming back by SPK_LIM_RELEASE */
    gmin = xmalloc(n * sizeof(float));
    gain = xmalloc(n * sizeof(float));
    for (i = 0; i < n; i++) {
        double a = fabs(x[i]);
        gmin[i] = a > SPK_CEIL ? (float)(SPK_CEIL / a) : 1.0f;
    }
    for (i = 0; i < n; i++) {
        float m = gmin[i];
        for (j = 1; j <= SPK_LOOKAHEAD && i + j < n; j++)
            if (gmin[i + j] < m) m = gmin[i + j];
        gain[i] = m;
    }
    {
        double sum = 0.0, back = 1.0;
        for (i = 0; i < n; i++) {
            double avg;
            sum += gain[i];
            if (i > SPK_LOOKAHEAD) sum -= gain[i - SPK_LOOKAHEAD - 1];
            avg = sum / (i < SPK_LOOKAHEAD ? i + 1 : SPK_LOOKAHEAD + 1);
            back += a_lim * (1.0 - back);
            if (back > avg) back = avg;
            x[i] = (float)(x[i] * back);
            /* (the sum's rounding aside, already within) */
            if (x[i] > SPK_CEIL) x[i] = (float)SPK_CEIL;
            if (x[i] < -SPK_CEIL) x[i] = (float)-SPK_CEIL;
        }
    }
    free(gmin);
    free(gain);
    out_rms = rms_of(x, n);
    return out_rms / in_rms;
}

/* ------------------------------------------------------------------ */
/* 8 bits                                                              */
/* ------------------------------------------------------------------ */

/* x (int16) as 8-bit samples into out[0], out[step], ...: each rounded with
 * the last one's rounding error taken off first (the noise's spectrum then
 * rises with frequency instead of lying flat under the music). */
static void to_8bit(const int16_t *x, uint32_t n, int8_t *out, int step, int shift)
{
    float err = 0.0f, k = (float)(1 << shift) / 256.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float want = (float)x[i] * k - err;
        long q = lrintf(want);
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        err = (float)q - want;
        /* (a clipped sample must not carry its whole error onward) */
        if (err > 1.0f || err < -1.0f) err = 0.0f;
        out[(size_t)i * step] = (int8_t)q;
    }
}

/* The speaker's mix (int16 scale) as 8-bit samples, rounded (see the
 * top). */
static void round_8bit(const int16_t *x, uint32_t n, int8_t *out, int shift)
{
    float k = (float)(1 << shift) / 256.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        long q = lrintf((float)x[i] * k);
        out[i] = (int8_t)(q > 127 ? 127 : q < -128 ? -128 : q);
    }
}

static void store(Track *t, int channels)
{
    int c;
    t->channels = channels;
    t->shift = 0;
    t->data = xmalloc((size_t)t->samples * channels);
    if (channels == 2) {
        for (c = 0; c < 2; c++) to_8bit(t->pcm[c], t->samples, t->data + c, 2, 0);
    } else if (t->pcm[1]) {
        /* (a song in mono: the two sides' average) */
        int16_t *m = xmalloc(t->samples * sizeof(int16_t));
        uint32_t i;
        for (i = 0; i < t->samples; i++) m[i] = (int16_t)(((int32_t)t->pcm[0][i] + t->pcm[1][i]) / 2);
        to_8bit(m, t->samples, t->data, 1, 0);
        free(m);
    } else {
        /* a sound effect: as loud as its loudest sample allows (the
         * speaker's mix of a song: at shift 0, as it is) */
        uint32_t i;
        int peak = 1;
        for (i = 0; i < t->samples; i++)
            if (abs(t->pcm[0][i]) > peak) peak = abs(t->pcm[0][i]);
        while (!t->loop && t->shift < 3 && (peak << (t->shift + 1)) < 32512) t->shift++;
        if (t->rounded) round_8bit(t->pcm[0], t->samples, t->data, t->shift);
        else to_8bit(t->pcm[0], t->samples, t->data, 1, t->shift);
    }
}

static void track_free(Track *t)
{
    free(t->pcm[0]);
    free(t->pcm[1]);
    free(t->data);
}

/* int16 from the speaker's mix (within SPK_CEIL already) */
static int16_t *to_int16(const float *x, uint32_t n)
{
    int16_t *out = xmalloc(n * sizeof(int16_t));
    uint32_t i;
    for (i = 0; i < n; i++) {
        long v = lrintf(x[i]);
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Songs and sounds                                                    */
/* ------------------------------------------------------------------ */

/* Arrangement length L and loop start S of a song, in seconds. */
static void song_loop(int song, double *len, double *loop_start)
{
    const AudioNote *notes;
    uint32_t len_steps, loop_step;
    double bpm = audio_song_bpm(song);
    audio_song_notes(song, &notes, &len_steps, &loop_step);
    *len = len_steps * 15.0 / bpm;
    *loop_start = loop_step * 15.0 / bpm;
}

/* How many samples a song keeps (a multiple of FRAME). */
static uint32_t song_samples(int song)
{
    double len, start;
    song_loop(song, &len, &start);
    return (uint32_t)ceil((song < SONG_FIRST_LEVEL ? 2.0 * len - start : len + LEVEL_TAIL) * 60.0 - 1e-9) * FRAME;
}

/* A song: the headphones' mix into t, and the speaker's into sp (if
 * not NULL), the same ticks long. */
static void make_song(int song, Track *t, Track *sp)
{
    double len, start;
    uint32_t frames;
    float *l, *r;
    song_loop(song, &len, &start);
    t->samples = song_samples(song);
    t->loop = (uint32_t)llround((len - start) * RATE * 256.0);
    t->rounded = 0;
    frames = t->samples / FRAME * (AUDIO_RATE / 60) + TAPS_HALF + 1;
    synth_reset();
    audio_play_song(song, 0.0f);
    render(frames, &l, &r);
    audio_stop_song();
    t->pcm[0] = resample(l, frames, t->samples);
    t->pcm[1] = resample(r, frames, t->samples);
    t->data = NULL;
    if (sp) {
        float *x;
        sp->samples = t->samples / FRAME * S_FRAME;
        sp->loop = (uint32_t)llround((len - start) * S_RATE * 256.0);
        x = speaker_base(l, r, frames, sp->samples);
        s_spk_gain_sum += speaker_dynamics(x, sp->samples);
        s_spk_gains++;
        sp->pcm[0] = to_int16(x, sp->samples);
        sp->pcm[1] = NULL;
        sp->rounded = 1;
        sp->data = NULL;
        free(x);
    }
    free(l);
    free(r);
}

/* A sound effect, mixed for the headphones into t and for the speaker
 * into sp (the songs' made first: their gain). */
static void make_sfx(int id, Track *t, Track *sp)
{
    const uint32_t frames = (uint32_t)(SFX_MAX * AUDIO_RATE), n = (uint32_t)(SFX_MAX * 60) * FRAME;
    uint32_t last = 0, end, i;
    int16_t *pcm;
    float *l, *r;
    synth_reset();
    audio_sfx(id);
    render(frames, &l, &r);
    /* (the sound effects are in the middle: one side is enough) */
    for (i = 0; i < frames; i++) l[i] = 0.5f * (l[i] + r[i]);
    pcm = resample(l, frames, n);
    for (i = 0; i < n; i++)
        if (abs(pcm[i]) >= SFX_SILENT) last = i;
    t->samples = (last / FRAME + 1) * FRAME;
    /* the rest of the last frame fades out (no step at the end) */
    end = t->samples;
    for (i = last + 1; i < end; i++) pcm[i] = (int16_t)(pcm[i] * (int32_t)(end - i) / (int32_t)(end - last));
    t->loop = 0;
    t->rounded = 0;
    t->pcm[0] = pcm;
    t->pcm[1] = NULL;
    t->data = NULL;
    if (sp) {
        /* as many ticks; the songs' gain, no more than its loudest sample
         * allows; the same fade at the end */
        uint32_t ns = t->samples / FRAME * S_FRAME, last_s = (last + 1) * S_FRAME / FRAME;
        float *x = speaker_base(l, NULL, frames, ns);
        double gain = s_spk_gains ? s_spk_gain_sum / s_spk_gains : 1.0, peak = 1.0;
        for (i = 0; i < ns; i++)
            if (fabs(x[i]) > peak) peak = fabs(x[i]);
        if (peak * gain > SPK_CEIL) gain = SPK_CEIL / peak;
        for (i = 0; i < ns; i++) x[i] = (float)(x[i] * gain * (i < last_s ? 1.0 : (double)(ns - i) / (ns - last_s)));
        sp->samples = ns;
        sp->loop = 0;
        sp->rounded = 1;
        sp->pcm[0] = to_int16(x, ns);
        sp->pcm[1] = NULL;
        sp->data = NULL;
        free(x);
    }
    free(l);
    free(r);
}

static void tools_init(void)
{
    static int done;
    if (done) return;
    done = 1;
    audio_init();
    fir_init(s_fir, UP, CUTOFF);
    fir_init(s_fir_s, S_UP, S_CUTOFF);
}

/* 2 if all the songs fit SONG_BUDGET with the headphones' mix in stereo,
 * else 1 */
static int song_channels(void)
{
    uint64_t total = 0;
    int i;
    for (i = 0; i < audio_song_count(); i++) total += (uint64_t)song_samples(i) * 2 + song_samples(i) / FRAME * S_FRAME;
    if (total <= SONG_BUDGET) return 2;
    fprintf(stderr, "gba_audio: the songs take %llu KB with the headphones' mix in stereo, more than the %u KB they "
                    "have: stored in mono\n",
            (unsigned long long)(total >> 10), SONG_BUDGET >> 10);
    return 1;
}

/* The mixer's soft clipping (audio_gba.c): a sum from -512 to 511 as an
 * 8-bit sample, the same to SOFT_KNEE and then bent to 127 at most (where
 * the volumes over 8 or the sound effects take it past 8 bits) */
#define SOFT_KNEE 100
static int8_t soft_clip(int v)
{
    double a = abs(v), y = a <= SOFT_KNEE ? a : SOFT_KNEE + (127.4 - SOFT_KNEE) * tanh((a - SOFT_KNEE) / (127.4 - SOFT_KNEE));
    long q = lrint(y);
    if (q > 127) q = 127;
    return (int8_t)(v < 0 ? -q : q);
}

/* ------------------------------------------------------------------ */
/* Export                                                              */
/* ------------------------------------------------------------------ */

static void c_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

int gba_audio_export(GbaGen *g)
{
    Track sfx[SFX_COUNT], ssfx[SFX_COUNT], song, ssong;
    int8_t clip[1024];
    size_t bytes = 0;
    char name[64];
    int songs, channels, i;
    tools_init();
    songs = audio_song_count();
    channels = song_channels();
    s_spk_gain_sum = 0.0;
    s_spk_gains = 0;
    fprintf(g->hdr, "\n/* songs and sound effects (gba_audio.c) */\n#include \"audio_gba.h\"\n");
    fprintf(g->hdr, "#define GBA_SONG_COUNT %d\n#define GBA_SFX_COUNT %d\n", songs, SFX_COUNT);
    fprintf(g->hdr, "extern const GbaSong gba_songs[GBA_SONG_COUNT];\nextern const GbaSfx gba_sfx[GBA_SFX_COUNT];\n");
    fprintf(g->hdr, "extern const int8_t gba_soft_clip[1024];\n");
    /* the songs (the sound effects take the speaker's mix's gain from
     * them), then the sound effects */
    fprintf(g->src, "\n/* songs and sound effects (gba_audio.c) */\nconst GbaSong gba_songs[GBA_SONG_COUNT] = {\n");
    for (i = 0; i < songs; i++) {
        make_song(i, &song, &ssong);
        store(&song, channels);
        store(&ssong, 1);
        snprintf(name, sizeof(name), "gba_song_%d", i);
        gba_gen_blob(g, name, "int8_t", song.data, (size_t)song.samples * channels);
        snprintf(name, sizeof(name), "gba_song_%d_spk", i);
        gba_gen_blob(g, name, "int8_t", ssong.data, ssong.samples);
        bytes += (size_t)song.samples * channels + ssong.samples;
        fprintf(g->src, "    {");
        c_string(g->src, audio_song_name(i));
        fprintf(g->src, ", {{gba_song_%d, %u, %uu, %d}, {gba_song_%d_spk, %u, %uu, 1}}, %#.9gf, %#.9gf},\n", i,
                song.samples, song.loop, channels, i, ssong.samples, ssong.loop, audio_song_bpm(i),
                audio_song_length(i));
        track_free(&song);
        track_free(&ssong);
    }
    fprintf(g->src, "};\n");
    for (i = 0; i < SFX_COUNT; i++) {
        make_sfx(i, &sfx[i], &ssfx[i]);
        store(&sfx[i], 1);
        store(&ssfx[i], 1);
        snprintf(name, sizeof(name), "gba_sfx_%d", i);
        gba_gen_blob(g, name, "int8_t", sfx[i].data, sfx[i].samples);
        snprintf(name, sizeof(name), "gba_sfx_%d_spk", i);
        gba_gen_blob(g, name, "int8_t", ssfx[i].data, ssfx[i].samples);
        bytes += sfx[i].samples + ssfx[i].samples;
    }
    fprintf(g->src, "const GbaSfx gba_sfx[GBA_SFX_COUNT] = {\n");
    for (i = 0; i < SFX_COUNT; i++)
        fprintf(g->src, "    {{{gba_sfx_%d, %u, %d}, {gba_sfx_%d_spk, %u, %d}}},\n", i, sfx[i].samples, sfx[i].shift, i,
                ssfx[i].samples, ssfx[i].shift);
    fprintf(g->src, "};\n");
    for (i = 0; i < SFX_COUNT; i++) {
        track_free(&sfx[i]);
        track_free(&ssfx[i]);
    }
    for (i = 0; i < 1024; i++) clip[i] = soft_clip(i - 512);
    gba_gen_blob(g, "gba_soft_clip", "int8_t", clip, sizeof(clip));
    printf("gba_audio: %d songs (%s, and the speaker's mix) and %d sounds, %zu KB of 8-bit samples\n", songs,
           channels == 2 ? "stereo" : "mono", SFX_COUNT, bytes / 1024);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Check                                                               */
/* ------------------------------------------------------------------ */

/* The SNR (dB) of the stored samples against the reference, the peak, and
 * the samples the GBA clips (softly) at volume 10 (alone). */
static double check_track(const Track *t, int *peak, uint32_t *clipped)
{
    double sig = 0.0, err = 0.0;
    uint32_t i;
    int c, nc = t->pcm[1] ? 2 : 1;
    *peak = 0;
    *clipped = 0;
    for (c = 0; c < nc; c++)
        for (i = 0; i < t->samples; i++) {
            int32_t ref = t->pcm[c][i];
            int32_t got = t->data[(size_t)i * t->channels + (t->channels == 2 ? c : 0)] * 256 >> t->shift;
            int32_t at10;
            if (t->channels == 1 && nc == 2) ref = ((int32_t)t->pcm[0][i] + t->pcm[1][i]) / 2;
            sig += (double)ref * ref;
            err += (double)(ref - got) * (ref - got);
            if (abs(ref) > *peak) *peak = abs(ref);
            at10 = (t->data[(size_t)i * t->channels + (t->channels == 2 ? c : 0)] >> t->shift) * 100 >> 6;
            if (at10 > SOFT_KNEE || at10 < -SOFT_KNEE) ++*clipped;
        }
    return err > 0.0 ? 10.0 * log10(sig / err) : 99.0;
}

static double rms_db(const Track *t)
{
    double sum = 0.0;
    uint32_t i;
    for (i = 0; i < t->samples; i++) sum += (double)t->pcm[0][i] * t->pcm[0][i];
    return sum > 0.0 ? 10.0 * log10(sum / t->samples / (32768.0 * 32768.0)) : -99.0;
}

static int check_line(const char *name, const char *mix, Track *t, int rate, double bar, double *worst)
{
    int peak;
    uint32_t clipped;
    double snr = check_track(t, &peak, &clipped);
    printf("%-16s %-4s %7.2f %6zu %8.1f %7.1f %7d %8u\n", name, mix, (double)t->samples / rate,
           (size_t)t->samples * t->channels / 1024, snr, rms_db(t), peak, clipped);
    if (snr < *worst) *worst = snr;
    return snr < bar;
}

int gba_audio_check(void)
{
    clock_t t0 = clock();
    size_t bytes = 0;
    double worst_song = 99.0, worst_sfx = 99.0, worst_spk = 99.0, worst_spk_sfx = 99.0;
    int songs, channels, i, fail = 0;
    tools_init();
    songs = audio_song_count();
    channels = song_channels();
    s_spk_gain_sum = 0.0;
    s_spk_gains = 0;
    printf("%-16s %-4s %7s %6s %8s %7s %7s %8s\n", "", "mix", "seconds", "KB", "SNR dB", "RMS dB", "peak", "knee@10");
    /* (the songs first: the sound effects' speaker gain is theirs) */
    for (i = 0; i < songs + SFX_COUNT; i++) {
        int sfx = i >= songs;
        const char *name = sfx ? SFX_NAMES[i - songs] : audio_song_name(i);
        Track t, s;
        if (sfx) make_sfx(i - songs, &t, &s);
        else make_song(i, &t, &s);
        store(&t, sfx ? 1 : channels);
        store(&s, 1);
        fail |= check_line(name, "hp", &t, RATE, sfx ? MIN_SNR_SFX : MIN_SNR_SONG, sfx ? &worst_sfx : &worst_song);
        fail |= check_line(name, "spk", &s, S_RATE, sfx ? MIN_SNR_SFX : MIN_SNR_SPK, sfx ? &worst_spk_sfx : &worst_spk);
        bytes += (size_t)t.samples * t.channels + s.samples;
        track_free(&t);
        track_free(&s);
    }
    printf("total %zu KB (songs in %s and the speaker's mix); worst SNR: songs %.1f dB (bar %.0f), sounds %.1f "
           "(bar %.0f), the speaker's songs %.1f (bar %.0f), sounds %.1f; the speaker's gain %.1f dB; %.1f s\n",
           bytes / 1024, channels == 2 ? "stereo" : "mono", worst_song, MIN_SNR_SONG, worst_sfx, MIN_SNR_SFX,
           worst_spk, MIN_SNR_SPK, worst_spk_sfx, 20.0 * log10(s_spk_gain_sum / (s_spk_gains ? s_spk_gains : 1)),
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    printf("%s\n", fail ? "AUDIO CHECK FAILED" : "audio check passed");
    return fail;
}
