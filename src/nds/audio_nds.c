/*
 * The DS's sound: the core's audio.h, played from the recordings nds_tool
 * made with the core's own synthesizer (src/host/nds_audio.c).
 *
 * The music. The songs (music_hp.bin, music_spk.bin: the two mixes of the
 * options' OUTPUT) are in the ROM's file system, 34 MB each, read as they
 * play: blocks of 1024 samples a side (8-bit samples in groups of 32 that
 * share a shift), kept in a cache of CACHE_BLOCKS (4 s); each song's
 * first FIRST_BLOCKS are kept in memory from the start, so that a song
 * starts at once. The loop reads what is coming, up to READ_AHEAD, two
 * blocks at a time (0.7 ms a block in melonDS), in frames with the time
 * left for it (audio_nds_update), and in a busy frame only what is
 * needed in the next quarter of a second: a level's busiest stretches
 * last a few seconds. The ARM9 decodes them into a ring of RING 16-bit
 * samples a side, which two of the sound hardware's channels play round
 * and round (panned hard left and right), filled AHEAD samples (62 ms,
 * nearly four frames) ahead of where they play, so a frame that runs late
 * doesn't leave them without sound.
 *
 * Where they play: timers 0 and 1 of the ARM9 count the samples, at the
 * channels' own rate (a sample every 1024 cycles of the 33.5 MHz bus; the
 * channels' timers count 512 of their 16.8 MHz), started with them. A
 * frame is 560190 cycles: 547.06 samples, which the songs were recorded
 * to play in a tick of song time (RATE_F), so they keep step with the
 * game's tick a frame.
 *
 * A song is a function of how far into it the sound is ("e", in samples,
 * through its loops): before its loop's end, the recording at e; after,
 * the loop again and again (a jump back by its length), each jump
 * crossfaded over XFADE samples with what the recording has past the
 * jump's point. So whatever starts, stops or changes (a song, the pause,
 * the output, the volume, the audio delay) is written from just ahead of
 * where the channels play, the rest of the ring written again: what is
 * heard changes 4 ms later at most. audio_song_time() is e where the
 * channels play now, from the anchors those changes leave.
 *
 * The sound effects (sfx.bin) are 8-bit samples in memory, both mixes
 * (680 KB), each played by a channel of its own, chosen by libnds.
 *
 * Without the file system (a loader that gives the game no way to read its
 * ROM) it plays nothing, but its time goes on: the game is the same.
 */
#include <nds.h>
#include <filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nds_platform.h"
#include "../core/audio.h"
#include "gen_nds.h"

#define RATE_HZ 32728          /* soundPlaySample's: a timer of 512 (32728.5 Hz) */
#define RATE_F 32823.633f      /* samples a second of song time: 547.06 a tick (nds_audio.c) */
#define RING 8192              /* samples a side in the channels' ring (a quarter of a second) */
#define CHUNK 64               /* the ring is written in chunks of this */
#define AHEAD 2048             /* samples written ahead of where the channels play */
#define MARGIN 128             /* changes are written from this far ahead of them */
#define FILL_MAX 1100          /* samples written a frame at most (two frames' worth),
                                  but for what the next frame needs */
#define FILL_MIN 1200          /* what is written ahead, whatever it takes */
#define BLOCK 1024             /* samples a side in a block of a song */
#define GROUP 32
#define BLOCK_BYTES (2 * (BLOCK / GROUP) * (1 + GROUP))
#define XFADE 256
#define CACHE_BLOCKS 128
#define READ_AHEAD (120 * BLOCK) /* samples of a song the reader keeps ready (3.7 s) */
#define READ_COST 60000        /* bus cycles a read of MAX_READ blocks takes, at most (melonDS: 40000) */
#define MAX_READ 2             /* blocks read at a time, at most (a frame plays half of one) */
#define SFX_VOICES 4
#define FIRST_BLOCKS 2         /* each song's first blocks, kept in memory */

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t offset, blocks, samples, loop_end, loop_to;
} SongInfo;

static int16_t s_ring[2][RING] __attribute__((aligned(32)));
static int s_ch[2] = {-1, -1};
static int s_on;               /* the sound plays (the file system and the channels are there) */

static FILE *s_file[2];        /* the two mixes */
static SongInfo s_info[2][NDS_SONG_COUNT];
static int s_mix;              /* 0: headphones, 1: speakers */
static int s_want_mix;

static uint8_t *s_sfx_data;
static uint32_t s_sfx_off[SFX_COUNT][2], s_sfx_len[SFX_COUNT][2];
static int s_sfx_vol = 81;     /* 127 * 8^2 / 100: volume 8 */
static int s_sfx_voice[SFX_VOICES] = {-1, -1, -1, -1};
static int s_sfx_next;

/* each song's first blocks in each mix (its start never waits for the card) */
static uint8_t (*s_first)[2][NDS_SONG_COUNT][FIRST_BLOCKS][BLOCK_BYTES];

/* the block cache */
static uint8_t s_cache[CACHE_BLOCKS][BLOCK_BYTES] __attribute__((aligned(4)));
static int32_t s_cache_block[CACHE_BLOCKS]; /* block index of the current song and mix, -1 free */
#define SONG_BLOCKS 8192       /* blocks a song has at most (4 minutes) */
static uint8_t s_slot_of[SONG_BLOCKS]; /* a block's slot, if s_cache_block says it holds it */
static int s_cache_song = -1, s_cache_mix = -1;
static uint32_t s_cache_use[CACHE_BLOCKS], s_use_clock;

/* where the channels play: samples since they started (timer 1 extended) */
static uint32_t s_play;
static uint16_t s_play_lo;

/* what is written */
static uint32_t s_written;     /* ring samples written up to (since the start) */
static int s_song = -1;        /* playing (audio_current_song) */
static int32_t s_e;            /* the song's e at s_written */
static int s_paused;
static int s_gain = 256;       /* the music's, 1/256: (volume / 8)^2 */
static int32_t s_delay;        /* the audio delay, samples */

/* audio_song_time's anchors: from ring sample `at` on, e was `e` there,
 * going on (or held, paused) */
#define ANCHORS 8
typedef struct {
    uint32_t at;
    int32_t e;
    int8_t song, held;
} Anchor;
static Anchor s_anchor[ANCHORS];
static int s_anchors;

/* ------------------------------------------------------------------ */
/* Where the channels play                                             */
/* ------------------------------------------------------------------ */

static uint32_t play_now(void)
{
    uint16_t lo = TIMER1_DATA;
    s_play += (uint16_t)(lo - s_play_lo);
    s_play_lo = lo;
    return s_play;
}

/* ------------------------------------------------------------------ */
/* Reading the songs                                                   */
/* ------------------------------------------------------------------ */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int open_music(int mix, const char *path)
{
    uint8_t head[20 + NDS_SONG_COUNT * 64];
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    /* blocks are read whole: no buffer of the C library's in between */
    setvbuf(f, NULL, _IONBF, 0);
    if (fread(head, 1, sizeof(head), f) != sizeof(head) || memcmp(head, "PDMU", 4) || rd32(head + 16) != NDS_SONG_COUNT) {
        fclose(f);
        return 0;
    }
    for (int i = 0; i < NDS_SONG_COUNT; i++) {
        const uint8_t *t = head + 20 + i * 64;
        s_info[mix][i] = (SongInfo){rd32(t), rd32(t + 4), rd32(t + 8), rd32(t + 12), rd32(t + 16)};
    }
    s_file[mix] = f;
    return 1;
}

static int load_first_blocks(void)
{
    s_first = malloc(sizeof(*s_first));
    if (!s_first) return 0;
    for (int mix = 0; mix < 2; mix++)
        for (int i = 0; i < NDS_SONG_COUNT; i++)
            if (fseek(s_file[mix], (long)s_info[mix][i].offset, SEEK_SET) ||
                fread((*s_first)[mix][i], BLOCK_BYTES, FIRST_BLOCKS, s_file[mix]) != FIRST_BLOCKS) {
                free(s_first);
                s_first = NULL;
                return 0;
            }
    return 1;
}

static void cache_reset(void)
{
    for (int i = 0; i < CACHE_BLOCKS; i++) s_cache_block[i] = -1;
    s_cache_song = s_song;
    s_cache_mix = s_mix;
}

/* the slot holding block b of the song playing (-1: none); the first
 * blocks are FIRST_SLOT */
#define FIRST_SLOT CACHE_BLOCKS
static int cache_find(int32_t b)
{
    if (b < FIRST_BLOCKS && s_first) return FIRST_SLOT;
    if (s_cache_song != s_song || s_cache_mix != s_mix) cache_reset();
    if (b < 0 || b >= SONG_BLOCKS) return -1;
    int i = s_slot_of[b];
    if (s_cache_block[i] != b) return -1;
    s_cache_use[i] = ++s_use_clock;
    return i;
}

/* the recording's sample for e (past the loop's end: in the loop), and
 * when the crossfade is on, the sample of the jump's other side in *x2,
 * weight *w2 (of XFADE) */
static int32_t rec_pos(const SongInfo *si, int32_t e, int32_t *x2, int *w2)
{
    *w2 = 0;
    if (e < (int32_t)si->loop_end) return e;
    int32_t len = (int32_t)(si->loop_end - si->loop_to), k = (e - (int32_t)si->loop_end) % len;
    if (k < XFADE) {
        /* fading in the loop at loop_to + k, out the recording past the jump */
        *x2 = (int32_t)si->loop_end + k;
        *w2 = XFADE - k;
    }
    return (int32_t)si->loop_to + k;
}

/* Reads n blocks from b on into free (or the least used) slots. */
static void read_blocks(int32_t b, int n)
{
    const SongInfo *si = &s_info[s_mix][s_song];
    static uint8_t buf[MAX_READ * BLOCK_BYTES] __attribute__((aligned(4)));
    if (b < 0 || b >= (int32_t)si->blocks || b >= SONG_BLOCKS - MAX_READ) return;
    if (b + n > (int32_t)si->blocks) n = (int)si->blocks - b;
    if (n > MAX_READ) n = MAX_READ;
    if (fseek(s_file[s_mix], (long)(si->offset + (uint32_t)b * BLOCK_BYTES), SEEK_SET)) return;
    n = (int)fread(buf, BLOCK_BYTES, (size_t)n, s_file[s_mix]);
    for (int i = 0; i < n; i++) {
        if (cache_find(b + i) >= 0) continue;
        int slot = 0;
        for (int k = 0; k < CACHE_BLOCKS; k++) {
            if (s_cache_block[k] < 0) {
                slot = k;
                break;
            }
            if (s_cache_use[k] < s_cache_use[slot]) slot = k;
        }
        memcpy(s_cache[slot], buf + i * BLOCK_BYTES, BLOCK_BYTES);
        s_cache_block[slot] = b + i;
        s_slot_of[b + i] = (uint8_t)slot;
        s_cache_use[slot] = ++s_use_clock;
    }
}

/* The block holding recording sample x, read now if it isn't there. */
static const uint8_t *block_of(int32_t x)
{
    int32_t b = x / BLOCK;
    int i = cache_find(b);
    if (i < 0) {
        read_blocks(b, 1);
        i = cache_find(b);
    }
    if (i == FIRST_SLOT) return (*s_first)[s_mix][s_song][b];
    return i < 0 ? NULL : s_cache[i];
}

/* ------------------------------------------------------------------ */
/* Writing the ring                                                    */
/* ------------------------------------------------------------------ */

static inline int16_t clip16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* side c's samples x.. of the recording, n of them (in one block), times
 * the gain, into out (w: of XFADE, the weight; added to what is there when
 * add) */
static void decode(const uint8_t *blk, int c, int32_t x, int n, int16_t *out, int w, int add)
{
    int i = x % BLOCK, gain = s_gain * w / XFADE;
    while (n > 0) {
        /* a group's samples at a time: its shift, then bytes */
        const uint8_t *g = blk + (c * (BLOCK / GROUP) + i / GROUP) * (1 + GROUP);
        const int8_t *b = (const int8_t *)g + 1 + i % GROUP;
        int sh = g[0], k = GROUP - i % GROUP;
        if (k > n) k = n;
        if (gain == 256 && !add) {
            /* (as recorded: an 8-bit sample shifted by at most 8 fits) */
            for (int j = 0; j < k; j++) out[j] = (int16_t)(b[j] << sh);
        } else {
            for (int j = 0; j < k; j++) {
                int32_t v = ((b[j] << sh) * gain) >> 8;
                out[j] = clip16(add ? out[j] + v : v);
            }
        }
        out += k;
        i += k;
        n -= k;
    }
}

/* n samples (within one chunk of the ring) of the song from e on, at ring
 * sample r: in runs that keep to one block and one side of the loop's
 * jump and its crossfade */
static void write_song(uint32_t r, int n, int32_t e)
{
    int16_t *out[2] = {&s_ring[0][r % RING], &s_ring[1][r % RING]};
    const SongInfo *si = s_on && s_song >= 0 ? &s_info[s_mix][s_song] : NULL;
    if (!si || s_paused || s_gain == 0) {
        memset(out[0], 0, (size_t)n * 2);
        memset(out[1], 0, (size_t)n * 2);
        return;
    }
    int done = 0;
    while (done < n) {
        int32_t x2 = 0, x;
        int w2, run = n - done;
        if (e < 0) {
            run = -e < run ? -e : run;
            memset(out[0] + done, 0, (size_t)run * 2);
            memset(out[1] + done, 0, (size_t)run * 2);
            done += run;
            e += run;
            continue;
        }
        x = rec_pos(si, e, &x2, &w2);
        if (e < (int32_t)si->loop_end) {
            if ((int32_t)si->loop_end - e < run) run = (int)((int32_t)si->loop_end - e);
        } else {
            int32_t k = x - (int32_t)si->loop_to, len = (int32_t)(si->loop_end - si->loop_to);
            int32_t to = k < XFADE ? XFADE : len;
            if (to - k < run) run = (int)(to - k);
            /* (the crossfade's weight goes in steps of 16 samples) */
            if (w2 && run > 16) run = 16;
        }
        if (BLOCK - x % BLOCK < run) run = BLOCK - x % BLOCK;
        if (w2 && BLOCK - x2 % BLOCK < run) run = BLOCK - x2 % BLOCK;
        const uint8_t *b = block_of(x);
        for (int c = 0; c < 2; c++) {
            if (b) decode(b, c, x, run, out[c] + done, XFADE - w2, 0);
            else memset(out[c] + done, 0, (size_t)run * 2);
        }
        if (w2) {
            const uint8_t *b2 = block_of(x2);
            if (b2)
                for (int c = 0; c < 2; c++) decode(b2, c, x2, run, out[c] + done, w2, 1);
        }
        done += run;
        e += run;
    }
}

/* Writes the ring up to `to`, in chunks. */
static void fill(uint32_t to)
{
    while ((int32_t)(to - s_written) > 0) {
        uint32_t r = s_written;
        int n = CHUNK - (int)(r % CHUNK);
        if ((int32_t)(to - r) < n) n = (int)(to - r);
        write_song(r, n, s_e);
        if (!s_paused) s_e += n;
        DC_FlushRange(&s_ring[0][r % RING], (u32)n * 2);
        DC_FlushRange(&s_ring[1][r % RING], (u32)n * 2);
        s_written += (uint32_t)n;
    }
}

/* A change from just ahead of where the channels play: the ring is written
 * again from there (its e as it stood there), and an anchor left. */
static uint32_t rewind_to_now(void)
{
    uint32_t p = play_now() + MARGIN;
    if ((int32_t)(p - s_written) < 0) {
        /* e at p (the ring was written in one state since the last
         * change: that one) */
        int32_t e = s_e;
        if (!s_paused) e -= (int32_t)(s_written - p);
        s_written = p;
        s_e = e;
    } else {
        /* (the ring had run dry: a long stall) */
        if (!s_paused) s_e += (int32_t)(p - s_written);
        s_written = p;
    }
    return p;
}

static void anchor(uint32_t at)
{
    if (s_anchors == ANCHORS) {
        memmove(s_anchor, s_anchor + 1, sizeof(Anchor) * (ANCHORS - 1));
        s_anchors--;
    }
    s_anchor[s_anchors++] = (Anchor){at, s_e, (int8_t)s_song, (int8_t)s_paused};
}

/* ------------------------------------------------------------------ */
/* The frontend's                                                      */
/* ------------------------------------------------------------------ */

static void load_sfx(void)
{
    uint8_t head[16 + SFX_COUNT * 2 * 12];
    FILE *f = fopen("nitro:/sfx.bin", "rb");
    if (!f) return;
    if (fread(head, 1, sizeof(head), f) == sizeof(head) && !memcmp(head, "PDSX", 4)) {
        uint32_t lo = 0xFFFFFFFFu, hi = 0;
        for (int i = 0; i < SFX_COUNT; i++)
            for (int m = 0; m < 2; m++) {
                const uint8_t *t = head + 16 + (i * 2 + m) * 12;
                s_sfx_off[i][m] = rd32(t);
                s_sfx_len[i][m] = rd32(t + 4);
                if (s_sfx_off[i][m] < lo) lo = s_sfx_off[i][m];
                if (s_sfx_off[i][m] + s_sfx_len[i][m] > hi) hi = s_sfx_off[i][m] + s_sfx_len[i][m];
            }
        s_sfx_data = malloc(hi - lo);
        if (s_sfx_data && !fseek(f, (long)lo, SEEK_SET) && fread(s_sfx_data, 1, hi - lo, f) == hi - lo) {
            for (int i = 0; i < SFX_COUNT; i++)
                for (int m = 0; m < 2; m++) s_sfx_off[i][m] -= lo;
            DC_FlushRange(s_sfx_data, hi - lo);
        } else {
            free(s_sfx_data);
            s_sfx_data = NULL;
        }
    }
    fclose(f);
}

int audio_nds_init(void)
{
    if (nitroFSInit(NULL) && open_music(0, "nitro:/music_hp.bin") && open_music(1, "nitro:/music_spk.bin")) {
        load_sfx();
        load_first_blocks();
        s_on = 1;
    }
    cache_reset();
    memset(s_ring, 0, sizeof(s_ring));
    DC_FlushRange(s_ring, sizeof(s_ring));
    soundEnable();
    /* the sample counter: timer 0 overflows every sample, timer 1 counts */
    TIMER0_CR = 0;
    TIMER1_CR = 0;
    TIMER1_DATA = 0;
    TIMER0_DATA = (uint16_t)(65536 - 1024);
    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE;
    TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
    s_ch[0] = soundPlaySample(s_ring[0], SoundFormat_16Bit, sizeof(s_ring[0]), RATE_HZ, 127, 0, true, 0);
    s_ch[1] = soundPlaySample(s_ring[1], SoundFormat_16Bit, sizeof(s_ring[1]), RATE_HZ, 127, 127, true, 0);
    s_play = 0;
    s_play_lo = 0;
    s_written = MARGIN;
    anchor(0);
    return s_on;
}

/* The reader: blocks the next READ_AHEAD samples of the song will need,
 * a read at a time while the frame has time (budget: bus cycles left). */
static void read_ahead(int urgent_only)
{
    if (!s_on || s_song < 0 || s_paused) return;
    const SongInfo *si = &s_info[s_mix][s_song];
    for (int32_t d = 0; d < READ_AHEAD; d += BLOCK) {
        int32_t x2, x;
        int w2;
        x = rec_pos(si, s_e + d, &x2, &w2);
        if (x < 0) continue;
        if (cache_find(x / BLOCK) < 0) {
            if (urgent_only && d > 8 * BLOCK) return;
            read_blocks(x / BLOCK, MAX_READ);
            return; /* a read a frame */
        }
        if (w2 && cache_find(x2 / BLOCK) < 0) {
            read_blocks(x2 / BLOCK, 1);
            return;
        }
    }
}

/* For the emulator test (src/host/nds_test.c): the song playing and its
 * time as heard (samples of song time), as each frame's sound was written */
volatile int32_t g_audio_song = -1, g_audio_heard;

void audio_nds_update(uint32_t until)
{
    if (s_want_mix != s_mix) {
        rewind_to_now();
        s_mix = s_want_mix;
        anchor(s_written);
    }
    /* ahead to AHEAD, but no more than FILL_MAX a frame past FILL_MIN: a
     * change writes the ring again from now (a song starting: its first
     * frames) and tops it up over the next frames */
    uint32_t p = play_now(), to = p + AHEAD;
    if ((int32_t)(to - s_written) > FILL_MAX && (int32_t)(s_written + FILL_MAX - (p + FILL_MIN)) > 0)
        to = s_written + FILL_MAX;
    else if ((int32_t)(to - s_written) > FILL_MAX)
        to = p + FILL_MIN;
    fill(to);
    /* (a read takes a millisecond or two from a flash card's memory card) */
    read_ahead((int32_t)(until - nds_clock()) < READ_COST);
    g_audio_song = s_song;
    g_audio_heard = (int32_t)(audio_song_time() * RATE_F + 0.5f);
}

void audio_nds_hold(void)
{
    /* as far as the ring holds, but for a little */
    fill(play_now() + RING - 4 * CHUNK);
}

uint32_t audio_nds_play_pos(void)
{
    return play_now();
}

/* ------------------------------------------------------------------ */
/* audio.h                                                             */
/* ------------------------------------------------------------------ */

void audio_init(void)
{
    /* the frontend calls audio_nds_init() */
}

void audio_mix(int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * 4);
}

void audio_set_latency(float sec)
{
    (void)sec;
}

void audio_set_user_delay(float sec)
{
    int32_t d = (int32_t)(sec * RATE_F + (sec < 0.0f ? -0.5f : 0.5f));
    if (d == s_delay) return;
    if (s_song >= 0) {
        /* the sound moves by the change, its time doesn't */
        rewind_to_now();
        s_e += d - s_delay;
        s_delay = d;
        anchor(s_written);
    }
    s_delay = d;
}

void audio_play_song(int song, float start_sec)
{
    if (song < 0 || song >= NDS_SONG_COUNT) {
        audio_stop_song();
        return;
    }
    /* start_sec heard now: as much further on where it is first written */
    uint32_t now = play_now();
    uint32_t p = rewind_to_now();
    s_song = song;
    s_paused = 0;
    s_e = (int32_t)(start_sec * RATE_F + 0.5f) + s_delay + (int32_t)(p - now);
    anchor(p);
}

void audio_stop_song(void)
{
    uint32_t p = rewind_to_now();
    s_song = -1;
    s_paused = 0;
    s_e = 0;
    anchor(p);
}

void audio_sfx(int id)
{
    if (!s_sfx_data || id < 0 || id >= SFX_COUNT || s_sfx_vol == 0) return;
    /* a voice of its own; the oldest of SFX_VOICES makes way */
    int v = s_sfx_next;
    s_sfx_next = (s_sfx_next + 1) % SFX_VOICES;
    if (s_sfx_voice[v] >= 0) soundKill(s_sfx_voice[v]);
    s_sfx_voice[v] = soundPlaySample(s_sfx_data + s_sfx_off[id][s_mix], SoundFormat_8Bit, s_sfx_len[id][s_mix],
                                     RATE_HZ, (u8)s_sfx_vol, 64, false, 0);
}

void audio_set_volume(int music, int sfx)
{
    /* the synth's curve, (volume / 10)^2: the songs were recorded at 8
     * (gain 256), the sound effects stored as at 10 (the channel's 127) */
    music = music < 0 ? 0 : music > 10 ? 10 : music;
    sfx = sfx < 0 ? 0 : sfx > 10 ? 10 : sfx;
    int g = music * music * 4;
    s_sfx_vol = (sfx * sfx * 127 + 50) / 100;
    if (g != s_gain) {
        rewind_to_now();
        s_gain = g;
        anchor(s_written);
    }
}

void audio_pause(int paused)
{
    paused = paused != 0;
    if (paused == s_paused || s_song < 0) {
        s_paused = paused;
        return;
    }
    uint32_t p = rewind_to_now();
    s_paused = paused;
    anchor(p);
}

void audio_suspend(int suspended)
{
    /* nothing puts a menu over the game on the DS */
    (void)suspended;
}

void audio_set_output(int speaker)
{
    s_want_mix = speaker ? 1 : 0;
}

float audio_song_time(void)
{
    uint32_t p = play_now();
    int i = s_anchors - 1;
    while (i > 0 && (int32_t)(p - s_anchor[i].at) < 0) i--;
    const Anchor *a = &s_anchor[i];
    if (a->song < 0) return 0.0f;
    int32_t e = a->e + (a->held || (int32_t)(p - a->at) < 0 ? 0 : (int32_t)(p - a->at));
    return (float)(e - s_delay) * (1.0f / RATE_F);
}

float audio_song_beat(void)
{
    return s_song < 0 ? 0.0f : audio_song_time() * g_nds_songs[s_song].bpm * (1.0f / 60.0f);
}

int audio_current_song(void)
{
    return s_song;
}

int audio_song_count(void)
{
    return NDS_SONG_COUNT;
}

const char *audio_song_name(int song)
{
    return song >= 0 && song < NDS_SONG_COUNT ? g_nds_songs[song].name : "";
}

float audio_song_bpm(int song)
{
    return song >= 0 && song < NDS_SONG_COUNT ? g_nds_songs[song].bpm : 120.0f;
}

float audio_song_length(int song)
{
    return song >= 0 && song < NDS_SONG_COUNT ? g_nds_songs[song].length : 0.0f;
}

/* The tool helpers: the DS has no note data. */
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
