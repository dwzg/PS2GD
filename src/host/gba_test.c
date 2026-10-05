/*
 * gba_test - runs the Game Boy Advance ROM in mGBA's core (libmgba),
 * headless, for tests and screenshots.
 *
 *   gba_test run <rom> <frames> [script] [shots]
 *       run the ROM for <frames> frames; script is a list of key changes
 *       "frame:KEYS,frame:KEYS,..." (KEYS: A B L R U D LT RT ST SE joined by +,
 *       or - for none); shots is a list of frames to save as
 *       shot_<frame>.png (240x160) in the current directory
 *
 *   gba_test play <rom> [levels] [shots-dir]
 *       the emulator test: visits the garage, the options and the garage
 *       again (a screen opened a second time draws on a surface just
 *       emptied: the slowest rows), then plays
 *       each level (all, or a list "0,2,5") from the menus with the
 *       solver's inputs (src/host/solver.c), after an attempt that dies
 *       and the respawn, with a pause a third of the way in, and checks
 *       that the ROM's player is the reference's (src/core/sim.c run here)
 *       after every tick, that the level is finished, that no frame of a
 *       run was late (the game, its music and the inputs would slip), that
 *       every attempt keeps time with its song, that no tile of BG0's text
 *       mixes two palettes on any screen seen, that the progress is saved,
 *       that the save is there after a restart, and that a save cut short
 *       leaves the one before it.
 *       With shots-dir: a picture of each level's respawn, start, middle,
 *       results (and of BG0's surface when a tile mixes palettes).
 *       Exits non-zero if anything failed.
 *
 * The ROM's mGBA debug output (its log lines) is printed as it comes. The
 * ROM's ELF file must be beside it (pulsedash.elf for pulsedash.gba): the
 * frames the game was late for (g_frames.late, src/gba/frame.h) and, for
 * play, where the game's state is (g_test_info, src/gba/main.c) are read
 * from it.
 */
/* (first: the library's build options, which the structures below depend on) */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/input.h>
#include <elf.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_write.h"
#include "solver.h"
#include "../core/game_internal.h"
#include "../core/level.h"
#include "../core/progress.h"
#include "../core/sim.h"
#include "../gba/art.h"

static void log_cb(struct mLogger *l, int cat, enum mLogLevel lvl, const char *fmt, va_list ap)
{
    const char *name = mLogCategoryName(cat);
    (void)l;
    if (name && strstr(name, "Debug")) {
        vprintf(fmt, ap);
        putchar('\n');
    } else if (lvl & (mLOG_FATAL | mLOG_ERROR)) {
        fprintf(stderr, "[%s] ", name ? name : "?");
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
    }
}

static struct mCore *s_core;
static color_t *s_video;

static struct mCore *boot(const char *rom)
{
    static struct mLogger logger = {.log = log_cb};
    mLogSetDefaultLogger(&logger);
    struct mCore *core = mCoreFind(rom);
    if (!core) {
        fprintf(stderr, "gba_test: %s is not a ROM mGBA knows\n", rom);
        exit(1);
    }
    core->init(core);
    mCoreInitConfig(core, NULL);
    unsigned w, h;
    core->desiredVideoDimensions(core, &w, &h);
    s_video = calloc((size_t)w * h, sizeof(color_t));
    core->setVideoBuffer(core, s_video, w);
    if (!mCoreLoadFile(core, rom)) {
        fprintf(stderr, "gba_test: can't load %s\n", rom);
        exit(1);
    }
    core->reset(core);
    return core;
}

static int save_shot(const char *path)
{
    uint8_t *rgb = malloc(240 * 160 * 3);
    for (int i = 0; i < 240 * 160; i++) {
        uint32_t c = s_video[i]; /* mGBA's 32-bit colour: 0xAABBGGRR */
        rgb[i * 3 + 0] = (uint8_t)(c & 0xFF);
        rgb[i * 3 + 1] = (uint8_t)((c >> 8) & 0xFF);
        rgb[i * 3 + 2] = (uint8_t)((c >> 16) & 0xFF);
    }
    int r = png_write_rgb(path, rgb, 240, 160);
    free(rgb);
    return r;
}

/* The address of a symbol in the ROM's ELF file, or 0. */
static uint32_t elf_symbol(const char *rom, const char *name)
{
    char path[1024];
    const char *dot = strrchr(rom, '.');
    size_t n = dot ? (size_t)(dot - rom) : strlen(rom);
    uint32_t addr = 0;
    snprintf(path, sizeof(path), "%.*s.elf", (int)n, rom);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)size);
    if (fread(d, 1, (size_t)size, f) != (size_t)size) size = 0;
    fclose(f);
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)d;
    if (size < (long)sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS32) {
        free(d);
        return 0;
    }
    const Elf32_Shdr *sh = (const Elf32_Shdr *)(d + eh->e_shoff);
    for (int i = 0; i < eh->e_shnum && !addr; i++) {
        if (sh[i].sh_type != SHT_SYMTAB) continue;
        const Elf32_Sym *sym = (const Elf32_Sym *)(d + sh[i].sh_offset);
        const char *str = (const char *)(d + sh[sh[i].sh_link].sh_offset);
        for (size_t k = 0; k < sh[i].sh_size / sizeof(Elf32_Sym); k++)
            if (!strcmp(str + sym[k].st_name, name)) {
                addr = sym[k].st_value;
                break;
            }
    }
    free(d);
    return addr;
}

static uint32_t parse_keys(const char *s)
{
    static const struct { const char *n; int bit; } K[] = {
        {"A", GBA_KEY_A}, {"B", GBA_KEY_B}, {"SE", GBA_KEY_SELECT}, {"ST", GBA_KEY_START},
        {"RT", GBA_KEY_RIGHT}, {"LT", GBA_KEY_LEFT}, {"U", GBA_KEY_UP}, {"D", GBA_KEY_DOWN},
        {"R", GBA_KEY_R}, {"L", GBA_KEY_L},
    };
    uint32_t keys = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", s);
    for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+"))
        for (size_t i = 0; i < sizeof(K) / sizeof(K[0]); i++)
            if (!strcmp(t, K[i].n)) keys |= 1u << K[i].bit;
    return keys;
}

static int cmd_run(const char *rom, int frames, const char *script, const char *shots)
{
    s_core = boot(rom);
    /* GbaFrames: vblanks, ticks, late (32 bits each), ... */
    uint32_t frames_at = elf_symbol(rom, "g_frames"), late = 0;
    for (int f = 0; f < frames; f++) {
        /* key changes for this frame */
        if (script) {
            const char *p = script;
            while (*p) {
                int at = atoi(p);
                const char *colon = strchr(p, ':');
                if (!colon) break;
                char keys[64];
                size_t n = strcspn(colon + 1, ",");
                if (n >= sizeof(keys)) n = sizeof(keys) - 1;
                memcpy(keys, colon + 1, n);
                keys[n] = 0;
                if (at == f) s_core->setKeys(s_core, parse_keys(keys));
                p = colon + 1 + n;
                if (*p == ',') p++;
            }
        }
        s_core->runFrame(s_core);
        if (frames_at) {
            uint32_t now = s_core->busRead32(s_core, frames_at + 8);
            if (now != late) printf("late: frame %d (+%u)\n", f, now - late);
            late = now;
        }
        if (shots) {
            const char *p = shots;
            while (*p) {
                if (atoi(p) == f) {
                    char name[64];
                    snprintf(name, sizeof(name), "shot_%d.png", f);
                    save_shot(name);
                }
                p = strchr(p, ',');
                if (!p) break;
                p++;
            }
        }
    }
    if (frames_at) printf("late frames: %u\n", late);
    s_core->deinit(s_core);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The emulator test                                                   */
/* ------------------------------------------------------------------ */

/* g_test_info's entries (src/gba/main.c) */
enum { TI_SCREEN, TI_FADE, TI_SEL_LEVEL, TI_PHASE, TI_PLAYER, TI_PROGRESS, TI_PAUSED, TI_RESULTS_SEL,
       TI_MENU_SEL, TI_LEVEL_IDX, TI_PRACTICE, TI_PHASE_T, TI_SEL_SCROLL, TI_SONG_POS, TI_AUDIO_STAT,
       TI_SPEAKER, TI_OPTIONS_SEL, TI_PLAYER_SIZE, TI_PROGRESS_SIZE, TI_COUNT };
static uint32_t s_ti[TI_COUNT], s_frames_at, s_cv_at, s_stale_at, s_oam_at, s_vframe_at;
static long s_frame; /* frames run since the ROM started */
static const char *s_shots;

static void rd(uint32_t addr, void *out, size_t n)
{
    for (size_t i = 0; i < n; i++) ((uint8_t *)out)[i] = (uint8_t)s_core->busRead8(s_core, addr + (uint32_t)i);
}

static int32_t rd32(int what) { return (int32_t)s_core->busRead32(s_core, s_ti[what]); }
static int32_t rd32_at(uint32_t addr) { return (int32_t)s_core->busRead32(s_core, addr); }
static float rdf(int what)
{
    uint32_t v = s_core->busRead32(s_core, s_ti[what]);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static uint32_t late(void) { return s_core->busRead32(s_core, s_frames_at + 8); }
/* the latest frame's work in scanlines (GbaFrames.work) */
static int work(void) { return (int)s_core->busRead16(s_core, s_frames_at + 16); }

/* Late frames, by where they were (the screen, the fade, the run's phase,
 * the pause): counted as they happen (GBA_TEST_LATE=1 lists them, and
 * GBA_TEST_HEAVY=n the frames of n scanlines' work or more). GBA_TEST_FRAME_STATE=f saves the
 * emulator's state in gba_test.state before frame f (to profile it; the
 * first time: a restart counts from 0 again), and the keys from there on as
 * a run script on stderr. */
#define LATE_KINDS 32
static struct {
    char what[48];
    unsigned n;
    long first;
    int work;
} s_late[LATE_KINDS];
static int s_nlate;
static uint32_t s_late_seen;
static unsigned s_late_total; /* (over the restarts too) */
static int s_most[5];         /* the most work in a frame, by screen */
static int s_heavy_seen;
static long s_state_frame = -1;

static void save_state_file(void);

/* Where each vertical blank pointed the sound's DMAs at the next buffers
 * (audio_gba.c's g_audio_stat): the samples played in the frame by then,
 * against its end, by the mix (the headphones', the speaker's). The DMAs
 * have read the last buffers to the end from 15 samples before it (the
 * request after the sample 16 n + 1 that reads the last 16 bytes) and ask
 * for the next 16 bytes 1 after it (audio_gba.c's top, as mGBA does it):
 * between those is fine. A restart this side of SOUND_EARLY or that side of
 * SOUND_LATE (the hardware's FIFO a sample or two otherwise) fails. */
#define SOUND_EARLY (-12)
#define SOUND_LATE 0
static struct {
    int min, max;
    long n, bad, first_bad;
} s_snd[2] = {{999, -999, 0, 0, -1}, {999, -999, 0, 0, -1}};
static uint32_t s_snd_count;

static void note_sound(void)
{
    uint32_t at = s_ti[TI_AUDIO_STAT], count = s_core->busRead32(s_core, at + 4);
    int played = s_core->busRead16(s_core, at), frame = s_core->busRead16(s_core, at + 2), m, o;
    if (count == s_snd_count || !frame) return;
    s_snd_count = count;
    m = frame == 352 ? 0 : 1;
    o = played % frame;
    if (o > frame / 2) o -= frame;
    if (o < s_snd[m].min) s_snd[m].min = o;
    if (o > s_snd[m].max) s_snd[m].max = o;
    s_snd[m].n++;
    if (o < SOUND_EARLY || o > SOUND_LATE) {
        if (!s_snd[m].bad++) s_snd[m].first_bad = s_frame;
    }
}

static int s_irq_max; /* (over the restarts too) */

static void note_late(void)
{
    uint32_t l = late();
    char what[48];
    int i, screen, phase;
    float fade;
    if (s_ti[TI_SCREEN] && work() > s_most[rd32(TI_SCREEN) % 5]) s_most[rd32(TI_SCREEN) % 5] = work();
    if (s_frames_at && (int)s_core->busRead16(s_core, s_frames_at + 26) > s_irq_max) {
        s_irq_max = s_core->busRead16(s_core, s_frames_at + 26);
        if (getenv("GBA_TEST_IRQ"))
            printf("  (interrupt: %d scanlines at frame %ld, screen %d, phase %d, paused %d)\n", s_irq_max, s_frame,
                   rd32(TI_SCREEN), rd32(TI_PHASE), rd32(TI_PAUSED));
    }
    if (getenv("GBA_TEST_HEAVY") && s_ti[TI_SCREEN] && work() >= atoi(getenv("GBA_TEST_HEAVY")) && work() != s_heavy_seen)
        printf("  (heavy: frame %ld, screen %d, fade %.2f, phase %d, paused %d, work %d)\n", s_frame - 1, rd32(TI_SCREEN),
               (double)rdf(TI_FADE), rd32(TI_PHASE), rd32(TI_PAUSED), work());
    s_heavy_seen = work();
    if (l == s_late_seen || !s_ti[TI_SCREEN]) {
        s_late_seen = l;
        return;
    }
    s_late_total += l - s_late_seen;
    screen = rd32(TI_SCREEN);
    fade = rdf(TI_FADE);
    phase = rd32(TI_PHASE);
    snprintf(what, sizeof(what), "%s%s%s", (const char *[]){"title", "select", "play", "garage", "options"}[screen % 5],
             fade >= 1.0f ? ", black" : (fade > 0.0f ? ", fading" : ""),
             screen != SCR_PLAY ? "" : rd32(TI_PAUSED) ? ", paused" : (const char *[]){", run", ", dead", ", results"}[phase % 3]);
    for (i = 0; i < s_nlate && strcmp(s_late[i].what, what); i++) {}
    if (i == s_nlate && s_nlate < LATE_KINDS) {
        snprintf(s_late[s_nlate].what, sizeof(s_late[0].what), "%s", what);
        s_late[s_nlate].first = s_frame - 1;
        s_nlate++;
    }
    if (getenv("GBA_TEST_LATE")) printf("  (late: frame %ld, %s, work %d)\n", s_frame - 1, what, work());
    if (i < s_nlate) {
        s_late[i].n += l - s_late_seen;
        if (work() > s_late[i].work) s_late[i].work = work();
    }
    s_late_seen = l;
}

static void frame(uint32_t keys)
{
    static uint32_t prev = ~0u;
    static long keys_from = -1;
    if (s_frame == s_state_frame) {
        /* (once: the restarts count their frames from 0 again) */
        save_state_file();
        keys_from = s_frame;
        s_state_frame = -1;
    }
    if (keys_from >= 0 && (s_frame < keys_from || s_frame >= keys_from + 64)) keys_from = -1;
    if (keys_from >= 0 && (keys != prev || s_frame == keys_from)) {
        /* (the keys from there, as a gba_test run script: on stderr) */
        static const char *const N[] = {"A", "B", "SE", "ST", "RT", "LT", "U", "D", "R", "L"};
        int k, n = 0;
        fprintf(stderr, "%ld:", s_frame - keys_from);
        for (k = 0; k < 10; k++)
            if (keys >> k & 1) fprintf(stderr, "%s%s", n++ ? "+" : "", N[k]);
        fprintf(stderr, ",");
    }
    prev = keys;
    s_core->setKeys(s_core, keys);
    s_core->runFrame(s_core);
    s_frame++;
    note_late();
    note_sound();
}

static void frames(int n)
{
    while (n-- > 0) frame(0);
}

/* a key pressed for a few frames, then released */
static void press(uint32_t key)
{
    frame(key);
    frame(key);
    frame(key);
    frames(3);
}

/* frames until the screen is `screen` and shown (not fading, unless
 * fading_too), at most `max` */
static int wait_screen_f(int screen, int max, int fading_too)
{
    while (max-- > 0) {
        if (rd32(TI_SCREEN) == screen && (fading_too || rdf(TI_FADE) <= 0.0f)) return 1;
        frame(0);
    }
    return 0;
}

static int wait_screen(int screen, int max)
{
    return wait_screen_f(screen, max, 0);
}

static void shot(const char *name, int level)
{
    char path[1024];
    if (!s_shots) return;
    snprintf(path, sizeof(path), "%s/level%d_%s.png", s_shots, level, name);
    save_shot(path);
}

static int boot_test(const char *rom)
{
    uint32_t info = elf_symbol(rom, "g_test_info");
    s_core = boot(rom);
    s_frames_at = elf_symbol(rom, "g_frames");
    s_cv_at = elf_symbol(rom, "s_cv");
    s_stale_at = elf_symbol(rom, "s_stale");
    s_oam_at = elf_symbol(rom, "g_oam");
    s_vframe_at = elf_symbol(rom, "s_vframe");
    if (!info || !s_frames_at) {
        fprintf(stderr, "gba_test: no g_test_info or g_frames in the ROM's ELF file\n");
        return 0;
    }
    for (int i = 0; i < TI_COUNT; i++) s_ti[i] = s_core->busRead32(s_core, info + (uint32_t)i * 4);
    if (s_ti[TI_PLAYER_SIZE] != sizeof(Player) || s_ti[TI_PROGRESS_SIZE] != sizeof(Progress)) {
        fprintf(stderr, "gba_test: the ROM's Player or Progress is not laid out as here (%u, %u bytes)\n",
                (unsigned)s_ti[TI_PLAYER_SIZE], (unsigned)s_ti[TI_PROGRESS_SIZE]);
        return 0;
    }
    s_frame = 0;
    s_late_seen = late();
    return 1;
}

/* The player's sprite in the frame just made (video.c's g_oam: attr0-2
 * and a quarter of an affine matrix, 4 halfwords a sprite): the one shown
 * of the vehicles' tiles (art.h PT_*) in the player's palette (0). Its x
 * on the screen. 0 if there is none. */
static int player_sprite(int *x)
{
    uint16_t oam[128 * 4];
    if (!s_oam_at) return 0;
    rd(s_oam_at, oam, sizeof(oam));
    for (int i = 0; i < 128; i++) {
        uint16_t a0 = oam[i * 4], a1 = oam[i * 4 + 1], a2 = oam[i * 4 + 2];
        if ((a0 & 0x0300) == 0x0200 || (a2 & 0x3FF) >= PLAYER_TILES || (a2 >> 12) != 0) continue;
        *x = a1 & 0x1FF;
        if (*x >= 256) *x -= 512;
        return 1;
    }
    return 0;
}

/* BG0's text surface (ui.c's s_cv: 160 lines of 512 pixels, a byte each,
 * palette bank << 4 | colour): in every 8x8 cell the colours from 4 up
 * are of one bank, as a tile shows one (1-3 are the same in all); a cell
 * that mixes two shows some of its text in the wrong colours. */
static int banks_ok(const char *where)
{
    static uint8_t cv[160][512];
    uint32_t stale[160 / 8][2] = {{0}};
    int bad = 0, bx = 0, by = 0;
    if (!s_cv_at) return 1;
    rd(s_cv_at, cv, sizeof(cv));
    if (s_stale_at) rd(s_stale_at, stale, sizeof(stale));
    for (int cy = 0; cy < 160 / 8; cy++)
        for (int cx = 0; cx < 512 / 8; cx++) {
            int bank = -1, mixed = 0;
            /* (a stale cell's pixels are not shown: emptied or filled
             * when next drawn into) */
            if (stale[cy][cx >> 5] >> (cx & 31) & 1) continue;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int p = cv[cy * 8 + y][cx * 8 + x];
                    if ((p & 15) < 4) continue;
                    if (bank < 0) bank = p >> 4;
                    else if (p >> 4 != bank) mixed = 1;
                }
            if (mixed && !bad++) {
                bx = cx * 8;
                by = cy * 8;
            }
        }
    if (bad) {
        printf("%s: FAIL, %d cells of BG0 mix two palette banks (the first at %d,%d)\n", where, bad, bx, by);
        if (s_shots) {
            /* the whole surface, in the palettes' colours as they are */
            char path[1024];
            uint8_t *rgb = malloc(512 * 160 * 3);
            for (int i = 0; i < 512 * 160; i++) {
                uint16_t c = s_core->busRead16(s_core, 0x05000000u + ((const uint8_t *)cv)[i] * 2u);
                rgb[i * 3 + 0] = (uint8_t)((c & 31) << 3);
                rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 31) << 3);
                rgb[i * 3 + 2] = (uint8_t)(((c >> 10) & 31) << 3);
            }
            snprintf(path, sizeof(path), "%s/bg0_%ld.png", s_shots, s_frame);
            png_write_rgb(path, rgb, 512, 160);
            free(rgb);
            printf("  (BG0's surface in %s)\n", path);
        }
    }
    return !bad;
}

/* the garage, the options and the garage again from the title, and back:
 * their text's banks */
static int tour_menus(void)
{
    int ok = 1;
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    frames(10);
    ok &= banks_ok("title");
    while (rd32(TI_MENU_SEL) != 0) press(1u << GBA_KEY_LEFT);
    press(1u << GBA_KEY_A);
    if (!wait_screen(SCR_GARAGE, 600)) return 0;
    frames(10);
    ok &= banks_ok("garage");
    press(1u << GBA_KEY_DOWN);
    frames(10);
    ok &= banks_ok("garage, colour 1");
    press(1u << GBA_KEY_DOWN);
    frames(10);
    ok &= banks_ok("garage, colour 2");
    /* (back to the icon's row: the garage keeps the row it was left on,
     * and opens on it again for the second visit below) */
    press(1u << GBA_KEY_UP);
    press(1u << GBA_KEY_UP);
    press(1u << GBA_KEY_B);
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    while (rd32(TI_MENU_SEL) != 2) press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_A);
    if (!wait_screen(SCR_OPTIONS, 600)) return 0;
    frames(10);
    ok &= banks_ok("options");
    press(1u << GBA_KEY_DOWN);
    press(1u << GBA_KEY_DOWN);
    frames(10);
    ok &= banks_ok("options, output");
    /* the speaker's mix: the output restarted at its rate within 6
     * frames (the sound fading out, a frame of silence), the menu's song
     * going on (not back) */
    {
        int32_t pos0 = rd32(TI_SONG_POS), pos1;
        int frame = 0, n;
        press(1u << GBA_KEY_RIGHT);
        for (n = 0; n < 8 && frame != 224; n++) {
            frames(1);
            frame = s_core->busRead16(s_core, s_ti[TI_AUDIO_STAT] + 2);
        }
        frames(30);
        pos1 = rd32(TI_SONG_POS);
        if (s_core->busRead8(s_core, s_ti[TI_SPEAKER]) != 1 || frame != 224 || pos1 * 352 / 224 < pos0) {
            printf("options: FAIL, OUTPUT did not change to the speaker's mix (speaker %d, frame %d, song at %d then %d)\n",
                   s_core->busRead8(s_core, s_ti[TI_SPEAKER]), frame, pos0, pos1);
            ok = 0;
        }
    }
    ok &= banks_ok("options, output on the speaker");
    press(1u << GBA_KEY_DOWN);
    frames(30);
    ok &= banks_ok("options, audio delay");
    press(1u << GBA_KEY_UP);
    press(1u << GBA_KEY_LEFT);
    frames(10);
    /* the mixer at its busiest: both volumes at 10 (no stored-as-is
     * copying), the sound effects overlapping as the choice moves every
     * other frame, and the songs changing (fades) as it passes AUDIO
     * DELAY: no frame late (checked as every frame) */
    {
        int k;
        for (k = 0; k < 4 && rd32(TI_OPTIONS_SEL) != 0; k++) press(1u << GBA_KEY_UP);
        press(1u << GBA_KEY_RIGHT);
        press(1u << GBA_KEY_RIGHT);
        press(1u << GBA_KEY_DOWN);
        press(1u << GBA_KEY_RIGHT);
        press(1u << GBA_KEY_RIGHT);
        for (k = 0; k < 120; k++) frame(k % 4 < 2 ? 1u << (k % 24 < 12 ? GBA_KEY_DOWN : GBA_KEY_UP) : 0);
        frames(10);
        for (k = 0; k < 6 && rd32(TI_OPTIONS_SEL) != 0; k++) press(1u << GBA_KEY_UP);
        press(1u << GBA_KEY_LEFT);
        press(1u << GBA_KEY_LEFT);
        press(1u << GBA_KEY_DOWN);
        press(1u << GBA_KEY_LEFT);
        press(1u << GBA_KEY_LEFT);
        for (k = 0; k < 6 && rd32(TI_OPTIONS_SEL) != 2; k++) press(1u << GBA_KEY_DOWN);
    }
    if (s_core->busRead8(s_core, s_ti[TI_SPEAKER]) != 0 || s_core->busRead16(s_core, s_ti[TI_AUDIO_STAT] + 2) != 352) {
        printf("options: FAIL, OUTPUT did not change back to the headphones' mix\n");
        ok = 0;
    }
    press(1u << GBA_KEY_B);
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    /* the garage a second time, on the icon's row, over what the title
     * and the options left: its first frame was late when it drew two
     * rows (every frame is checked) */
    while (rd32(TI_MENU_SEL) != 0) press(1u << GBA_KEY_LEFT);
    press(1u << GBA_KEY_A);
    if (!wait_screen(SCR_GARAGE, 600)) return 0;
    frames(10);
    ok &= banks_ok("garage, a second time");
    press(1u << GBA_KEY_B);
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    return ok;
}

/* The options' OUTPUT set (from the title or the level select, back to the
 * title): the speaker's mix if speaker; 1 if the sound changed to it. */
static int set_output(int speaker)
{
    int frame = 0, n;
    if (rd32(TI_SCREEN) == SCR_SELECT) press(1u << GBA_KEY_B);
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    while (rd32(TI_MENU_SEL) != 2) press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_A);
    if (!wait_screen(SCR_OPTIONS, 600)) return 0;
    for (n = 0; n < 8 && rd32(TI_OPTIONS_SEL) != 2; n++) press(1u << GBA_KEY_DOWN);
    if ((int)s_core->busRead8(s_core, s_ti[TI_SPEAKER]) != speaker) press(1u << GBA_KEY_RIGHT);
    for (n = 0; n < 30 && frame != (speaker ? 224 : 352); n++) {
        frames(1);
        frame = s_core->busRead16(s_core, s_ti[TI_AUDIO_STAT] + 2);
    }
    /* and both volumes at 10 (the mixer's slower, scaling path in every
     * frame of the runs) */
    for (n = 0; n < 6 && rd32(TI_OPTIONS_SEL) != 0; n++) press(1u << GBA_KEY_UP);
    press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_DOWN);
    press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_RIGHT);
    press(1u << GBA_KEY_B);
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    return frame == (speaker ? 224 : 352) && (int)s_core->busRead8(s_core, s_ti[TI_SPEAKER]) == speaker;
}

/* from the title or the level select, into level idx (normal mode) */
static int start_level(int idx)
{
    if (!wait_screen(rd32(TI_SCREEN) == SCR_SELECT ? SCR_SELECT : SCR_TITLE, 600)) return 0;
    if (rd32(TI_SCREEN) == SCR_TITLE) {
        while (rd32(TI_MENU_SEL) != 1) press(1u << GBA_KEY_RIGHT);
        press(1u << GBA_KEY_A);
        if (!wait_screen(SCR_SELECT, 600)) return 0;
    }
    for (int tries = 0; rd32(TI_SEL_LEVEL) != idx && tries < 32; tries++) {
        press(1u << (rd32(TI_SEL_LEVEL) < idx ? GBA_KEY_RIGHT : GBA_KEY_LEFT));
        frames(10);
    }
    if (rd32(TI_SEL_LEVEL) != idx) return 0;
    for (int n = 0; n < 120 && fabsf(rdf(TI_SEL_SCROLL) - (float)idx) > 0.01f; n++) frame(0);
    frames(4);
    if (!banks_ok("level select")) return 0;
    press(1u << GBA_KEY_A);
    /* (the run starts as the screen fades in: the frame after the level is
     * set, the test is on time for its first tick) */
    for (int n = 0; n < 600; n++) {
        Player p;
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        /* (the level set, and the player reset: not the last run's) */
        if (rd32(TI_SCREEN) == SCR_PLAY && rd32(TI_LEVEL_IDX) == idx && !p.done && !p.dead) return !rd32(TI_PRACTICE);
        frame(0);
    }
    return 0;
}

/* GBA_TEST_STATE=level:tick: the emulator's state as that tick is about
 * to run, in gba_test.state (for profiling a slow frame) */
static int s_state_level = -1, s_state_tick = -1;

static void save_state_file(void)
{
    size_t n = s_core->stateSize(s_core);
    void *st = malloc(n);
    FILE *f = fopen("gba_test.state", "wb");
    if (st && f && s_core->saveState(s_core, st)) fwrite(st, 1, n, f);
    if (f) fclose(f);
    free(st);
    printf("  (state saved before frame %ld)\n", s_frame);
}

static void save_state(void)
{
    save_state_file();
    printf("  (that is, before tick %d)\n", s_state_tick);
}

static void print_player(const char *who, const Player *p)
{
    printf("    %s: x %d y %d vy %d mode %d grav %d grounded %d dead %d done %d coins %d ticks %u jumps %u\n", who,
           p->x, p->y, p->vy, p->mode, p->grav, p->grounded, p->dead, p->done, p->coins, p->ticks, p->jumps);
}

/* The run against its song (audio_gba.c's g_audio_emit, in samples, 352 a
 * frame; the speaker's mix 224): within an attempt the song never goes
 * back (a part heard twice), and from tick 30 on (the level's first frames
 * done) it is as far ahead of the run's ticks in every attempt of every
 * level played in that mix (a run a tick or more out of time with its
 * music). */
#define PAUSE_FRAMES 40
static int32_t s_sync_lead[2], s_sync_prev;
static int s_sync_known[2], s_sync_tick;

static int sync_ok(int idx, int tick)
{
    int32_t pos = (int32_t)s_core->busRead32(s_core, s_ti[TI_SONG_POS]), lead;
    int per_tick = s_core->busRead16(s_core, s_ti[TI_AUDIO_STAT] + 2), m = per_tick != 352;
    /* (from the attempt's first tick on: before it, the menu's song may
     * still be playing) */
    if (tick >= s_sync_tick && s_sync_tick > 0 && pos < s_sync_prev) {
        printf("level %d: FAIL, the song went back %d samples at tick %d (heard twice)\n", idx,
               (int)(s_sync_prev - pos), tick);
        return 0;
    }
    s_sync_prev = pos;
    s_sync_tick = tick;
    if (tick < 30) return 1;
    lead = pos - tick * per_tick;
    if (!s_sync_known[m]) {
        s_sync_known[m] = 1;
        s_sync_lead[m] = lead;
    } else if (lead != s_sync_lead[m]) {
        printf("level %d: FAIL, the run is %+.2f ticks out of time with its song at tick %d\n", idx,
               (double)(s_sync_lead[m] - lead) / per_tick, tick);
        return 0;
    }
    return 1;
}

/* An attempt that dies: the solver's presses until tick `upto`, then
 * none; then the attempt after it, from the level's start (the song
 * starting again, the camera going back). Returns 1 if all was well. */
static int die_once(int idx, const uint8_t *held, int upto)
{
    Player p;
    int n;
    for (n = 0; n < 60 * 300; n++) {
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if (p.dead || p.done) break;
        frame(p.ticks < upto && held[p.ticks] ? 1u << GBA_KEY_A : 0);
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if (!sync_ok(idx, (int)p.ticks)) return 0;
    }
    if (!p.dead) {
        printf("level %d: FAIL, the attempt meant to die did not (done %d, tick %u)\n", idx, p.done, p.ticks);
        return 0;
    }
    for (n = 0; n < 600 && p.dead; n++) {
        frame(0);
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
    }
    if (p.dead) {
        printf("level %d: FAIL, no attempt after the death\n", idx);
        return 0;
    }
    shot("respawn", idx);
    return 1;
}

/* The pause menu and the results appear whole, in one frame (they take
 * several to draw: hud.c draws them out of sight). Their panel's rectangle
 * on the screen (its rounded corners left out, and the rows [skip0, skip1),
 * the results' coins, sprites that turn), in each of the frames from the
 * one they were called up in: it is either what the last of them shows
 * (all of it) or for the most part not (the run behind, before it shows);
 * a frame that has half of it or more and not all of it shows it in parts. */
#define WHOLE_MAX 200
static uint16_t *s_whole[WHOLE_MAX]; /* (the GBA's 15-bit colours) */
static int s_nwhole, s_whole_px, s_wx0, s_wy0, s_wx1, s_wy1, s_wskip0, s_wskip1;

static void whole_begin(int x0, int y0, int x1, int y1, int skip0, int skip1)
{
    s_nwhole = s_whole_px = 0;
    s_wx0 = x0, s_wy0 = y0, s_wx1 = x1, s_wy1 = y1, s_wskip0 = skip0, s_wskip1 = skip1;
}

static void whole_frame(void)
{
    int n = 0;
    if (s_nwhole >= WHOLE_MAX) return;
    if (!s_whole[s_nwhole]) s_whole[s_nwhole] = malloc(240 * 160 * sizeof(uint16_t));
    for (int y = s_wy0; y < s_wy1; y++)
        for (int x = s_wx0; x < s_wx1; x++) {
            if (y >= s_wskip0 && y < s_wskip1) continue;
            if ((x - s_wx0 < 3 || s_wx1 - 1 - x < 3) && (y - s_wy0 < 3 || s_wy1 - 1 - y < 3)) continue;
            uint32_t c = s_video[y * 240 + x]; /* 0xAABBGGRR */
            s_whole[s_nwhole][n++] = (uint16_t)((c >> 3 & 31) | (c >> 11 & 31) << 5 | (c >> 19 & 31) << 10);
        }
    s_whole_px = n;
    s_nwhole++;
}

static int whole_ok(int idx, const char *what, const char *file)
{
    const uint16_t *last = s_whole[s_nwhole - 1];
    int shown = 0;
    for (int i = 0; i < s_nwhole; i++) {
        int same = 0;
        for (int k = 0; k < s_whole_px; k++) same += s_whole[i][k] == last[k];
        if (same == s_whole_px) {
            shown = 1;
        } else if (shown || same * 2 >= s_whole_px) {
            printf("level %d: FAIL, %s shown in parts: in frame %d after it was called up, %d%% of it%s\n", idx,
                   what, i, (int)((long)same * 100 / s_whole_px), shown ? " (after all of it)" : "");
            if (s_shots) {
                char path[1024];
                snprintf(path, sizeof(path), "%s/level%d_%s_in_parts.png", s_shots, idx, file);
                save_shot(path);
            }
            return 0;
        }
    }
    return 1;
}

/* Plays level idx with the solver's inputs, after an attempt that dies at
 * the solver's presses' end at tick `die_at`; returns 1 if all was well. */
static int play_level(int idx, int die_at)
{
    static uint8_t held[MAX_TICKS];
    Level *L = level_parse(g_levels[idx].src);
    LevelInfo info;
    Player ref, p;
    int t = -1, ok = 1, prev = 0, mid_shot = 0, wmax = 0, wmax_tick = 0;
    /* the player's sprite: where on the screen in each vehicle (the camera
     * follows it: always there), and how long the cube has been down */
    int spr_x[8], on_ground = 0;
    for (int k = 0; k < 8; k++) spr_x[k] = -1000;
    /* a pause a third of the way in, for PAUSE_FRAMES, where the solver
     * presses nothing for a while (pause: 0 before, 1 paused, 2 after);
     * closed with START, or in every other level with A (the menu's
     * RESUME), held a few frames longer: it must not jump */
    int pause = 0, pause_frames = 0, pause_at = -1, resume_a = idx & 1, a_frames = 0;
    long wsum = 0, wn = 0;
    uint32_t late0, late_before = 0;
    level_info(idx, &info);
    for (int K = 3; K >= 1 && t < 0; K--) t = solve(L, K, 0, NULL);
    if (t < 0) {
        printf("level %d (%s): FAIL, the solver finds no way through\n", idx, info.name);
        level_free(L);
        return 0;
    }
    memcpy(held, g_sol, sizeof(held));
    for (int k = t / 3; k < t - 20 && pause_at < 0; k++) {
        int quiet = 1;
        for (int j = 0; j < 12; j++) quiet &= !held[k + j];
        if (quiet) pause_at = k;
    }
    if (!start_level(idx)) {
        printf("level %d (%s): FAIL, could not start it from the menus (frame %ld: screen %d, level %d, fade %.2f)\n",
               idx, info.name, s_frame, rd32(TI_SCREEN), rd32(TI_SEL_LEVEL), (double)rdf(TI_FADE));
        shot("menus", idx);
        level_free(L);
        return 0;
    }
    if (die_at > 0 && !die_once(idx, held, die_at)) {
        level_free(L);
        return 0;
    }
    sim_reset(&ref, L);
    /* the ticks run before this frame: no input in them */
    rd(s_ti[TI_PLAYER], &p, sizeof(p));
    for (int k = 0; k < p.ticks; k++) {
        if (held[k]) {
            printf("level %d: FAIL, the run began before the test could press (tick %d of %d)\n", idx, k, p.ticks);
            level_free(L);
            return 0;
        }
        sim_tick(&ref, L, 0, 0);
    }
    shot("start", idx);
    late0 = late();
    for (;;) {
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if (p.done || p.dead || p.ticks >= MAX_TICKS - 1) break;
        {
            /* this frame's tick is the attempt's p.ticks-th: its input */
            int tick = p.ticks, h = held[tick];
            uint32_t keys = h ? 1u << GBA_KEY_A : 0;
            if (s_state_level == idx && tick == s_state_tick) save_state();
            if (pause == 0 && tick == pause_at) {
                /* START: the pause menu (its frames may be late: nothing runs) */
                keys = 1u << GBA_KEY_START;
                pause = 1;
                late_before = late() - late0;
                /* (the pause panel, hud.c's P_X0..P_Y1) */
                whole_begin(45, 21, 195, 142, 0, 0);
            } else if (pause == 1) {
                keys = 0;
                if (++pause_frames == PAUSE_FRAMES) {
                    if (!rd32(TI_PAUSED)) {
                        printf("level %d: FAIL, START did not pause the run at tick %d\n", idx, tick);
                        ok = 0;
                        break;
                    }
                    if (!banks_ok("pause menu") || !whole_ok(idx, "the pause menu", "pause")) {
                        ok = 0;
                        break;
                    }
                    /* START again, or A: the run goes on, from the next frame on time */
                    keys = 1u << (resume_a ? GBA_KEY_A : GBA_KEY_START);
                    a_frames = resume_a ? 3 : 0;
                    pause = 2;
                    late0 = late();
                }
            } else if (pause == 2 && a_frames > 0) {
                keys |= 1u << GBA_KEY_A; /* (not let go yet) */
                a_frames--;
            }
            frame(keys);
            if (pause == 1) whole_frame();
            rd(s_ti[TI_PLAYER], &p, sizeof(p));
            if (p.ticks == tick) {
                if (h && tick > 0) {
                    printf("level %d: FAIL, no tick in frame %ld (tick %d) with the button down\n", idx, s_frame, tick);
                    ok = 0;
                    break;
                }
                continue; /* not running yet */
            }
            /* (more than one tick: a frame after a long one, the level's
             * loading; the frame's input was in all of them) */
            for (int k = tick; k < p.ticks; k++) {
                if (held[k] != h) {
                    printf("level %d: FAIL, ticks %d-%d in one frame, their inputs not all the same\n", idx, tick,
                           p.ticks - 1);
                    ok = 0;
                    break;
                }
                sim_tick(&ref, L, h, h && !prev);
                prev = h;
            }
            if (!ok) break;
            if (p.ticks - tick > 1) printf("  (%d ticks in the frame at tick %d)\n", p.ticks - tick, tick);
            if (tick >= 30) {
                /* (the frame's work, done after its tick: shown in the next frame) */
                int w = work();
                wsum += w;
                wn++;
                if (w > wmax) {
                    wmax = w;
                    wmax_tick = tick;
                }
            }
            if (tick < 30) late0 = late(); /* (the level's start, as it fades in) */
            if (!sync_ok(idx, p.ticks)) {
                ok = 0;
                break;
            }
            if (memcmp(&ref, &p, sizeof(p))) {
                printf("level %d: FAIL, the player differs from the reference after tick %d\n", idx, tick);
                print_player("ROM", &p);
                print_player("reference", &ref);
                ok = 0;
                break;
            }
            {
                int x;
                if (rd32(TI_PHASE) == 0 && player_sprite(&x)) {
                    if (spr_x[p.mode & 7] == -1000) spr_x[p.mode & 7] = x;
                    if (x != spr_x[p.mode & 7]) {
                        printf("level %d: FAIL, the player's sprite moved on the screen from x %d to %d at tick %d "
                               "(the camera follows it)\n", idx, spr_x[p.mode & 7], x, tick);
                        ok = 0;
                        break;
                    }
                    /* a cube on the ground, its turn done: drawn square (its
                     * frame a whole quarter turn: sprites.c s_vframe) */
                    on_ground = p.mode == MODE_CUBE && p.grounded ? on_ground + 1 : 0;
                    if (on_ground >= 4 && s_vframe_at && rd32_at(s_vframe_at) % (VF_CUBE / 4)) {
                        printf("level %d: FAIL, the cube on the ground at tick %d is drawn turned (frame %d of %d)\n",
                               idx, tick, (int)rd32_at(s_vframe_at), VF_CUBE);
                        ok = 0;
                        break;
                    }
                }
            }
            if (!mid_shot && p.ticks >= t / 2) {
                mid_shot = 1;
                shot("middle", idx);
            }
        }
    }
    if (ok && !p.done) {
        printf("level %d: FAIL, the run ended at tick %u without finishing (dead %d)\n", idx, p.ticks, p.dead);
        ok = 0;
    }
    if (ok && p.ticks != t) {
        printf("level %d: FAIL, finished after %u ticks, the reference after %d\n", idx, p.ticks, t);
        ok = 0;
    }
    if (ok && pause != 2) {
        printf("level %d: FAIL, the run was not paused and resumed (tick %d)\n", idx, pause_at);
        ok = 0;
    }
    if (ok && late() - late0 + late_before) {
        printf("level %d: FAIL, %u frames of the run were late\n", idx, late() - late0 + late_before);
        ok = 0;
    }
    if (ok) {
        /* the results, the progress, and back to the level select */
        Progress pr;
        /* (the results' panel, hud.c's R_X0..R_Y1, but its coins) */
        whole_begin(52, 46, 187, 135, 87, 104);
        for (int k = 0; k < 180; k++) {
            frame(0);
            whole_frame();
        }
        shot("results", idx);
        if (!banks_ok("results") || !whole_ok(idx, "the results", "results")) ok = 0;
        rd(s_ti[TI_PROGRESS], &pr, sizeof(pr));
        if (rd32(TI_PHASE) != PH_COMPLETE || pr.best[idx] != 100) {
            printf("level %d: FAIL, no results or no best of 100%% after the finish (phase %d, best %d)\n", idx,
                   rd32(TI_PHASE), pr.best[idx]);
            ok = 0;
        }
        while (rd32(TI_RESULTS_SEL) != 0) press(1u << GBA_KEY_LEFT);
        press(1u << GBA_KEY_A);
        if (ok && !wait_screen(SCR_SELECT, 600)) {
            printf("level %d: FAIL, MENU did not lead to the level select\n", idx);
            ok = 0;
        }
    }
    if (ok)
        printf("level %d (%s): ok, %d ticks as the reference, no frame late; a frame's work %ld / %d scanlines "
               "(mean / most, at tick %d) of 228\n",
               idx, info.name, t, wn ? wsum / wn : 0, wmax, wmax_tick);
    level_free(L);
    return ok;
}

/* save_gba.c's two copies: slots of SAVE_SLOT bytes, a header (tag, sequence
 * number, length, checksum) then the data */
#define SAVE_SLOT 0x200
#define SAVE_HEADER 16

static uint32_t le32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* the slot holding the latest save, by its tag and sequence number */
static int latest_slot(const uint8_t *save, size_t size)
{
    int best = -1;
    for (int k = 0; k < 2; k++) {
        const uint8_t *h = save + k * SAVE_SLOT;
        if ((size_t)(k + 1) * SAVE_SLOT > size || memcmp(h, "PDA1", 4)) continue;
        if (best < 0 || (int32_t)(le32(h + 4) - le32(save + best * SAVE_SLOT + 4)) > 0) best = k;
    }
    return best;
}

/* boot the ROM (core loaded by boot_test) with this SRAM: how many of the
 * levels played it has as finished */
static int finished_after_restart(void *save, size_t size, const int *list, int n)
{
    Progress pr;
    int done = 0;
    s_core->savedataRestore(s_core, save, size, false);
    s_core->reset(s_core);
    frames(60);
    rd(s_ti[TI_PROGRESS], &pr, sizeof(pr));
    for (int i = 0; i < n; i++) done += pr.best[list[i]] == 100;
    return done;
}

static int cmd_play(const char *rom, const char *levels, const char *shots)
{
    int fails = 0, n = 0, list[PROGRESS_LEVELS];
    void *save = NULL;
    size_t save_size;
    int latest;
    s_shots = shots;
    if (getenv("GBA_TEST_STATE")) sscanf(getenv("GBA_TEST_STATE"), "%d:%d", &s_state_level, &s_state_tick);
    if (getenv("GBA_TEST_FRAME_STATE")) s_state_frame = atol(getenv("GBA_TEST_FRAME_STATE"));
    if (levels) {
        for (const char *p = levels; *p && n < PROGRESS_LEVELS; p = strchr(p, ',') ? strchr(p, ',') + 1 : "")
            list[n++] = atoi(p);
    } else {
        for (n = 0; n < g_level_count && n < PROGRESS_LEVELS; n++) list[n] = n;
    }
    if (!boot_test(rom)) return 1;
    frames(30);
    if (tour_menus()) {
        printf("menus: ok, the title, the garage and the options show their text in its colours\n");
    } else {
        printf("menus: FAIL, in the title, the garage or the options (screen %d)\n", rd32(TI_SCREEN));
        fails++;
        s_core->deinit(s_core);
        if (!boot_test(rom)) return 1;
        frames(30);
    }
    for (int i = 0; i < n; i++) {
        /* (the second half of the levels with the speaker's mix and the
         * volumes at 10: its mixing and the scaling path, under a run's
         * load, on time) */
        if (i == n / 2 && n > 1) {
            if (set_output(1)) {
                printf("output: ok, the speaker's mix and both volumes at 10 for the levels from here\n");
            } else {
                printf("output: FAIL, could not change to the speaker's mix (frame %ld)\n", s_frame);
                fails++;
            }
        }
        /* (each level is played after an attempt that dies, the solver's
         * presses stopping at tick 1000: in Neon Steps that is 181 blocks
         * in, a respawn as far back as the title's run loops) */
        if (!play_level(list[i], 1000)) {
            /* start again for the next one */
            fails++;
            s_core->deinit(s_core);
            if (!boot_test(rom)) return 1;
            frames(30);
        }
        fflush(stdout);
    }
    /* the save: in the cartridge's SRAM, and read back after a restart */
    save_size = s_core->savedataClone(s_core, &save);
    s_core->deinit(s_core);
    latest = latest_slot(save, save_size);
    if (latest < 0) {
        printf("save: FAIL, no save in the SRAM\n");
        fails++;
    } else if (!boot_test(rom)) {
        fails++;
    } else {
        int bad = finished_after_restart(save, save_size, list, n) != n;
        printf("save: %s\n", bad ? "FAIL, the bests were not there after a restart" : "ok, the bests are kept over a restart");
        fails += bad;
        /* a save cut short (its data not all written): the one before it,
         * at most a level behind, is read instead */
        ((uint8_t *)save)[latest * SAVE_SLOT + SAVE_HEADER + 20] ^= 0x5A;
        bad = finished_after_restart(save, save_size, list, n) < n - 1;
        printf("save: %s\n", bad ? "FAIL, a save cut short lost the one before" : "ok, a save cut short leaves the one before");
        fails += bad;
        s_core->deinit(s_core);
    }
    free(save);
    /* every frame on time: the menus, their fades, the levels' loading,
     * the pause, the results, the restarts */
    if (s_late_total) {
        printf("late frames: FAIL, %u in all\n", s_late_total);
        fails++;
    } else {
        printf("late frames: ok, none (the screens' changes, the levels' loading and the pause included)\n");
    }
    for (int i = 0; i < s_nlate; i++)
        printf("  %4u late: %s (first at frame %ld; most work %d scanlines)\n", s_late[i].n, s_late[i].what,
               s_late[i].first, s_late[i].work);
    printf("  a frame's most work: title %d, level select %d, play %d, garage %d, options %d scanlines of 228\n",
           s_most[SCR_TITLE], s_most[SCR_SELECT], s_most[SCR_PLAY], s_most[SCR_GARAGE], s_most[SCR_OPTIONS]);
    printf("  the vertical blank's interrupt: at most %d scanlines (showing the frame, mixing the sound)\n", s_irq_max);
    /* the sound's DMAs restarted where they had read their buffers to the
     * end, never near asking for more */
    {
        int bad = s_snd[0].bad || s_snd[1].bad || !s_snd[0].n || !s_snd[1].n;
        printf("sound: %s, the DMAs restarted from %d to %d samples from a frame's end (the speaker's mix: %d to %d; "
               "%ld and %ld times), in %d to %d\n",
               bad ? "FAIL" : "ok", s_snd[0].min, s_snd[0].max, s_snd[1].min, s_snd[1].max, s_snd[0].n, s_snd[1].n,
               SOUND_EARLY, SOUND_LATE);
        for (int m = 0; m < 2; m++)
            if (s_snd[m].bad) printf("  %ld outside (%s mix), the first at frame %ld\n", s_snd[m].bad, m ? "the speaker's" : "the headphones'", s_snd[m].first_bad);
        fails += bad;
    }
    printf(fails ? "SOME CHECKS FAILED IN THE ROM\n" : "all levels finished in the ROM\n");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "run"))
        return cmd_run(argv[2], atoi(argv[3]), argc > 4 && strcmp(argv[4], "-") ? argv[4] : NULL,
                       argc > 5 ? argv[5] : NULL);
    if (argc >= 3 && !strcmp(argv[1], "play"))
        return cmd_play(argv[2], argc > 3 && strcmp(argv[3], "-") ? argv[3] : NULL, argc > 4 ? argv[4] : NULL);
    fprintf(stderr, "usage: gba_test run <rom> <frames> [script] [shots]\n"
                    "       gba_test play <rom> [levels|-] [shots-dir]\n");
    return 2;
}
