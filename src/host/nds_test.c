/*
 * nds_test - runs the Nintendo DS ROM in melonDS (its libretro core,
 * loaded at run time), headless, for tests and screenshots.
 *
 *   nds_test run <core.so> <rom> <frames> [script] [shots]
 *       run the ROM for <frames> frames; script is a list of key changes
 *       "frame:KEYS,frame:KEYS,..." (KEYS: A B X Y L R U D LT RT ST SE
 *       joined by +, or - for none; T touches the bottom screen); shots is
 *       a list of frames to save as shot_<frame>.png (256x384, both
 *       screens) in the current directory
 *
 *   nds_test play <core.so> <rom> [levels] [shots-dir]
 *       the emulator test: visits the garage and the options, then plays
 *       each level (all, or a list "0,2,5") from the menus with the
 *       solver's inputs (src/host/solver.c), with a pause a third of the
 *       way in, and checks that the ROM's player is the reference's
 *       (src/core/sim.c run here) after every tick, that the level is
 *       finished, that no frame of a run was late (the game, its music and
 *       the inputs would slip), that the run keeps time with its song (the
 *       song heard as far ahead of the run's ticks all through it, the
 *       pause included) and that the song is heard (melonDS's sound out),
 *       that the 3D engine was never asked for more polygons than it
 *       draws, that the results come and lead back to the level select,
 *       and that the best is 100%. Exits non-zero if anything failed.
 *       With shots-dir: pictures of each level's start, middle and
 *       results, both screens.
 *
 * The ROM's ELF file must be beside it (pulsedash.elf for pulsedash.nds):
 * where the game keeps its state (g_test_info, src/nds/main_nds.c) is read
 * from it. melonDS runs with its interpreter (not its JIT, whose timing is
 * coarser) and with FreeBIOS, booting the ROM directly. Its ARM9 timing is
 * an approximation (its caches always hit, in 3 cycles a data access), so
 * the frames' work it measures is an estimate of the hardware's.
 */
#include <dlfcn.h>
#include <elf.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png_write.h"
#include "solver.h"
#include "../core/audio.h"
#include "../core/game_internal.h"
#include "../core/level.h"
#include "../core/progress.h"
#include "../core/sim.h"

/* ------------------------------------------------------------------ */
/* libretro, as much of it as is used                                  */
/* ------------------------------------------------------------------ */

struct retro_variable {
    const char *key, *value;
};
struct retro_game_info {
    const char *path;
    const void *data;
    size_t size;
    const char *meta;
};
typedef bool (*env_cb)(unsigned cmd, void *data);
typedef void (*video_cb)(const void *data, unsigned w, unsigned h, size_t pitch);
typedef void (*audio_cb)(int16_t l, int16_t r);
typedef size_t (*audio_batch_cb)(const int16_t *data, size_t frames);
typedef void (*poll_cb)(void);
typedef int16_t (*state_cb)(unsigned port, unsigned device, unsigned index, unsigned id);

#define ENV_GET_SYSTEM_DIRECTORY 9
#define ENV_SET_PIXEL_FORMAT 10
#define ENV_GET_VARIABLE 15
#define ENV_GET_VARIABLE_UPDATE 17
#define ENV_GET_SAVE_DIRECTORY 31
#define DEVICE_JOYPAD 1
#define DEVICE_POINTER 6
#define MEMORY_SYSTEM_RAM 2
enum { J_B = 0, J_Y, J_SELECT, J_START, J_UP, J_DOWN, J_LEFT, J_RIGHT, J_A, J_X, J_L, J_R };
#define K(j) (1u << (j))
#define K_TOUCH (1u << 16)

static struct {
    void (*init)(void);
    void (*set_environment)(env_cb);
    void (*set_video_refresh)(video_cb);
    void (*set_audio_sample)(audio_cb);
    void (*set_audio_sample_batch)(audio_batch_cb);
    void (*set_input_poll)(poll_cb);
    void (*set_input_state)(state_cb);
    bool (*load_game)(const struct retro_game_info *);
    void (*run)(void);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
} R;

static char s_sysdir[1024];
static int s_pixfmt;
static const uint8_t *s_video;
static unsigned s_vw, s_vh;
static size_t s_vpitch;
static uint32_t s_keys;
static double s_snd_sum;    /* the sound's power since it was last read */
static long s_snd_n;
static uint8_t *s_ram;      /* the DS's main RAM, 0x02000000 */
static size_t s_ram_size;

static bool on_env(unsigned cmd, void *data)
{
    static const struct retro_variable OPTS[] = {
        {"melonds_jit_enable", "disabled"},          {"melonds_boot_directly", "enabled"},
        {"melonds_console_mode", "DS"},              {"melonds_threaded_renderer", "disabled"},
        {"melonds_screen_layout", "Top/Bottom"},     {"melonds_touch_mode", "Touch"},
    };
    switch (cmd & 0xFFFF) {
    case ENV_GET_VARIABLE: {
        struct retro_variable *v = data;
        for (size_t i = 0; i < sizeof(OPTS) / sizeof(OPTS[0]); i++)
            if (!strcmp(v->key, OPTS[i].key)) {
                v->value = OPTS[i].value;
                return true;
            }
        return false;
    }
    case ENV_GET_SYSTEM_DIRECTORY:
    case ENV_GET_SAVE_DIRECTORY: *(const char **)data = s_sysdir; return true;
    case ENV_SET_PIXEL_FORMAT: s_pixfmt = *(int *)data; return true;
    case ENV_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    default: return false;
    }
}

static void on_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    s_video = data;
    s_vw = w;
    s_vh = h;
    s_vpitch = pitch;
}

static void on_audio(int16_t l, int16_t r)
{
    s_snd_sum += (double)l * l + (double)r * r;
    s_snd_n += 2;
}

static size_t on_audio_batch(const int16_t *d, size_t n)
{
    for (size_t i = 0; i < 2 * n; i++) s_snd_sum += (double)d[i] * d[i];
    s_snd_n += (long)(2 * n);
    return n;
}

static void on_poll(void) {}

static int16_t on_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port) return 0;
    if (device == DEVICE_JOYPAD) return id < 16 && (s_keys >> id & 1);
    if (device == DEVICE_POINTER && (s_keys & K_TOUCH)) {
        /* the middle of the bottom screen (the screens one above the other) */
        if (id == 0) return 0;
        if (id == 1) return 16384;
        if (id == 2) return 1;
    }
    return 0;
}

static int boot(const char *core, const char *rom)
{
    void *lib = dlopen(core, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "nds_test: %s\n", dlerror());
        return 0;
    }
#define SYM(f, name) *(void **)&R.f = dlsym(lib, name)
    SYM(init, "retro_init");
    SYM(set_environment, "retro_set_environment");
    SYM(set_video_refresh, "retro_set_video_refresh");
    SYM(set_audio_sample, "retro_set_audio_sample");
    SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll");
    SYM(set_input_state, "retro_set_input_state");
    SYM(load_game, "retro_load_game");
    SYM(run, "retro_run");
    SYM(get_memory_data, "retro_get_memory_data");
    SYM(get_memory_size, "retro_get_memory_size");
#undef SYM
    if (!R.init || !R.load_game || !R.run || !R.get_memory_data) {
        fprintf(stderr, "nds_test: %s is not a libretro core\n", core);
        return 0;
    }
    snprintf(s_sysdir, sizeof(s_sysdir), "%s", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    R.set_environment(on_env);
    R.set_video_refresh(on_video);
    R.set_audio_sample(on_audio);
    R.set_audio_sample_batch(on_audio_batch);
    R.set_input_poll(on_poll);
    R.set_input_state(on_state);
    R.init();
    FILE *f = fopen(rom, "rb");
    if (!f) {
        fprintf(stderr, "nds_test: can't open %s\n", rom);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) size = 0;
    fclose(f);
    struct retro_game_info gi = {rom, data, (size_t)size, NULL};
    if (!size || !R.load_game(&gi)) {
        fprintf(stderr, "nds_test: melonDS did not load %s\n", rom);
        return 0;
    }
    s_ram = R.get_memory_data(MEMORY_SYSTEM_RAM);
    s_ram_size = R.get_memory_size(MEMORY_SYSTEM_RAM);
    if (!s_ram || s_ram_size < (4u << 20)) {
        fprintf(stderr, "nds_test: no main RAM from the core\n");
        return 0;
    }
    return 1;
}

/* the DS's main RAM at addr (0x02xxxxxx, mirrored every 4 MB) */
static void rd(uint32_t addr, void *out, size_t n)
{
    memcpy(out, s_ram + ((addr - 0x02000000u) & 0x3FFFFFu), n);
}

static uint32_t rd32_at(uint32_t addr)
{
    uint32_t v;
    rd(addr, &v, 4);
    return v;
}

/* Both screens as a PNG (256x384). */
static int save_shot(const char *path)
{
    if (!s_video) return -1;
    uint8_t *rgb = malloc((size_t)s_vw * s_vh * 3);
    for (unsigned y = 0; y < s_vh; y++)
        for (unsigned x = 0; x < s_vw; x++) {
            uint8_t *o = rgb + (y * s_vw + x) * 3;
            if (s_pixfmt == 1) {
                uint32_t c = ((const uint32_t *)(s_video + y * s_vpitch))[x];
                o[0] = (uint8_t)(c >> 16);
                o[1] = (uint8_t)(c >> 8);
                o[2] = (uint8_t)c;
            } else {
                uint16_t c = ((const uint16_t *)(s_video + y * s_vpitch))[x];
                o[0] = (uint8_t)((c >> 11) << 3);
                o[1] = (uint8_t)(((c >> 5) & 63) << 2);
                o[2] = (uint8_t)((c & 31) << 3);
            }
        }
    int r = png_write_rgb(path, rgb, (int)s_vw, (int)s_vh);
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

/* "A+U" -> keys */
static uint32_t parse_keys(const char *s)
{
    static const char *const N[] = {"B", "Y", "SE", "ST", "U", "D", "LT", "RT", "A", "X", "L", "R"};
    uint32_t k = 0;
    while (*s && *s != ',') {
        size_t n = strcspn(s, "+,");
        for (int i = 0; i < 12; i++)
            if (strlen(N[i]) == n && !strncmp(s, N[i], n)) k |= 1u << i;
        if (n == 1 && *s == 'T') k |= K_TOUCH;
        s += n;
        if (*s == '+') s++;
    }
    return k;
}

static int cmd_run(const char *core, const char *rom, int frames, const char *script, const char *shots)
{
    if (!boot(core, rom)) return 1;
    for (int f = 0; f < frames; f++) {
        if (script) {
            /* the last change at or before this frame */
            const char *p = script;
            while (*p) {
                int at = atoi(p);
                const char *c = strchr(p, ':');
                if (!c || at > f) break;
                s_keys = parse_keys(c + 1);
                p = strchr(c, ',');
                if (!p) break;
                p++;
            }
        }
        R.run();
        if (shots) {
            char key[32];
            snprintf(key, sizeof(key), ",%d,", f);
            char *list = malloc(strlen(shots) + 3);
            sprintf(list, ",%s,", shots);
            if (strstr(list, key)) {
                char path[64];
                snprintf(path, sizeof(path), "shot_%d.png", f);
                save_shot(path);
            }
            free(list);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

/* g_test_info's entries (src/nds/main_nds.c) */
enum { TI_SCREEN, TI_FADE, TI_SEL_LEVEL, TI_PHASE, TI_PLAYER, TI_PROGRESS, TI_PAUSED, TI_RESULTS_SEL,
       TI_MENU_SEL, TI_LEVEL_IDX, TI_PRACTICE, TI_PHASE_T, TI_SEL_SCROLL, TI_SONG, TI_HEARD, TI_STATS,
       TI_PLAYER_SIZE, TI_PROGRESS_SIZE, TI_COUNT };
static uint32_t s_ti[TI_COUNT];
static long s_frame;
static const char *s_shots;

/* NdsStats (main_nds.c): the words read here */
enum { ST_FRAMES = 1, ST_LATE = 2, ST_WORK_LAST = 3, ST_POLYS_MAX = 5, ST_DROPPED = 6, ST_TICK_LAST = 9,
       ST_DRAW_LAST = 10, ST_AUDIO_LAST = 11, ST_LATE_AT = 18 };
#define FRAME_CYCLES 560190.0 /* bus cycles a frame */
#define SAMPLES_PER_TICK (560190.0 / 1024.0)

static int32_t rd32(int what) { return (int32_t)rd32_at(s_ti[what]); }
static float rdf(int what)
{
    float f;
    rd(s_ti[what], &f, 4);
    return f;
}
static uint32_t stat(int word) { return rd32_at(s_ti[TI_STATS] + 4u * (uint32_t)word); }

static void frame(uint32_t keys)
{
    s_keys = keys;
    R.run();
    s_frame++;
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

static int wait_screen(int screen, int max)
{
    while (max-- > 0) {
        if (rd32(TI_SCREEN) == screen && rdf(TI_FADE) <= 0.0f) return 1;
        frame(0);
    }
    return 0;
}

static void shot(const char *name, int level)
{
    char path[1024];
    if (!s_shots) return;
    snprintf(path, sizeof(path), "%s/level%d_%s.png", s_shots, level, name);
    save_shot(path);
}

static double sound_rms(void)
{
    double r = s_snd_n ? sqrt(s_snd_sum / (double)s_snd_n) : 0.0;
    s_snd_sum = 0.0;
    s_snd_n = 0;
    return r;
}

static int boot_test(const char *core, const char *rom)
{
    uint32_t info = elf_symbol(rom, "g_test_info");
    if (!info) {
        fprintf(stderr, "nds_test: no g_test_info in the ROM's ELF file\n");
        return 0;
    }
    if (!boot(core, rom)) return 0;
    frames(2);
    for (int i = 0; i < TI_COUNT; i++) s_ti[i] = rd32_at(info + (uint32_t)i * 4);
    if (s_ti[TI_PLAYER_SIZE] != sizeof(Player) || s_ti[TI_PROGRESS_SIZE] != sizeof(Progress)) {
        fprintf(stderr, "nds_test: the ROM's Player or Progress is not laid out as here (%u, %u bytes)\n",
                (unsigned)s_ti[TI_PLAYER_SIZE], (unsigned)s_ti[TI_PROGRESS_SIZE]);
        return 0;
    }
    return 1;
}

/* the garage and the options, there and back (their screens drawn on time) */
static int tour_menus(void)
{
    uint32_t late0;
    if (!wait_screen(SCR_TITLE, 600)) {
        printf("menus: FAIL, no title screen\n");
        return 0;
    }
    frames(120);
    late0 = stat(ST_LATE);
    while (rd32(TI_MENU_SEL) != 0) press(K(J_LEFT));
    press(K(J_A));
    if (!wait_screen(SCR_GARAGE, 600)) {
        printf("menus: FAIL, A on GARAGE did not open the garage\n");
        return 0;
    }
    frames(60);
    press(K(J_B));
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    while (rd32(TI_MENU_SEL) != 2) press(K(J_RIGHT));
    press(K(J_A));
    if (!wait_screen(SCR_OPTIONS, 600)) {
        printf("menus: FAIL, A on OPTIONS did not open the options\n");
        return 0;
    }
    frames(60);
    press(K(J_B));
    if (!wait_screen(SCR_TITLE, 600)) return 0;
    if (stat(ST_LATE) != late0) {
        printf("menus: FAIL, %u frames late in the title, the garage and the options\n", stat(ST_LATE) - late0);
        return 0;
    }
    printf("menus: ok, the title, the garage and the options, no frame late\n");
    return 1;
}

static int start_level(int idx)
{
    if (!wait_screen(rd32(TI_SCREEN) == SCR_SELECT ? SCR_SELECT : SCR_TITLE, 600)) return 0;
    if (rd32(TI_SCREEN) == SCR_TITLE) {
        while (rd32(TI_MENU_SEL) != 1) press(K(rd32(TI_MENU_SEL) < 1 ? J_RIGHT : J_LEFT));
        press(K(J_A));
        if (!wait_screen(SCR_SELECT, 600)) return 0;
    }
    for (int tries = 0; rd32(TI_SEL_LEVEL) != idx && tries < 32; tries++) {
        press(K(rd32(TI_SEL_LEVEL) < idx ? J_RIGHT : J_LEFT));
        frames(10);
    }
    if (rd32(TI_SEL_LEVEL) != idx) return 0;
    for (int n = 0; n < 120 && fabsf(rdf(TI_SEL_SCROLL) - (float)idx) > 0.01f; n++) frame(0);
    frames(4);
    press(K(J_A));
    for (int n = 0; n < 600; n++) {
        Player p;
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if (rd32(TI_SCREEN) == SCR_PLAY && rd32(TI_LEVEL_IDX) == idx && !p.done && !p.dead && p.ticks < 2)
            return !rd32(TI_PRACTICE);
        frame(0);
    }
    return 0;
}

static void print_player(const char *who, const Player *p)
{
    printf("  %-9s x %d y %d vy %d mode %d grav %d grounded %d dead %d ticks %u\n", who, (int)p->x, (int)p->y,
           (int)p->vy, p->mode, p->grav, p->grounded, p->dead, p->ticks);
}

#define PAUSE_FRAMES 40

/* the late frames among the last 8 since `from` (late count): what took
 * the time (main_nds.c's late_at) */
static void print_late(uint32_t from)
{
    static const char *const SCREENS[] = {"title", "select", "play", "garage", "options"};
    uint32_t late = stat(ST_LATE);
    for (uint32_t k = late > 8 && late - 8 > from ? late - 8 : from; k < late; k++) {
        uint32_t w = ST_LATE_AT + (k % 8) * 6;
        uint32_t scr = stat((int)w + 1);
        printf("  late frame %u (%s): tick %.0f%%, drawing %.0f%%, sound %.0f%%, bottom screen %.0f%% of a frame\n",
               stat((int)w), scr < 5 ? SCREENS[scr] : "?", 100.0 * stat((int)w + 2) / FRAME_CYCLES,
               100.0 * stat((int)w + 3) / FRAME_CYCLES, 100.0 * stat((int)w + 4) / FRAME_CYCLES,
               100.0 * stat((int)w + 5) / FRAME_CYCLES);
    }
}

/* back to the level select from a run that failed: the pause menu's EXIT */
static void leave_run(void)
{
    if (rd32(TI_SCREEN) != SCR_PLAY) return;
    for (int n = 0; n < 300 && rd32(TI_PHASE) != PH_RUN; n++) frame(0);
    press(K(J_START));
    for (int k = 0; k < 4; k++) press(K(J_DOWN));
    press(K(J_A));
    wait_screen(SCR_SELECT, 600);
}

static int play_level(int idx)
{
    static uint8_t held[MAX_TICKS];
    Level *L = level_parse(g_levels[idx].src);
    LevelInfo info;
    Player ref, p;
    int t = -1, ok = 1, prev = 0, mid_shot = 0, pause = 0, pause_frames = 0, pause_at = -1;
    double lead_lo = 1e9, lead_hi = -1e9, wsum = 0, wmax = 0, snd = 0;
    long wn = 0;
    int wmax_tick = 0;
    uint32_t late0 = 0;
    int late_ticks[8], n_late_ticks = 0;
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
        printf("level %d (%s): FAIL, could not start it from the menus (frame %ld: screen %d, level %d)\n", idx,
               info.name, s_frame, rd32(TI_SCREEN), rd32(TI_SEL_LEVEL));
        shot("menus", idx);
        level_free(L);
        return 0;
    }
    sim_reset(&ref, L);
    rd(s_ti[TI_PLAYER], &p, sizeof(p));
    for (int k = 0; k < (int)p.ticks; k++) sim_tick(&ref, L, 0, 0);
    shot("start", idx);
    sound_rms();
    for (;;) {
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if (p.done || p.dead || p.ticks >= MAX_TICKS - 1) break;
        int tick = (int)p.ticks, h = held[tick];
        uint32_t keys = h ? K(J_A) : 0;
        if (pause == 0 && tick == pause_at) {
            keys = K(J_START);
            pause = 1;
        } else if (pause == 1) {
            keys = 0;
            if (++pause_frames == PAUSE_FRAMES) {
                if (!rd32(TI_PAUSED)) {
                    printf("level %d: FAIL, START did not pause the run at tick %d\n", idx, tick);
                    ok = 0;
                    break;
                }
                keys = K(J_START);
                pause = 2;
            }
        }
        uint32_t late_before = stat(ST_LATE);
        frame(keys);
        if (tick == 30) late0 = stat(ST_LATE);
        if (tick > 30 && stat(ST_LATE) != late_before && n_late_ticks < 8) late_ticks[n_late_ticks++] = tick;
        rd(s_ti[TI_PLAYER], &p, sizeof(p));
        if ((int)p.ticks == tick) continue; /* (paused, or not running yet) */
        for (int k = tick; k < (int)p.ticks; k++) {
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
        if (memcmp(&ref, &p, sizeof(p))) {
            printf("level %d: FAIL, the player differs from the reference after tick %d\n", idx, tick);
            print_player("ROM", &p);
            print_player("reference", &ref);
            ok = 0;
            break;
        }
        if (tick >= 30) {
            /* the game's (the ticks, the drawing, the sound), without the
             * bottom screen's drawing, which takes what time is left */
            double w = (stat(ST_TICK_LAST) + stat(ST_DRAW_LAST) + stat(ST_AUDIO_LAST)) / FRAME_CYCLES;
            wsum += w;
            wn++;
            if (w > wmax) {
                wmax = w;
                wmax_tick = tick;
            }
            /* the song heard, against the run's ticks */
            if (rd32(TI_SONG) != SONG_FIRST_LEVEL + info.song) {
                printf("level %d: FAIL, not its song (%d) at tick %d\n", idx, rd32(TI_SONG), tick);
                ok = 0;
                break;
            }
            double lead = rd32(TI_HEARD) / SAMPLES_PER_TICK - p.ticks;
            if (lead < lead_lo) lead_lo = lead;
            if (lead > lead_hi) lead_hi = lead;
        }
        if (!mid_shot && (int)p.ticks >= t / 2) {
            mid_shot = 1;
            shot("middle", idx);
        }
    }
    snd = sound_rms();
    if (ok && !p.done) {
        printf("level %d: FAIL, the run ended at tick %u without finishing (dead %d)\n", idx, p.ticks, p.dead);
        print_player("ROM", &p);
        print_player("reference", &ref);
        ok = 0;
    }
    if (ok && (int)p.ticks != t) {
        printf("level %d: FAIL, finished after %u ticks, the reference after %d\n", idx, p.ticks, t);
        ok = 0;
    }
    if (ok && pause != 2) {
        printf("level %d: FAIL, the run was not paused and resumed (tick %d)\n", idx, pause_at);
        ok = 0;
    }
    if (ok && stat(ST_LATE) != late0) {
        printf("level %d: FAIL, %u frames of the run were late, at ticks", idx, stat(ST_LATE) - late0);
        for (int k = 0; k < n_late_ticks; k++) printf(" %d", late_ticks[k]);
        printf("\n");
        print_late(late0);
        ok = 0;
    }
    /* the sound's time is read once a frame, at a point of it that moves
     * with the frame's work: within a tick, but for that */
    if (ok && lead_hi - lead_lo > 1.5) {
        printf("level %d: FAIL, the song drifted from the run by %.2f ticks\n", idx, lead_hi - lead_lo);
        ok = 0;
    }
    if (ok && snd < 300.0) {
        printf("level %d: FAIL, no music heard (rms %.0f)\n", idx, snd);
        ok = 0;
    }
    if (ok && stat(ST_DROPPED)) {
        printf("level %d: FAIL, %u primitives left out (past the 3D engine's polygons)\n", idx, stat(ST_DROPPED));
        ok = 0;
    }
    if (ok) {
        Progress pr;
        frames(180);
        shot("results", idx);
        rd(s_ti[TI_PROGRESS], &pr, sizeof(pr));
        if (rd32(TI_PHASE) != PH_COMPLETE || pr.best[idx] != 100) {
            printf("level %d: FAIL, no results or no best of 100%% after the finish (phase %d, best %d)\n", idx,
                   rd32(TI_PHASE), pr.best[idx]);
            ok = 0;
        }
        while (rd32(TI_RESULTS_SEL) != 0) press(K(J_LEFT));
        press(K(J_A));
        if (ok && !wait_screen(SCR_SELECT, 600)) {
            printf("level %d: FAIL, MENU did not lead to the level select\n", idx);
            ok = 0;
        }
    }
    if (!ok) leave_run();
    if (ok)
        printf("level %d (%s): ok, %d ticks as the reference, no frame late, in time with its song (within %.2f "
               "ticks), music rms %.0f; a frame's work %.0f%% / %.0f%% (mean / most, at tick %d; the game's, without the bottom "
               "screen's, which takes the time left)\n",
               idx, info.name, t, lead_hi - lead_lo, snd, 100.0 * wsum / (double)(wn ? wn : 1), 100.0 * wmax,
               wmax_tick);
    level_free(L);
    return ok;
}

static int cmd_play(const char *core, const char *rom, const char *levels, const char *shots)
{
    int list[64], n = 0, ok = 1;
    s_shots = shots;
    if (levels && *levels) {
        for (const char *p = levels; *p && n < 64;) {
            list[n++] = atoi(p);
            p = strchr(p, ',');
            if (!p) break;
            p++;
        }
    } else {
        for (int i = 0; i < g_level_count; i++) list[n++] = i;
    }
    if (!boot_test(core, rom)) return 1;
    ok &= tour_menus();
    for (int i = 0; i < n; i++) ok &= play_level(list[i]);
    printf("most polygons in a frame: %u of 2048; frames late in all: %u of %u\n", stat(ST_POLYS_MAX), stat(ST_LATE),
           stat(ST_FRAMES));
    printf(ok ? "nds_test: all passed\n" : "nds_test: FAILED\n");
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 5 && !strcmp(argv[1], "run"))
        return cmd_run(argv[2], argv[3], atoi(argv[4]), argc > 5 ? argv[5] : NULL, argc > 6 ? argv[6] : NULL);
    if (argc >= 4 && !strcmp(argv[1], "play"))
        return cmd_play(argv[2], argv[3], argc > 4 ? argv[4] : NULL, argc > 5 ? argv[5] : NULL);
    fprintf(stderr,
            "usage: nds_test run <core.so> <rom> <frames> [script] [shots]\n"
            "       nds_test play <core.so> <rom> [levels] [shots-dir]\n");
    return 2;
}
