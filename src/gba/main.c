/*
 * Game Boy Advance frontend: start-up and the frame loop.
 *
 * The game is the shared core (src/core: the menus, the play, the title's
 * demo run, the physics), ticked once per frame; this frontend draws it
 * with tiles and sprites (gba_draw.h) and plays the songs the core's synth
 * rendered at build time (audio_gba.c). The display runs at 59.73 Hz, so
 * the game and its music run 0.45% slower than on the other platforms, in
 * step with each other.
 *
 * Each frame: wait for the vertical blank, read the keys, tick the game,
 * then prepare the next picture (sprites, scrolling, palettes, tiles
 * coming into view). The vertical blank interrupt shows the prepared
 * picture and mixes the next frame's sound, so neither waits on the
 * other: if a frame's work ever ran long, the music would go on and the
 * game would catch up the tick it missed (counted in g_frames.late).
 */
#include "gba.h"
#include "frame.h"
#include "gba_draw.h"
#include "audio_gba.h"
#include "game.h"
#include "game_internal.h"
#include "audio.h"

GbaFrames g_frames;
static uint32_t s_song_began = 0x80000000u; /* the vertical blank a song began at */

/* Where the emulator test (src/host/gba_test.c) finds the game's state:
 * the addresses of what it reads, then two sizes. Kept by gba.ld. */
__attribute__((section(".test_info"), used)) const void *const g_test_info[] = {
    &g_game.screen, &g_game.fade, &g_game.sel_level, &g_game.play.phase, &g_game.play.p,
    &g_game.save.progress, &g_game.play.paused, &g_game.play.results_sel, &g_game.menu_sel,
    &g_game.play.level_idx, &g_game.play.practice, &g_game.play.phase_t, &g_game.sel_scroll,
    (const void *)&g_audio_emit, (const void *)&g_audio_stat, &g_game.save.speaker, &g_game.options_sel,
    (const void *)sizeof(Player), (const void *)sizeof(Progress),
};

/* The interrupt handler (crt0.s calls it with the interrupts acknowledged). */
IWRAM_CODE void gba_irq(uint32_t flags)
{
    if (flags & IRQ_VBLANK) {
        int line0 = REG_VCOUNT, lines;
        audio_gba_vblank();
        if (g_frames.ready) {
            gba_draw_commit();
            g_frames.ready = 0;
        } else if (g_frames.running) {
            gba_draw_hold();
            g_frames.late++;
        }
        g_frames.vblanks++;
        audio_gba_mix();
        lines = REG_VCOUNT - line0;
        if (lines < 0) lines += 228;
        g_frames.irq = (uint16_t)lines;
        if (lines > g_frames.irq_max) g_frames.irq_max = (uint16_t)lines;
    }
}

/*
 * The pad as the core's buttons. A jumps and confirms, B goes back, Start
 * pauses, Select is the level select's "practice". During a run in
 * practice mode B places a checkpoint and Select removes one (as on the
 * Game Boy Color: B can't be a second jump button then); L and R jump, like
 * the shoulder buttons of the PS2 and PSP.
 */
static uint32_t map_keys(uint32_t k)
{
    static uint32_t s_hold_off; /* keys held as their meaning changed: not seen until let go */
    static int s_was_run = -1;
    const Game *g = &g_game;
    uint32_t b = 0;
    /* (dead too: B held through a death must not place a checkpoint at the respawn) */
    int run = g->screen == SCR_PLAY && !g->play.paused && g->play.phase != PH_COMPLETE;
    /* B resuming from the pause menu, still down as the run goes on, is
     * not a checkpoint (nor Select one removed) */
    if (run != s_was_run) {
        s_hold_off = k & (KEY_B | KEY_SELECT);
        s_was_run = run;
    }
    s_hold_off &= k;
    k &= ~s_hold_off;
    if (k & KEY_A) b |= BTN_CROSS;
    if (k & KEY_START) b |= BTN_START;
    if (k & KEY_UP) b |= BTN_UP;
    if (k & KEY_DOWN) b |= BTN_DOWN;
    if (k & KEY_LEFT) b |= BTN_LEFT;
    if (k & KEY_RIGHT) b |= BTN_RIGHT;
    if (k & KEY_L) b |= BTN_L1;
    if (k & KEY_R) b |= BTN_R1;
    if (run && g->play.practice) {
        if (k & KEY_B) b |= BTN_SQUARE;
        if (k & KEY_SELECT) b |= BTN_TRIANGLE;
    } else if (!run) {
        if (k & KEY_B) b |= BTN_CIRCLE;
        if (k & KEY_SELECT) b |= BTN_SQUARE;
    }
    return b;
}

/* scanlines since the latest vertical blank began (at line 160; 228 a frame) */
static int since_vblank(void)
{
    int v = REG_VCOUNT;
    return v >= 160 ? v - 160 : v + 68;
}

int frame_lines_left(void)
{
    /* (since_vblank counts the vertical blank's interrupt already: the 15
     * lines are a margin for what the frame does after its last check) */
    int left = 228 - 15 - since_vblank();
    return g_frames.vblanks != g_frames.begun || left < 0 ? 0 : left;
}

int main(void)
{
    REG_DISPCNT = DCNT_BLANK;
    REG_IME = 0;
    REG_IE = IRQ_VBLANK;
    REG_DISPSTAT = DSTAT_VBL_IRQ;
    REG_IME = 1;

    gba_draw_init_hw();
    audio_gba_init();
    game_init();

    g_frames.ticks = g_frames.vblanks;
    for (;;) {
        uint32_t n, vb0, lines;
        int line0;
        /* (the frame prepared last is shown before this one is begun, even
         * after a long frame) */
        while (g_frames.ready || g_frames.ticks >= g_frames.vblanks) vblank_intr_wait();
        vb0 = g_frames.vblanks;
        g_frames.begun = vb0;
        line0 = since_vblank();
        /* normally one tick; more only after a long frame (a level being
         * loaded), so the game keeps time with the music: up to a quarter
         * of a second of them, as on the PC (src/host/main_sdl.c) */
        n = g_frames.vblanks - g_frames.ticks;
        if (n > 15) {
            g_frames.ticks = g_frames.vblanks - 15;
            n = 15;
        }
        {
            uint32_t v0 = g_frames.vblanks;
            int song = audio_current_song(), began = 0;
            float t = audio_song_time();
            while (n--) {
                uint32_t keys = ~REG_KEYINPUT & KEY_MASK;
                int song0 = song;
                float t0 = t;
                g_frames.keys = (uint16_t)keys;
                game_tick(map_keys(keys));
                g_frames.ticks++;
                song = audio_current_song();
                t = audio_song_time();
                /* a song began (a run starting or starting again): it
                 * begins as this tick's picture is shown, so no tick runs
                 * after it in this frame */
                if (song >= 0 && (song != song0 || t < t0)) {
                    began = 1;
                    break;
                }
            }
            game_render(1.0f);
            if (began) {
                /* the song waits for the picture however long the frame
                 * took (a run's first frame draws all of its tiles): the
                 * next frame is its next tick, with no catching up */
                s_song_began = g_frames.vblanks;
                g_frames.ticks = g_frames.vblanks;
            } else if (g_frames.vblanks - v0 > 1 && song >= 0 && g_frames.vblanks - s_song_began < 30) {
                /* a long frame just after a song began: the run and the
                 * song go on from here together, rather than the run
                 * catching up, unseen, with a song that played on */
                audio_play_song(song, t);
                g_frames.ticks = g_frames.vblanks;
            }
        }
        /* how long it took, in scanlines (228 a frame) */
        lines = (g_frames.vblanks - vb0) * 228 + (uint32_t)(since_vblank() - line0);
        g_frames.work = (uint16_t)(lines > 0xFFFF ? 0xFFFF : lines);
        if (g_frames.work > g_frames.work_max) g_frames.work_max = g_frames.work;
        g_frames.ready = 1;
        g_frames.running = 1; /* (a vertical blank before the first picture is not late) */
    }
}
