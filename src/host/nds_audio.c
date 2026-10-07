/*
 * The Nintendo DS ROM's songs and sound effects (nds_tool audio, audiocheck).
 *
 * The DS's ARM9 has no floating point unit and can't run the core's
 * synthesizer, so, as for the Game Boy Advance (gba_audio.c), each song
 * and sound effect is played by the synthesizer here (src/core/audio.c,
 * 48 kHz stereo, at volume 8, the default) and the ROM carries the
 * recording, resampled to the DS's own rate, 16756991 / 512 = 32728.5 Hz
 * (a sound channel's timer at 512), as the DS plays the game: a tick a
 * frame, 59.83 frames a second, so 0.29% slower than 60 ticks (a 20th of
 * a semitone lower), 547.06 samples a tick. Twice: as the synth mixes it for headphones, and as it mixes it
 * for a handheld's own small speakers (audio_set_output: the bass they
 * can't play cut, compressed, louder), the options' OUTPUT.
 *
 * Songs (music_hp.bin, music_spk.bin in the ROM's file system, NitroFS)
 * are read as they play (src/nds/audio_nds.c), in blocks of 1024 samples
 * a side, so playing can start at any block. Their samples are 8 bits
 * in groups of 32 that share a shift (block floating point: each group as
 * loud as its loudest sample allows): 43 dB or more over the error, where
 * IMA-ADPCM's 4 bits gave 27 on average and 18 in the songs' busiest
 * parts, and decoding is a shift (the ARM9 decodes them into the buffers
 * the sound hardware plays). They loop as the sequencer
 * loops them, back to their loop bar (S) at the end of the arrangement
 * (L). The menu, practice and metronome loops keep [0, 2L - S): the first
 * pass and a second pass of the loop, so the jump back by L - S at the
 * end lands where the loop sounds as it does after itself (its echoes and
 * releases). The levels' songs, which loop only while the results are
 * shown, keep [0, L + 2 s): the jump back by L - S comes 2 seconds into
 * the loop, when the end's tails have rung out. The DS crossfades each
 * jump over XFADE samples, recorded past the jump's point.
 *
 * Sound effects (sfx.bin) are mono, 8-bit samples the sound hardware
 * plays from memory (its 4-bit ADPCM lost the attacks: 17 dB), stored as
 * loud as the synth plays them at volume 10 (the DS turns them down to
 * the volume set: its channels can only make them quieter), rounded with
 * the rounding error of the one before taken off (first-order noise
 * shaping, as on the GBA).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/audio.h"

/* samples a second of song time: a channel plays 16756991 / 512 = 32728.5
 * a second, and the game ticks once a frame, 59.83 times a second (its
 * 1/60 s tick takes 1/59.83): 547.06 samples a tick, 560190 / 1024 */
#define RATE (560190.0 * 60.0 / 1024.0)
#define BLOCK 1024                /* samples a side in a block of a song */
#define GROUP 32                  /* samples that share a shift */
#define BLOCK_BYTES (2 * (BLOCK / GROUP) * (1 + GROUP))
#define XFADE 256                 /* samples a loop's jump is crossfaded over */
#define LEVEL_TAIL 2.0            /* seconds kept after a level song's arrangement */
#define SFX_MAX 3.0               /* seconds, at most, of a sound effect */
#define SFX_SILENT 64             /* a sound effect ends where it stays under this */
#define TAPS_HALF 32              /* the resampler: 64 taps at 48 kHz */
#define PHASES 512                /* its filter at this many fractions of a sample */
#define KAISER_BETA 8.0
#define CUTOFF 15000.0            /* Hz; stops by RATE's half */
/* The bar (SNR of the stored samples against the resampled render, the
 * sound effects' shaped noise counted in full): the songs get 43 to 47 dB,
 * the sound effects 21 to 33 (the quiet ones least); less means something
 * is broken. */
#define MIN_SNR_SONG 38.0
#define MIN_SNR_SFX 20.0

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "nds_audio: out of memory\n");
        exit(1);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Rendering (as gba_audio.c)                                          */
/* ------------------------------------------------------------------ */

/* A silent synthesizer at volume 8 with nothing queued, mixing for
 * headphones or the speakers. */
static void synth_reset(int speaker)
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
    audio_set_output(speaker);
    /* (the output's change goes through the synth's queue) */
    audio_mix(buf, 16);
}

/* `frames` frames (48 kHz) of what is playing: left and right. */
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
/* Resampling: 48 kHz -> 32728.5 Hz, windowed sinc                     */
/* ------------------------------------------------------------------ */

static float s_fir[PHASES + 1][2 * TAPS_HALF];

static double bessel_i0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; k++) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

static void fir_init(void)
{
    const double fc = CUTOFF / AUDIO_RATE;
    for (int ph = 0; ph <= PHASES; ph++) {
        double sum = 0.0, h[2 * TAPS_HALF];
        /* tap j is input sample floor(t) - TAPS_HALF + 1 + j, at t's
         * fraction ph / PHASES */
        for (int j = 0; j < 2 * TAPS_HALF; j++) {
            double x = (double)(j - TAPS_HALF + 1) - (double)ph / PHASES;
            double u = x / TAPS_HALF, w = 0.0;
            double s = x == 0.0 ? 1.0 : sin(2.0 * M_PI * fc * x) / (2.0 * M_PI * fc * x);
            if (u > -1.0 && u < 1.0) w = bessel_i0(KAISER_BETA * sqrt(1.0 - u * u)) / bessel_i0(KAISER_BETA);
            h[j] = s * w;
            sum += h[j];
        }
        for (int j = 0; j < 2 * TAPS_HALF; j++) s_fir[ph][j] = (float)(h[j] / sum);
    }
}

/* n samples at RATE from `in` (48 kHz, nin frames; zeros past its ends),
 * times gain, as floats */
static float *resample(const float *in, uint32_t nin, uint32_t n, double gain)
{
    float *out = xmalloc(n * sizeof(float));
    for (uint32_t i = 0; i < n; i++) {
        double t = (double)i * AUDIO_RATE / RATE;
        int64_t base = (int64_t)floor(t);
        double fr = (t - (double)base) * PHASES;
        int ph = (int)fr;
        float k = (float)(fr - ph);
        const float *h0 = s_fir[ph], *h1 = s_fir[ph + 1];
        float acc = 0.0f;
        base -= TAPS_HALF - 1;
        for (int j = 0; j < 2 * TAPS_HALF; j++) {
            int64_t m = base + j;
            if (m >= 0 && m < (int64_t)nin) acc += in[m] * (h0[j] + (h1[j] - h0[j]) * k);
        }
        out[i] = (float)(acc * gain);
    }
    return out;
}

/* ------------------------------------------------------------------ */
/* Block floating point                                                */
/* ------------------------------------------------------------------ */

/* A group of GROUP samples (x, n of them real, zeros after) into p: the
 * shift, then the samples, each round(x / 2^shift) in 8 bits, with the
 * least shift that holds the loudest. Adds the signal and the error. */
static void bfp_group(const float *x, int n, uint8_t *p, double *sig, double *err)
{
    float peak = 0.0f;
    for (int i = 0; i < n; i++)
        if (fabsf(x[i]) > peak) peak = fabsf(x[i]);
    int sh = 0;
    while (sh < 8 && (lrintf(peak / (float)(1 << sh)) > 127)) sh++;
    p[0] = (uint8_t)sh;
    for (int i = 0; i < GROUP; i++) {
        float v = i < n ? x[i] : 0.0f;
        long q = lrintf(v / (float)(1 << sh));
        q = q > 127 ? 127 : q < -128 ? -128 : q;
        p[1 + i] = (uint8_t)(int8_t)q;
        if (i < n) {
            double d = (double)(q * (1 << sh)) - v;
            *sig += (double)v * v;
            *err += d * d;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Songs and sounds                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    double snr_sum;
    int snr_n;
    double snr_min;
} Stats;

static void snr_add(Stats *st, double sig, double err)
{
    double snr = 10.0 * log10((sig + 1e-9) / (err + 1e-9));
    st->snr_sum += snr;
    st->snr_n++;
    if (st->snr_n == 1 || snr < st->snr_min) st->snr_min = snr;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

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

/* A song in blocks: appends to *out (size *n), returns its table entry's
 * figures. */
typedef struct {
    uint32_t offset, blocks, samples, loop_end, loop_to;
} SongEntry;

static SongEntry encode_song(int song, int speaker, uint8_t **out, size_t *n, Stats *st)
{
    double len, start;
    song_loop(song, &len, &start);
    SongEntry e;
    double kept = song < SONG_FIRST_LEVEL ? 2.0 * len - start : len + LEVEL_TAIL;
    e.loop_end = (uint32_t)llround(kept * RATE);
    e.loop_to = e.loop_end - (uint32_t)llround((len - start) * RATE);
    e.samples = e.loop_end + XFADE;
    e.blocks = (e.samples + BLOCK - 1) / BLOCK;
    uint32_t padded = e.blocks * BLOCK;
    uint32_t frames = (uint32_t)ceil(padded / RATE * AUDIO_RATE) + TAPS_HALF + 1;
    float *l, *r;
    synth_reset(speaker);
    audio_play_song(song, 0.0f);
    render(frames, &l, &r);
    audio_stop_song();
    float *side[2] = {resample(l, frames, e.samples, 1.0), resample(r, frames, e.samples, 1.0)};
    free(l);
    free(r);
    *out = realloc(*out, *n + (size_t)e.blocks * BLOCK_BYTES);
    e.offset = (uint32_t)*n;
    double sig = 0.0, err = 0.0;
    for (uint32_t b = 0; b < e.blocks; b++) {
        uint8_t *blk = *out + *n + (size_t)b * BLOCK_BYTES;
        for (int c = 0; c < 2; c++)
            for (int g = 0; g < BLOCK / GROUP; g++) {
                uint32_t k = b * BLOCK + (uint32_t)g * GROUP;
                int real = k >= e.samples ? 0 : (int)(e.samples - k < GROUP ? e.samples - k : GROUP);
                bfp_group(side[c] + k, real, blk + (c * (BLOCK / GROUP) + g) * (1 + GROUP), &sig, &err);
            }
    }
    snr_add(st, sig, err);
    *n += (size_t)e.blocks * BLOCK_BYTES;
    free(side[0]);
    free(side[1]);
    return e;
}

/* music_hp.bin / music_spk.bin:
 *   "PDMU", version 1, rate * 1000, samples a block, songs
 *   per song, 64 bytes: offset, blocks, samples, loop_end, loop_to (the
 *     jump: from loop_end to loop_to, crossfaded over the XFADE samples
 *     after each), bpm and length (floats), its name (36 bytes, 0-ended)
 *   the blocks: per block, left then right, each 32 groups of a shift
 *     (a byte) and 32 samples (int8): a sample is its byte << shift */
static int write_music(const char *path, int speaker, Stats *st)
{
    int count = audio_song_count();
    size_t head = 20 + (size_t)count * 64, n = head;
    uint8_t *out = calloc(head, 1);
    memcpy(out, "PDMU", 4);
    put32(out + 4, 1);
    put32(out + 8, (uint32_t)llround(RATE * 1000.0));
    put32(out + 12, BLOCK);
    put32(out + 16, (uint32_t)count);
    for (int song = 0; song < count; song++) {
        SongEntry e = encode_song(song, speaker, &out, &n, st);
        uint8_t *t = out + 20 + song * 64;
        float bpm = audio_song_bpm(song), length = audio_song_length(song);
        put32(t, e.offset);
        put32(t + 4, e.blocks);
        put32(t + 8, e.samples);
        put32(t + 12, e.loop_end);
        put32(t + 16, e.loop_to);
        memcpy(t + 20, &bpm, 4);
        memcpy(t + 24, &length, 4);
        snprintf((char *)t + 28, 36, "%s", audio_song_name(song));
    }
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(out, 1, n, f) != n) {
        fprintf(stderr, "nds_audio: can't write %s\n", path);
        if (f) fclose(f);
        free(out);
        return 1;
    }
    fclose(f);
    printf("%s: %d songs, %.1f MB\n", path, count, n / 1048576.0);
    free(out);
    return 0;
}

/* sfx.bin:
 *   "PDSX", version 1, rate * 1000, sound effects
 *   per sound effect and mix (headphones, speakers): offset, bytes (a
 *     multiple of 4), samples (int8, the same number)
 *   the data */
static int write_sfx(const char *path, Stats *st)
{
    size_t head = 16 + SFX_COUNT * 2 * 12, n = head;
    uint8_t *out = calloc(head, 1);
    memcpy(out, "PDSX", 4);
    put32(out + 4, 1);
    put32(out + 8, (uint32_t)llround(RATE * 1000.0));
    put32(out + 12, SFX_COUNT);
    for (int id = 0; id < SFX_COUNT; id++) {
        for (int mix = 0; mix < 2; mix++) {
            const uint32_t frames = (uint32_t)(SFX_MAX * AUDIO_RATE), max = (uint32_t)(SFX_MAX * RATE);
            float *l, *r;
            synth_reset(mix);
            audio_sfx(id);
            render(frames, &l, &r);
            for (uint32_t i = 0; i < frames; i++) l[i] = 0.5f * (l[i] + r[i]);
            /* as loud as at volume 10 (the synth's curve: (v / 10)^2) */
            float *x = resample(l, frames, max, 100.0 / 64.0);
            free(l);
            free(r);
            uint32_t last = 0;
            for (uint32_t i = 0; i < max; i++)
                if (fabsf(x[i]) >= SFX_SILENT) last = i;
            /* a whole number of words; the last samples fade out */
            uint32_t ns = (last + 1 + 64 + 3) / 4 * 4;
            if (ns > max) ns = max / 4 * 4;
            for (uint32_t i = last + 1; i < ns; i++) x[i] *= (float)(ns - i) / (float)(ns - last);
            uint32_t bytes = ns;
            out = realloc(out, n + bytes);
            uint8_t *p = out + n;
            double sig = 0.0, err = 0.0, carry = 0.0;
            for (uint32_t i = 0; i < ns; i++) {
                double v = x[i] > 32767.0f ? 32767.0 : x[i] < -32768.0f ? -32768.0 : x[i];
                long q = lrint((v - carry) / 256.0);
                q = q > 127 ? 127 : q < -128 ? -128 : q;
                p[i] = (uint8_t)(int8_t)q;
                carry = q * 256.0 - (v - carry);
                sig += v * v;
                err += (q * 256.0 - v) * (q * 256.0 - v);
            }
            snr_add(st, sig, err);
            uint8_t *t = out + 16 + (id * 2 + mix) * 12;
            put32(t, (uint32_t)n);
            put32(t + 4, bytes);
            put32(t + 8, ns);
            n += bytes;
            free(x);
        }
    }
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(out, 1, n, f) != n) {
        fprintf(stderr, "nds_audio: can't write %s\n", path);
        if (f) fclose(f);
        free(out);
        return 1;
    }
    fclose(f);
    printf("%s: %d sound effects, %.0f KB\n", path, SFX_COUNT, n / 1024.0);
    free(out);
    return 0;
}

/* The songs' names, tempos and lengths, compiled into the ROM (the game
 * needs them without the file system too): gen_nds.h and gen_nds.c in
 * gen_dir. */
static int write_gen(const char *gen_dir)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/gen_nds.h", gen_dir);
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "/* made by nds_tool (src/host/nds_audio.c) */\n#ifndef PD_GEN_NDS_H\n#define PD_GEN_NDS_H\n\n");
    fprintf(f, "#define NDS_SONG_COUNT %d\n\ntypedef struct {\n    const char *name;\n    float bpm, length;\n} NdsSong;\n\n",
            audio_song_count());
    fprintf(f, "extern const NdsSong g_nds_songs[NDS_SONG_COUNT];\n\n#endif\n");
    fclose(f);
    snprintf(path, sizeof(path), "%s/gen_nds.c", gen_dir);
    f = fopen(path, "w");
    if (!f) return 1;
    fprintf(f, "/* made by nds_tool (src/host/nds_audio.c) */\n#include \"gen_nds.h\"\n\nconst NdsSong g_nds_songs[NDS_SONG_COUNT] = {\n");
    for (int i = 0; i < audio_song_count(); i++)
        fprintf(f, "    {\"%s\", (float)%.9g, (float)%.9g},\n", audio_song_name(i), (double)audio_song_bpm(i), (double)audio_song_length(i));
    fprintf(f, "};\n");
    fclose(f);
    return 0;
}

/* Records everything into dir (the ROM's NitroFS folder), and the songs'
 * table into gen_dir. Fails if the encoding is off (the SNRs under their
 * bars). */
int nds_audio_export(const char *dir, const char *gen_dir)
{
    if (write_gen(gen_dir)) {
        fprintf(stderr, "nds_audio: can't write into %s\n", gen_dir);
        return 1;
    }
    char path[1024];
    Stats songs = {0, 0, 0}, sfx = {0, 0, 0};
    fir_init();
    snprintf(path, sizeof(path), "%s/music_hp.bin", dir);
    if (write_music(path, 0, &songs)) return 1;
    snprintf(path, sizeof(path), "%s/music_spk.bin", dir);
    if (write_music(path, 1, &songs)) return 1;
    snprintf(path, sizeof(path), "%s/sfx.bin", dir);
    if (write_sfx(path, &sfx)) return 1;
    printf("stored against the recording: songs %.1f dB (%.1f at worst), sound effects %.1f dB (%.1f at worst)\n",
           songs.snr_sum / songs.snr_n, songs.snr_min, sfx.snr_sum / sfx.snr_n, sfx.snr_min);
    if (songs.snr_min < MIN_SNR_SONG || sfx.snr_min < MIN_SNR_SFX) {
        fprintf(stderr, "nds_audio: the encoding is worse than it should be\n");
        return 1;
    }
    return 0;
}
