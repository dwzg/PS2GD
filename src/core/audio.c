/*
 * Pulse Dash synthesizer.
 *
 * Voices are rendered in blocks of CTRL samples: envelopes, filter
 * coefficients and pitch are computed once per block and interpolated, so
 * the per-sample work is just oscillators and one-pole filters. Only float
 * math is used in the render path (the PS2's EE has no hardware doubles).
 */
#include "audio.h"
#include "songdata.h"

#include <stdlib.h>

#define SR AUDIO_RATE
#define SRF ((float)AUDIO_RATE)
#define CTRL 32
#define MAX_VOICES 40
#define DLY_LEN 32768
#define SINE_N 1024
#define MAX_SONGS 16
#define MAX_PENDING_SFX 48

/* ------------------------------------------------------------------ */
/* Instruments                                                         */
/* ------------------------------------------------------------------ */

enum { K_OSC = 0, K_KICK, K_SNARE, K_CLAP, K_HAT, K_NOISE };
enum { W_SAW = 0, W_PULSE, W_TRI, W_SINE, W_SUPER, W_FM };

typedef struct {
    uint8_t kind, wave, mono, poly;
    float pw;
    float a, d, s, r;
    float vol;
    float cut, cut_env, cut_decay;
    float detune, vib, duck, send, pan, glide, sub;
} InstDef;

static const InstDef INST[INS_COUNT] = {
    /*                kind     wave    mono poly pw    a      d     s     r     vol   cut    cenv   cdec  det   vib   duck  send  pan    glide  sub */
    [INS_KICK] =     {K_KICK,  W_SINE,  1, 1, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.95f, 0,     0,     0,    0,    0,    0,    0,    0,     0,     0},
    [INS_SNARE] =    {K_SNARE, W_SINE,  1, 1, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.55f, 0,     0,     0,    0,    0,    0,    0.08f, 0.05f, 0,    0},
    [INS_CLAP] =     {K_CLAP,  W_SINE,  1, 1, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.50f, 0,     0,     0,    0,    0,    0,    0.12f, -0.05f, 0,   0},
    [INS_HAT] =      {K_HAT,   W_SINE,  1, 1, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.22f, 0,     0,     0,    0,    0,    0.2f, 0,    0.25f, 0,     0},
    [INS_BASS_SAW] = {K_OSC,   W_SAW,   1, 1, 0.5f, 0.003f, 0.25f, 0.6f, 0.08f, 0.50f, 260,  1900,  9.0f, 0,    0,    0.65f, 0,   0,     0,     0.45f},
    [INS_BASS_SQUARE]={K_OSC,  W_PULSE, 1, 1, 0.5f, 0.002f, 0.20f, 0.5f, 0.06f, 0.40f, 700,  1600,  10.f, 0,    0,    0.55f, 0,   0,     0,     0.35f},
    [INS_BASS_SUB] = {K_OSC,   W_TRI,   1, 1, 0.5f, 0.004f, 0.30f, 0.8f, 0.10f, 0.60f, 1200, 0,     0,    0,    0,    0.4f, 0,    0,     0,     0.5f},
    [INS_PAD_SUPER]= {K_OSC,   W_SUPER, 0, 4, 0.5f, 0.06f, 0.50f, 0.75f, 0.35f, 0.15f, 2400, 600,   2.0f, 0.13f, 0,   0.75f, 0.12f, 0,   0,     0},
    [INS_PAD_SOFT] = {K_OSC,   W_TRI,   0, 4, 0.5f, 0.22f, 0.80f, 0.80f, 0.70f, 0.20f, 2600, 0,     0,    0,    0.08f, 0.3f, 0.2f, 0,    0,     0},
    [INS_PLUCK] =    {K_OSC,   W_PULSE, 1, 1, 0.25f, 0.001f, 0.15f, 0.0f, 0.06f, 0.20f, 4200, 3500,  14.f, 0,   0,    0.35f, 0.35f, -0.2f, 0,    0},
    [INS_PLUCK_SAW]= {K_OSC,   W_SAW,   1, 1, 0.5f, 0.001f, 0.18f, 0.0f, 0.06f, 0.18f, 1800, 6000,  13.f, 0,   0,    0.35f, 0.32f, 0.2f, 0,     0},
    [INS_BELL] =     {K_OSC,   W_FM,    0, 3, 0.5f, 0.001f, 0.70f, 0.0f, 0.40f, 0.20f, 9000, 0,     0,    0,    0,    0.2f, 0.35f, 0.15f, 0,    0},
    [INS_LEAD_SUPER]={K_OSC,   W_SUPER, 1, 1, 0.5f, 0.008f, 0.35f, 0.8f, 0.12f, 0.24f, 5200, 2500,  4.0f, 0.11f, 0.17f, 0.3f, 0.30f, 0,  0.035f, 0},
    [INS_LEAD_SQUARE]={K_OSC,  W_PULSE, 1, 1, 0.5f, 0.004f, 0.25f, 0.7f, 0.08f, 0.20f, 6500, 1500,  6.0f, 0,   0.2f, 0.25f, 0.28f, 0,   0.03f, 0},
    [INS_LEAD_PULSE]={K_OSC,   W_PULSE, 1, 1, 0.18f, 0.003f, 0.20f, 0.65f, 0.08f, 0.19f, 7000, 1000, 6.0f, 0,   0.15f, 0.25f, 0.25f, 0,  0.025f, 0},
    [INS_CHIP_TRI] = {K_OSC,   W_TRI,   1, 1, 0.5f, 0.002f, 0.10f, 0.8f, 0.05f, 0.32f, 9000, 0,     0,    0,    0.1f, 0.2f, 0.15f, 0,    0.02f, 0},
    [INS_SFX_BLIP] = {K_OSC,   W_PULSE, 0, 4, 0.5f, 0.001f, 0.07f, 0.0f, 0.03f, 0.28f, 9000, 0,     0,    0,    0,    0,    0.1f, 0,    0,     0},
    [INS_SFX_NOISE]= {K_NOISE, W_SINE,  0, 2, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.55f, 0,     0,     0,    0,    0,    0,    0.1f, 0,    0,     0},
    [INS_SFX_THUMP]= {K_KICK,  W_SINE,  0, 2, 0.5f, 0.0f,  0.0f, 0.0f, 0.0f, 0.70f, 0,     0,     0,    0,    0,    0,    0,    0,    0,     0},
};

/* ------------------------------------------------------------------ */
/* Compiled songs                                                      */
/* ------------------------------------------------------------------ */

typedef AudioNote SeqEvent;

typedef struct {
    SeqEvent *ev;
    int nev;
    uint32_t len_steps;
    uint32_t loop_step;
    float bpm;
} CompiledSong;

static CompiledSong s_cs[MAX_SONGS];
static int s_ncs;

/* ------------------------------------------------------------------ */
/* Voices                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t on, gate, inst, open;
    int8_t track; /* -1 = sound effect */
    float freq, target_freq;
    float ph[3];
    float vel, gain;
    float env;
    int stage;
    float t;
    float lp1, lp2, nlp, nlp2;
    uint32_t rng;
    int off_count;
    int note;
} Voice;

static Voice s_v[MAX_VOICES];
static float s_sine[SINE_N + 2];
static float s_dl[DLY_LEN], s_dr[DLY_LEN];
static int s_dpos;
/* Mix buses for one control block: left, right, centre (both sides) and the
 * echo send (mono). s_blk holds the voice being rendered. */
static float s_bufL[CTRL], s_bufR[CTRL], s_bufM[CTRL], s_send[CTRL];
static float s_blk[CTRL];

/* playback state (audio thread) */
static int s_song = -1;
static uint32_t s_gen;
static int s_paused;
static float s_pos;        /* samples into the arrangement */
static float s_elapsed;    /* samples since the song started (not wrapped) */
static int s_seq_idx;
static float s_step_samples;
static float s_sc_t = 10.0f; /* time since last kick (sidechain) */
static float s_music_gain = 0.64f, s_sfx_gain = 0.64f;
static float s_latency;    /* seconds between mixing and hearing (frontend) */
static float s_user_delay; /* the player's extra delay setting */
static float s_track_vol[SONG_TRACKS];
static int s_track_inst[SONG_TRACKS];
static int s_delay_samples = 12000;

/* published to the game thread */
static volatile uint32_t s_pub_gen;
static volatile float s_pub_elapsed;

/* game-thread mirrors */
static int m_song = -1;
static uint32_t m_gen;

/* ------------------------------------------------------------------ */
/* Command queue (game thread -> audio thread)                         */
/* ------------------------------------------------------------------ */

enum { CMD_PLAY = 1, CMD_STOP, CMD_SFX, CMD_VOL };

typedef struct {
    int type, a, b;
    float f;
    uint32_t gen;
} Cmd;

#define QN 64
static Cmd s_q[QN];
static volatile uint32_t s_qhead, s_qtail;

static void mem_barrier(void)
{
#if defined(__GNUC__)
    __sync_synchronize();
#endif
}

static void push_cmd(Cmd c)
{
    uint32_t h = s_qhead;
    if (h - s_qtail >= QN) return; /* queue full: drop */
    s_q[h % QN] = c;
    mem_barrier();
    s_qhead = h + 1;
}

/* ------------------------------------------------------------------ */
/* Pending sound effect notes                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    float when; /* samples from now */
    uint8_t inst, note, vel;
    float len;  /* seconds */
} SfxNote;

static SfxNote s_sfx[MAX_PENDING_SFX];
static int s_nsfx;

typedef struct {
    float at; /* seconds */
    uint8_t inst, note, vel;
    float len;
} SfxStep;

#define SFX_END {-1.0f, 0, 0, 0, 0}
static const SfxStep SFX_DEFS[SFX_COUNT][8] = {
    [SFX_DEATH] = {{0.0f, INS_SFX_NOISE, 60, 255, 0.6f}, {0.0f, INS_SFX_THUMP, 36, 255, 0.4f}, SFX_END},
    [SFX_COIN] = {{0.0f, INS_SFX_BLIP, 83, 200, 0.06f}, {0.07f, INS_SFX_BLIP, 88, 220, 0.25f}, SFX_END},
    [SFX_CHECKPOINT] = {{0.0f, INS_SFX_BLIP, 72, 170, 0.05f}, {0.05f, INS_SFX_BLIP, 79, 190, 0.12f}, SFX_END},
    [SFX_MENU_MOVE] = {{0.0f, INS_SFX_BLIP, 81, 120, 0.03f}, SFX_END},
    [SFX_MENU_SELECT] = {{0.0f, INS_SFX_BLIP, 76, 170, 0.04f}, {0.05f, INS_SFX_BLIP, 83, 190, 0.09f}, SFX_END},
    [SFX_MENU_BACK] = {{0.0f, INS_SFX_BLIP, 79, 160, 0.04f}, {0.05f, INS_SFX_BLIP, 72, 150, 0.08f}, SFX_END},
    [SFX_COMPLETE] = {{0.0f, INS_SFX_BLIP, 72, 200, 0.1f}, {0.1f, INS_SFX_BLIP, 76, 200, 0.1f},
                      {0.2f, INS_SFX_BLIP, 79, 210, 0.1f}, {0.3f, INS_SFX_BLIP, 84, 230, 0.5f},
                      {0.3f, INS_SFX_BLIP, 88, 160, 0.5f}, {0.3f, INS_SFX_THUMP, 36, 180, 0.3f}, SFX_END},
    [SFX_START] = {{0.0f, INS_SFX_BLIP, 72, 180, 0.05f}, {0.05f, INS_SFX_BLIP, 79, 190, 0.05f},
                   {0.1f, INS_SFX_BLIP, 84, 210, 0.15f}, SFX_END},
};

/* ------------------------------------------------------------------ */
/* Song compiler                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    SeqEvent *ev;
    int n, cap;
} EvBuf;

static void evbuf_push(EvBuf *b, SeqEvent e)
{
    if (b->n == b->cap) {
        int nc = b->cap ? b->cap * 2 : 512;
        SeqEvent *ne = (SeqEvent *)realloc(b->ev, (size_t)nc * sizeof(SeqEvent));
        if (!ne) return;
        b->ev = ne;
        b->cap = nc;
    }
    b->ev[b->n++] = e;
}

static int parse_note(const char **pp)
{
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7}; /* A B C D E F G */
    const char *p = *pp;
    if (*p < 'A' || *p > 'G') return -1;
    int n = base[*p - 'A'];
    p++;
    if (*p == '#') { n++; p++; }
    else if (*p == 'b') { n--; p++; }
    if (*p < '0' || *p > '9') return -1;
    int oct = *p - '0';
    p++;
    *pp = p;
    return n + 12 * (oct + 1);
}

static int parse_len(const char **pp)
{
    const char *p = *pp;
    if (*p != ':') return 1;
    p++;
    int v = 0;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    *pp = p;
    return v > 0 ? v : 1;
}

static int drum_vel(int track, char c)
{
    (void)track;
    switch (c) {
    case 'X': return 255;
    case 'r': return 110;
    case 'o': return 200;
    case 'c': return 210;
    default: return 200;
    }
}

/* Emit a pattern at `start` (steps). Returns its length in steps. */
static int compile_pattern(EvBuf *b, int track, const char *data, uint32_t start, int transpose)
{
    const char *p = data;
    uint32_t pos = start;
    if (track <= TR_HAT) {
        for (; *p; p++) {
            if (*p == ' ' || *p == '\t' || *p == '|') continue;
            if (*p != '.') {
                SeqEvent e = {pos, 1, (uint8_t)track, (uint8_t)*p, (uint8_t)drum_vel(track, *p)};
                evbuf_push(b, e);
            }
            pos++;
        }
        return (int)(pos - start);
    }
    int last_first = -1, last_count = 0; /* events of the previous note token */
    uint32_t last_end = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '|') p++;
        if (!*p) break;
        if (*p == '.') {
            p++;
            if (last_first >= 0 && last_end == pos) {
                for (int k = 0; k < last_count; k++) b->ev[last_first + k].len++;
                last_end++;
            }
            pos++;
            continue;
        }
        if (*p == '-') {
            p++;
            pos += (uint32_t)parse_len(&p);
            last_first = -1;
            continue;
        }
        int notes[6], nn = 0;
        for (;;) {
            int n = parse_note(&p);
            if (n < 0) break;
            if (nn < 6) notes[nn++] = n;
            if (*p == '+') { p++; continue; }
            break;
        }
        if (nn == 0) { /* unknown token: skip it */
            while (*p && *p != ' ' && *p != '\t') p++;
            continue;
        }
        int len = parse_len(&p);
        last_first = b->n;
        last_count = nn;
        for (int k = 0; k < nn; k++) {
            int note = clampi(notes[k] + transpose, 0, 127);
            SeqEvent e = {pos, (uint16_t)len, (uint8_t)track, (uint8_t)note, 200};
            evbuf_push(b, e);
        }
        pos += (uint32_t)len;
        last_end = pos;
    }
    return (int)(pos - start);
}

static const PatternDef *find_in(const PatternDef *pd, const char *name, int n)
{
    for (; pd && pd->name; pd++)
        if ((int)strlen(pd->name) == n && strncmp(pd->name, name, (size_t)n) == 0) return pd;
    return NULL;
}

/* Song-local patterns first, then the shared drum/riser library. */
static const PatternDef *find_pattern(const SongDef *s, const char *name, int n)
{
    const PatternDef *pd = find_in(s->patterns, name, n);
    return pd ? pd : find_in(g_common_patterns, name, n);
}

/* Returns the arrangement length in bars. */
static uint32_t compile_track(EvBuf *b, const SongDef *s, int track)
{
    const char *p = s->arrange[track];
    uint32_t bar = 0;
    if (!p) return 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *name = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '*' && *p != '+' && *p != '-') p++;
        int nlen = (int)(p - name);
        int transpose = 0, reps = 1;
        if (*p == '+' || *p == '-') {
            int sign = *p == '-' ? -1 : 1;
            p++;
            int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            transpose = sign * v;
        }
        if (*p == '*') {
            p++;
            int v = 0;
            while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
            reps = v > 0 ? v : 1;
        }
        if (nlen == 1 && name[0] == '.') {
            bar += (uint32_t)reps;
            continue;
        }
        const PatternDef *pd = find_pattern(s, name, nlen);
        if (!pd) {
            bar += (uint32_t)reps;
            continue;
        }
        for (int r = 0; r < reps; r++) {
            int steps = compile_pattern(b, track, pd->data, bar * 16, transpose);
            bar += (uint32_t)((steps + 15) / 16);
            if (steps <= 0) bar++;
        }
    }
    return bar;
}

static int ev_cmp(const void *a, const void *b)
{
    const SeqEvent *x = (const SeqEvent *)a, *y = (const SeqEvent *)b;
    if (x->step != y->step) return x->step < y->step ? -1 : 1;
    return (int)x->track - (int)y->track;
}

static void compile_song(int idx, const SongDef *s)
{
    EvBuf b = {NULL, 0, 0};
    uint32_t bars = 0;
    for (int t = 0; t < SONG_TRACKS; t++) {
        uint32_t tb = compile_track(&b, s, t);
        if (tb > bars) bars = tb;
    }
    if (b.n > 1) qsort(b.ev, (size_t)b.n, sizeof(SeqEvent), ev_cmp);
    CompiledSong *c = &s_cs[idx];
    c->ev = b.ev;
    c->nev = b.n;
    c->len_steps = bars * 16;
    if (c->len_steps == 0) c->len_steps = 16;
    c->loop_step = (uint32_t)clampi(s->loop_bar, 0, (int)bars - 1) * 16;
    c->bpm = s->bpm;
}

/* ------------------------------------------------------------------ */
/* Voice management                                                    */
/* ------------------------------------------------------------------ */

static float note_freq(int n)
{
    return 440.0f * powf(2.0f, (n - 69) / 12.0f);
}

static uint32_t s_rng_seed = 0x1234567u;

static Voice *alloc_voice(int track, int inst)
{
    const InstDef *in = &INST[inst];
    /* mono instruments reuse their track voice */
    if (in->mono && track >= 0) {
        for (int i = 0; i < MAX_VOICES; i++)
            if (s_v[i].on && s_v[i].track == track) return &s_v[i];
    }
    int count = 0, oldest = -1;
    float oldest_t = -1.0f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_v[i].on && s_v[i].track == track && s_v[i].inst == inst) {
            count++;
            if (s_v[i].t > oldest_t) { oldest_t = s_v[i].t; oldest = i; }
        }
    }
    if (count >= in->poly && oldest >= 0) return &s_v[oldest];
    for (int i = 0; i < MAX_VOICES; i++)
        if (!s_v[i].on) return &s_v[i];
    /* steal the quietest voice */
    int best = 0;
    float best_env = 1e9f;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_v[i].env < best_env) { best_env = s_v[i].env; best = i; }
    }
    return &s_v[best];
}

static void note_on(int track, int inst, int note, int vel, float len_sec)
{
    const InstDef *in = &INST[inst];
    Voice *v = alloc_voice(track, inst);
    int legato = v->on && v->gate && v->track == track && v->inst == inst && in->mono && in->kind == K_OSC;
    float f = note_freq(note);
    if (!legato || in->glide <= 0.0f) v->freq = f;
    v->target_freq = f;
    if (!(v->on && v->inst == inst && v->track == track)) {
        v->env = 0.0f;
        v->lp1 = v->lp2 = v->nlp = v->nlp2 = 0.0f;
        v->ph[0] = 0.0f;
        v->ph[1] = 0.33f;
        v->ph[2] = 0.67f;
    }
    if (in->kind != K_OSC) {
        v->ph[0] = 0.0f;
        v->env = 1.0f;
    }
    if (!legato) {
        v->stage = 0;
        v->t = 0.0f;
    }
    v->on = 1;
    v->gate = 1;
    v->inst = (uint8_t)inst;
    v->track = (int8_t)track;
    v->vel = vel / 255.0f;
    v->open = (uint8_t)(note == 'o');
    v->note = note;
    s_rng_seed = s_rng_seed * 1664525u + 1013904223u;
    v->rng = s_rng_seed | 1u;
    v->off_count = (int)(len_sec * SRF);
    if (v->off_count < 1) v->off_count = 1;
    if (in->kind == K_KICK && track >= 0) s_sc_t = 0.0f;
}

/* Phase in [0, 1) -> sine (table lookup, no floorf: it is slow on the EE). */
static inline float sine_lu(float ph)
{
    float x = ph * SINE_N;
    int i = (int)x;
    return s_sine[i] + (s_sine[i + 1] - s_sine[i]) * (x - (float)i);
}

/* Any (small) phase -> [0, 1). */
static inline float wrap01(float ph)
{
    ph -= (float)(int)ph;
    return ph < 0.0f ? ph + 1.0f : ph;
}

static inline float polyblep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static inline float noise(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static inline float saw_blep(float ph, float dt)
{
    return 2.0f * ph - 1.0f - polyblep(ph, dt);
}

static inline float pulse_blep(float ph, float dt, float pw)
{
    float v = ph < pw ? 1.0f : -1.0f;
    v += polyblep(ph, dt);
    float p2 = ph + 1.0f - pw;
    if (p2 >= 1.0f) p2 -= 1.0f;
    v -= polyblep(p2, dt);
    return v;
}

static inline float phase_step(float ph, float dt)
{
    ph += dt;
    return ph >= 1.0f ? ph - 1.0f : ph;
}

static float onepole_coef(float hz)
{
    if (hz > SRF * 0.45f) hz = SRF * 0.45f;
    if (hz < 10.0f) hz = 10.0f;
    return 1.0f - expf(-2.0f * PI * hz / SRF);
}

/* Envelope value after dt seconds; updates stage. */
static float env_advance(Voice *v, const InstDef *in, float dt)
{
    float e = v->env;
    if (v->stage == 0) {
        if (in->a <= 0.0005f) e = 1.0f;
        else e += dt / in->a;
        if (e >= 1.0f) {
            e = 1.0f;
            v->stage = 1;
        }
    } else if (v->stage == 1) {
        float k = in->d > 0.0f ? 1.0f - expf(-dt * 4.0f / in->d) : 1.0f;
        e -= (e - in->s) * k;
    } else {
        float k = in->r > 0.0f ? expf(-dt * 5.0f / in->r) : 0.0f;
        e *= k;
    }
    return e;
}

/* Hand-clap envelope: three quick bursts, then a decaying tail. */
static float clap_env(float t)
{
    if (t < 0.033f) return expf(-(t - 0.011f * (float)(int)(t / 0.011f)) * 260.0f);
    return 0.9f * expf(-(t - 0.033f) * 13.0f);
}

/* Adds the rendered block (s_blk) to the buses. */
static void bus_mix(int n, float pan, float gl, float gr, float send)
{
    const float *o = s_blk;
    if (pan == 0.0f) {
        for (int i = 0; i < n; i++) s_bufM[i] += o[i] * gl;
    } else {
        for (int i = 0; i < n; i++) {
            s_bufL[i] += o[i] * gl;
            s_bufR[i] += o[i] * gr;
        }
    }
    if (send > 0.0f)
        for (int i = 0; i < n; i++) s_send[i] += o[i] * send;
}

/* Raw oscillator for one block into s_blk (phases advance). */
static void render_osc(Voice *v, const InstDef *in, int n, float dt, float t0, float t1)
{
    float *o = s_blk;
    float p0 = v->ph[0], p1 = v->ph[1], p2 = v->ph[2];
    switch (in->wave) {
    case W_SAW:
        for (int i = 0; i < n; i++) {
            o[i] = saw_blep(p0, dt);
            p0 = phase_step(p0, dt);
        }
        break;
    case W_PULSE: {
        float pw = in->pw;
        for (int i = 0; i < n; i++) {
            o[i] = pulse_blep(p0, dt, pw);
            p0 = phase_step(p0, dt);
        }
        break;
    }
    case W_TRI:
        for (int i = 0; i < n; i++) {
            o[i] = 4.0f * fabsf(p0 - 0.5f) - 1.0f;
            p0 = phase_step(p0, dt);
        }
        break;
    case W_SINE:
        for (int i = 0; i < n; i++) {
            o[i] = sine_lu(p0);
            p0 = phase_step(p0, dt);
        }
        break;
    case W_SUPER: {
        float det = in->detune > 0.0f ? in->detune : 0.0f;
        float dt_hi = dt * (1.0f + det * 0.0578f), dt_lo = dt * (1.0f - det * 0.0578f);
        for (int i = 0; i < n; i++) {
            o[i] = (saw_blep(p0, dt) + saw_blep(p1, dt_hi) + saw_blep(p2, dt_lo)) * 0.45f;
            p0 = phase_step(p0, dt);
            p1 = phase_step(p1, dt_hi);
            p2 = phase_step(p2, dt_lo);
        }
        break;
    }
    case W_FM: {
        /* modulation index decays over the note (interpolated per block) */
        float idx = 2.2f * 0.16f * expf(-t0 * 5.0f), idx1 = 2.2f * 0.16f * expf(-t1 * 5.0f);
        float didx = (idx1 - idx) / (float)n, dm = dt * 3.5f;
        for (int i = 0; i < n; i++) {
            p1 = phase_step(p1, dm);
            o[i] = sine_lu(wrap01(p0 + idx * sine_lu(p1)));
            idx += didx;
            p0 = phase_step(p0, dt);
        }
        break;
    }
    default:
        for (int i = 0; i < n; i++) o[i] = 0.0f;
        break;
    }
    if (in->sub > 0.0f) {
        float sub = in->sub, keep = 1.0f - in->sub * 0.5f, ds = dt * 0.5f;
        for (int i = 0; i < n; i++) {
            p2 = phase_step(p2, ds);
            o[i] = o[i] * keep + sine_lu(p2) * sub;
        }
    }
    v->ph[0] = p0;
    v->ph[1] = p1;
    v->ph[2] = p2;
}

static void render_voice(Voice *v, int n)
{
    const InstDef *in = &INST[v->inst];
    const float dt_blk = n / SRF, inv_n = 1.0f / (float)n;
    float *o = s_blk;
    float gain;
    int music = v->track >= 0;
    if (music) {
        if (s_paused) return;
        gain = s_music_gain * s_track_vol[v->track];
    } else {
        gain = s_sfx_gain;
    }
    gain *= in->vol * v->vel;
    if (in->duck > 0.0f && music) gain *= 1.0f - in->duck * expf(-s_sc_t * 9.0f);

    float pan = in->pan;
    float gl = sqrtf(0.5f - pan * 0.5f) * 1.41f, gr = sqrtf(0.5f + pan * 0.5f) * 1.41f;

    /* gate handling */
    if (v->gate) {
        v->off_count -= n;
        if (v->off_count <= 0) {
            v->gate = 0;
            if (in->kind == K_OSC) v->stage = 2;
        }
    }

    float t0 = v->t, t1 = v->t + dt_blk;

    switch (in->kind) {
    case K_KICK: {
        /* sine with a falling pitch, plus a click at the very start */
        float pitch_base = in->vol > 0.8f ? 46.0f : 38.0f;
        float f = (pitch_base + 140.0f * expf(-t0 * 28.0f)) / SRF;
        float df = ((pitch_base + 140.0f * expf(-t1 * 28.0f)) / SRF - f) * inv_n;
        float a = expf(-t0 * 6.5f) * gain, da = (expf(-t1 * 6.5f) * gain - a) * inv_n;
        float ph = v->ph[0];
        for (int i = 0; i < n; i++) {
            ph = phase_step(ph, f);
            o[i] = sine_lu(ph) * a;
            f += df;
            a += da;
        }
        v->ph[0] = ph;
        if (t0 < 0.004f) {
            for (int i = 0; i < n; i++) {
                float tt = t0 + (float)i * (1.0f / SRF);
                if (tt < 0.004f) o[i] += noise(&v->rng) * 0.35f * (1.0f - tt * 250.0f) * gain;
            }
        }
        bus_mix(n, 0.0f, 1.0f, 1.0f, 0.0f);
        if (t1 > 0.6f) v->on = 0;
        break;
    }
    case K_SNARE: {
        float a = expf(-t0 * 15.0f) * 0.9f * gain, da = (expf(-t1 * 15.0f) * 0.9f * gain - a) * inv_n;
        float b = expf(-t0 * 32.0f) * 0.55f * gain, db = (expf(-t1 * 32.0f) * 0.55f * gain - b) * inv_n;
        float nlp = v->nlp, nlp2 = v->nlp2, ph = v->ph[0];
        const float hp = 0.12f, lp = 0.65f, f = 185.0f / SRF;
        for (int i = 0; i < n; i++) {
            float nz = noise(&v->rng);
            nlp += hp * (nz - nlp);
            nlp2 += lp * ((nz - nlp) - nlp2);
            ph = phase_step(ph, f);
            o[i] = nlp2 * a + sine_lu(ph) * b;
            a += da;
            b += db;
        }
        v->nlp = nlp;
        v->nlp2 = nlp2;
        v->ph[0] = ph;
        bus_mix(n, pan, gl, gr, in->send);
        if (t1 > 0.45f) v->on = 0;
        break;
    }
    case K_CLAP: {
        float e = clap_env(t0) * gain * 1.4f, de = (clap_env(t1) * gain * 1.4f - e) * inv_n;
        float nlp = v->nlp, nlp2 = v->nlp2;
        const float hp = 0.08f, lp = 0.45f;
        for (int i = 0; i < n; i++) {
            float nz = noise(&v->rng);
            nlp += hp * (nz - nlp);
            nlp2 += lp * ((nz - nlp) - nlp2);
            o[i] = nlp2 * e;
            e += de;
        }
        v->nlp = nlp;
        v->nlp2 = nlp2;
        bus_mix(n, pan, gl, gr, in->send);
        if (t1 > 0.5f) v->on = 0;
        break;
    }
    case K_HAT: {
        float rate = v->open ? 9.0f : 48.0f;
        float a = expf(-t0 * rate) * gain, da = (expf(-t1 * rate) * gain - a) * inv_n;
        float nlp = v->nlp;
        for (int i = 0; i < n; i++) {
            float nz = noise(&v->rng);
            nlp += 0.55f * (nz - nlp);
            o[i] = (nz - nlp) * a;
            a += da;
        }
        v->nlp = nlp;
        bus_mix(n, pan, gl, gr, 0.0f);
        if (t1 > (v->open ? 0.6f : 0.2f)) v->on = 0;
        break;
    }
    case K_NOISE: {
        /* falling noise sweep (death) */
        float c = onepole_coef(7000.0f * expf(-t0 * 7.0f) + 150.0f);
        float dc = (onepole_coef(7000.0f * expf(-t1 * 7.0f) + 150.0f) - c) * inv_n;
        float a = expf(-t0 * 4.5f) * gain * 1.6f, da = (expf(-t1 * 4.5f) * gain * 1.6f - a) * inv_n;
        float nlp = v->nlp, nlp2 = v->nlp2;
        for (int i = 0; i < n; i++) {
            nlp += c * (noise(&v->rng) - nlp);
            nlp2 += c * (nlp - nlp2);
            o[i] = nlp2 * a;
            c += dc;
            a += da;
        }
        v->nlp = nlp;
        v->nlp2 = nlp2;
        bus_mix(n, 0.0f, 1.0f, 1.0f, 0.0f);
        if (t1 > 0.9f) v->on = 0;
        break;
    }
    default: {
        /* pitch: glide + vibrato */
        if (in->glide > 0.0f && v->freq != v->target_freq) {
            float k = 1.0f - expf(-dt_blk / in->glide);
            v->freq += (v->target_freq - v->freq) * k;
        } else {
            v->freq = v->target_freq;
        }
        float f = v->freq;
        if (in->vib > 0.0f && t0 > 0.18f) {
            float depth = in->vib * minf(1.0f, (t0 - 0.18f) * 3.0f);
            f *= 1.0f + depth * 0.0578f * sinf(t0 * 2.0f * PI * 5.5f);
        }
        float e0 = v->env;
        float e1 = env_advance(v, in, dt_blk);
        v->env = e1;
        if (v->stage == 2 && e1 < 0.0004f) {
            v->on = 0;
        }
        render_osc(v, in, n, f / SRF, t0, t1);
        /* two one-pole low-passes, then the envelope */
        float cc = onepole_coef(in->cut + in->cut_env * expf(-t0 * in->cut_decay));
        float lp1 = v->lp1, lp2 = v->lp2;
        float e = e0 * gain, de = (e1 - e0) * gain * inv_n;
        for (int i = 0; i < n; i++) {
            lp1 += cc * (o[i] - lp1);
            lp2 += cc * (lp1 - lp2);
            o[i] = lp2 * e;
            e += de;
        }
        v->lp1 = lp1;
        v->lp2 = lp2;
        bus_mix(n, pan, gl, gr, in->send);
        break;
    }
    }
    v->t = t1;
}

/* ------------------------------------------------------------------ */
/* Sequencer                                                           */
/* ------------------------------------------------------------------ */

static void release_music_voices(void)
{
    for (int i = 0; i < MAX_VOICES; i++) {
        if (s_v[i].on && s_v[i].track >= 0) {
            if (INST[s_v[i].inst].kind == K_OSC) {
                s_v[i].gate = 0;
                s_v[i].stage = 2;
            } else {
                s_v[i].on = 0;
            }
        }
    }
}

static void start_song(int song, float start_samples, uint32_t gen)
{
    release_music_voices();
    s_gen = gen;
    if (song < 0 || song >= s_ncs || !g_songs[song]) {
        s_song = -1;
        return;
    }
    const SongDef *sd = g_songs[song];
    CompiledSong *c = &s_cs[song];
    s_song = song;
    s_step_samples = SRF * 60.0f / c->bpm / 4.0f;
    s_pos = start_samples;
    s_elapsed = start_samples;
    s_seq_idx = 0;
    float len = c->len_steps * s_step_samples;
    while (s_pos >= len) s_pos -= len - c->loop_step * s_step_samples;
    while (s_seq_idx < c->nev && c->ev[s_seq_idx].step * s_step_samples < s_pos) s_seq_idx++;
    for (int t = 0; t < SONG_TRACKS; t++) {
        s_track_vol[t] = sd->vol[t] > 0.0f ? sd->vol[t] : 1.0f;
        s_track_inst[t] = sd->inst[t];
    }
    s_track_inst[TR_KICK] = INS_KICK;
    s_track_inst[TR_HAT] = INS_HAT;
    s_delay_samples = clampi((int)(0.75f * 60.0f / c->bpm * SRF), 1000, DLY_LEN - 1);
    s_sc_t = 10.0f;
}

static void seq_advance(int n)
{
    if (s_song < 0 || s_paused) return;
    CompiledSong *c = &s_cs[s_song];
    float end = s_pos + n;
    float song_len = c->len_steps * s_step_samples;
    while (s_seq_idx < c->nev) {
        const SeqEvent *e = &c->ev[s_seq_idx];
        float when = e->step * s_step_samples;
        if (when >= end) break;
        int inst = s_track_inst[e->track];
        if (e->track == TR_SNARE) inst = e->note == 'c' ? INS_CLAP : INS_SNARE;
        note_on(e->track, inst, e->note, e->vel, e->len * s_step_samples / SRF * 0.92f);
        s_seq_idx++;
    }
    s_pos = end;
    s_elapsed += (float)n;
    if (s_pos >= song_len) {
        s_pos -= song_len - c->loop_step * s_step_samples;
        s_seq_idx = 0;
        while (s_seq_idx < c->nev && c->ev[s_seq_idx].step < c->loop_step) s_seq_idx++;
    }
}

static void sfx_advance(int n)
{
    for (int i = 0; i < s_nsfx;) {
        if (s_sfx[i].when < (float)n) {
            note_on(-1, s_sfx[i].inst, s_sfx[i].note, s_sfx[i].vel, s_sfx[i].len);
            s_sfx[i] = s_sfx[--s_nsfx];
            continue;
        }
        s_sfx[i].when -= (float)n;
        i++;
    }
}

static void process_commands(void)
{
    while (s_qtail != s_qhead) {
        Cmd c = s_q[s_qtail % QN];
        mem_barrier();
        s_qtail++;
        switch (c.type) {
        case CMD_PLAY: start_song(c.a, c.f, c.gen); break;
        case CMD_STOP:
            release_music_voices();
            s_song = -1;
            s_gen = c.gen;
            break;
        case CMD_SFX:
            if (c.a >= 0 && c.a < SFX_COUNT) {
                for (int k = 0; k < 8 && SFX_DEFS[c.a][k].at >= 0.0f; k++) {
                    if (s_nsfx >= MAX_PENDING_SFX) break;
                    const SfxStep *st = &SFX_DEFS[c.a][k];
                    SfxNote sn = {st->at * SRF, st->inst, st->note, st->vel, st->len};
                    s_sfx[s_nsfx++] = sn;
                }
            }
            break;
        case CMD_VOL:
            s_music_gain = (c.a / 10.0f) * (c.a / 10.0f);
            s_sfx_gain = (c.b / 10.0f) * (c.b / 10.0f);
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void audio_init(void)
{
    for (int i = 0; i <= SINE_N + 1; i++) s_sine[i] = sinf((float)i / SINE_N * 2.0f * PI);
    memset(s_v, 0, sizeof(s_v));
    memset(s_dl, 0, sizeof(s_dl));
    memset(s_dr, 0, sizeof(s_dr));
    s_ncs = mini(g_song_count, MAX_SONGS);
    for (int i = 0; i < s_ncs; i++) compile_song(i, g_songs[i]);
    s_song = -1;
    m_song = -1;
}

void audio_mix(int16_t *out, int frames)
{
    process_commands();
    int done = 0;
    while (done < frames) {
        int n = mini(CTRL, frames - done);
        seq_advance(n);
        sfx_advance(n);
        memset(s_bufL, 0, sizeof(float) * (size_t)n);
        memset(s_bufR, 0, sizeof(float) * (size_t)n);
        memset(s_bufM, 0, sizeof(float) * (size_t)n);
        memset(s_send, 0, sizeof(float) * (size_t)n);
        for (int i = 0; i < MAX_VOICES; i++)
            if (s_v[i].on) render_voice(&s_v[i], n);
        s_sc_t += n / SRF;

        for (int i = 0; i < n; i++) {
            /* ping-pong echo */
            int rp = s_dpos - s_delay_samples;
            if (rp < 0) rp += DLY_LEN;
            float el = s_dl[rp], er = s_dr[rp];
            s_dl[s_dpos] = s_send[i] + er * 0.38f;
            s_dr[s_dpos] = s_send[i] * 0.6f + el * 0.38f;
            s_dpos = (s_dpos + 1) & (DLY_LEN - 1);
            float l = s_bufL[i] + s_bufM[i] + el * 0.55f;
            float r = s_bufR[i] + s_bufM[i] + er * 0.55f;
            /* soft clip */
            l = clampf(l, -1.5f, 1.5f);
            r = clampf(r, -1.5f, 1.5f);
            l = l - l * l * l * (4.0f / 27.0f);
            r = r - r * r * r * (4.0f / 27.0f);
            int il = (int)(l * 32000.0f), ir = (int)(r * 32000.0f);
            out[(done + i) * 2] = (int16_t)clampi(il, -32767, 32767);
            out[(done + i) * 2 + 1] = (int16_t)clampi(ir, -32767, 32767);
        }
        done += n;
    }
    s_pub_elapsed = s_song >= 0 ? s_elapsed : 0.0f;
    mem_barrier();
    s_pub_gen = s_gen;
}

static uint32_t next_gen(void)
{
    return ++m_gen;
}

void audio_set_latency(float sec)
{
    s_latency = sec > 0.0f ? sec : 0.0f;
}

void audio_set_user_delay(float sec)
{
    s_user_delay = sec;
}

/* Seconds from mixing a sample to hearing it. */
static float heard_delay(void)
{
    return s_latency + s_user_delay;
}

void audio_play_song(int song, float start_sec)
{
    /* what gets mixed now is heard heard_delay() later (a negative start
     * just holds the song back) */
    Cmd c = {CMD_PLAY, song, 0, (start_sec + heard_delay()) * SRF, next_gen()};
    m_song = song;
    push_cmd(c);
}

void audio_stop_song(void)
{
    Cmd c = {CMD_STOP, 0, 0, 0.0f, next_gen()};
    m_song = -1;
    push_cmd(c);
}

void audio_sfx(int id)
{
    Cmd c = {CMD_SFX, id, 0, 0.0f, 0};
    push_cmd(c);
}

void audio_set_volume(int music, int sfx)
{
    Cmd c = {CMD_VOL, clampi(music, 0, 10), clampi(sfx, 0, 10), 0.0f, 0};
    push_cmd(c);
}

void audio_pause(int paused)
{
    /* Written directly: a single int flag is safe to share. */
    s_paused = paused;
}

int audio_current_song(void)
{
    return m_song;
}

float audio_song_time(void)
{
    if (m_song < 0) return 0.0f;
    if (s_pub_gen != m_gen) return 0.0f;
    return s_pub_elapsed / SRF - heard_delay();
}

float audio_song_beat(void)
{
    if (m_song < 0 || m_song >= s_ncs) return 0.0f;
    return audio_song_time() * s_cs[m_song].bpm / 60.0f;
}

int audio_song_count(void)
{
    return s_ncs;
}

const char *audio_song_name(int song)
{
    if (song < 0 || song >= s_ncs) return "";
    return g_songs[song]->title;
}

float audio_song_bpm(int song)
{
    if (song < 0 || song >= s_ncs) return 120.0f;
    return s_cs[song].bpm;
}

float audio_song_length(int song)
{
    if (song < 0 || song >= s_ncs) return 0.0f;
    return s_cs[song].len_steps * 15.0f / s_cs[song].bpm;
}

/* ------------------------------------------------------------------ */
/* Tooling helpers (used by the host test tool)                         */
/* ------------------------------------------------------------------ */

int audio_pattern_steps(const char *data, int drum)
{
    EvBuf b = {NULL, 0, 0};
    int steps = compile_pattern(&b, drum ? TR_KICK : TR_LEAD, data, 0, 0);
    free(b.ev);
    return steps;
}

int audio_song_bars(int song)
{
    if (song < 0 || song >= s_ncs) return 0;
    return (int)(s_cs[song].len_steps / 16);
}

int audio_song_notes(int song, const AudioNote **notes, uint32_t *len_steps, uint32_t *loop_step)
{
    if (song < 0 || song >= s_ncs) return 0;
    *notes = s_cs[song].ev;
    *len_steps = s_cs[song].len_steps;
    *loop_step = s_cs[song].loop_step;
    return s_cs[song].nev;
}
