/*
 * Pulse Dash for the Game Boy Color: what the ROM's modules share.
 *
 *   main.c   start-up, interrupts, the screens' order
 *   video.c  palettes, tiles, text, the background map
 *   play.c   a level: physics (gbsim.c), camera, sprites, death, practice
 *   menu.c   title and level select
 *   music.c  the songs and sound effects on the four sound channels
 *   save.c   best percentages and coins in the cartridge's battery RAM
 *
 * Data made from the game's sources by gbc_tool (levels, tiles, songs) is
 * in build/gbc/gen.
 */
#ifndef GBC_H
#define GBC_H

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

#include "gbsim.h"
#include "gfx_ids.h"
#include "gbc_data.h"

/* --- main.c --- */
extern uint8_t g_keys, g_pressed; /* buttons held / newly pressed this frame */
extern uint16_t g_frame;
/* For the emulator test: which screen runs, and how much of a frame the
 * last one's work took (the scanline it finished on, 144+ = in vblank). */
enum { SCR_BOOT = 0, SCR_TITLE, SCR_SELECT, SCR_PLAY };
extern uint8_t g_screen;
extern uint8_t g_perf_ly, g_perf_ly_max;

/* PERF=1 builds (Makefile.gbc) time the parts of a frame, in scanlines,
 * for the emulator test (scripts/gbc-emu-test.py --perf). */
enum { PERF_FRAME, PERF_SIM, PERF_MUSIC, PERF_STREAM, PERF_HUD, PERF_PAL, PERF_SPRITES, PERF_N };
#ifdef PD_PERF
extern uint8_t g_perf[PERF_N];
uint16_t perf_now(void);
#define PERF_BEGIN(t) ((t) = perf_now())
#define PERF_END(i, t) (g_perf[i] = (uint8_t)(perf_now() - (t) > 255 ? 255 : perf_now() - (t)))
#else
#define PERF_BEGIN(t)
#define PERF_END(i, t)
#endif

/* Read the pad; once a frame. */
void input_update(void);
/* Wait for the next frame, then do its video work (video_vblank). */
void frame_wait(void);

/* --- video.c --- */
/* The progress bar row: background row 0 is shown unscrolled, the rest
 * scrolled by g_scx (switched by the LY=7 interrupt). */
extern volatile uint8_t g_scx;
extern uint8_t g_hud_split;

void video_init(void);
/* The title logo (in the tiles' ROM bank) at background row y. */
void video_logo(uint8_t y);
/* Upload what changed this frame; call first thing in vblank. */
void video_vblank(void);
void video_off(void);
void video_on(void);
void bkg_clear(void);

/* Palettes to upload in the next vblank (RGB555, 8 background and 3
 * sprite palettes of 4), g_pal_dirty bit 0 / bit 1 when they changed. */
extern uint16_t g_bgpal[32];
extern uint16_t g_objpal[12];
extern uint8_t g_pal_dirty;

/* --- palette.c --- */
void pal_init(void) BANKED;
/* pal_level() blends two level palettes (t of 256), adds the beat's flash
 * (0..15) and dims everything by fade (0 black .. 8 full). */
void pal_level(uint8_t from, uint8_t to, uint8_t t, uint8_t flash, uint8_t fade) BANKED;
void pal_menu(uint8_t fade) BANKED;
void pal_sprites(void) BANKED;

/* Text: background or window map, font tiles in VRAM bank 1. */
void text_bkg(uint8_t x, uint8_t y, const char *s, uint8_t pal);
void text_win(uint8_t x, uint8_t y, const char *s, uint8_t pal);
void tile_bkg(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr);
void tile_win(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr);
void fill_bkg(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr);
void fill_win(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t tile, uint8_t attr);
/* Decimal into buf (no leading zeros), returns buf. */
char *fmt_uint(char *buf, uint16_t v);

/* Queue a background column (15 tiles from map row 1) for the next vblank. */
#define COL_ROWS 15
void col_queue(uint8_t mapx, const uint8_t *tiles, const uint8_t *attrs);
uint8_t col_queue_free(void);
/* Queue one map cell for the next vblank (a collected coin). */
void cell_queue(uint8_t mapx, uint8_t mapy, uint8_t tile, uint8_t attr);
/* Show the saw tile's animation frame in the next vblank. */
void saw_frame(uint8_t f);

/* --- music.c --- */
/* songs, numbered as in src/core/audio.h */
#define SONG_MENU_GB 0
#define SONG_PRACTICE_GB 1
#define SONG_FIRST_LEVEL_GB 3
void music_init(void);
void music_play(uint8_t song);
/* Play the song unless it is playing already. */
void music_ensure(uint8_t song);
void music_stop(void);
void music_pause(uint8_t paused);
/* Advance one 1/60 s tick (the game's tick). */
void music_tick(void);
/* The current song's beat started on this tick. */
extern uint8_t music_beat;
enum { SFX_DEATH, SFX_COIN, SFX_CHECKPOINT, SFX_MOVE, SFX_SELECT, SFX_BACK, SFX_COMPLETE, SFX_ORB };
void sfx_play(uint8_t id);

/* --- save.c --- */
typedef struct {
    uint8_t best[GBC_LEVEL_COUNT];          /* normal mode, percent */
    uint8_t best_practice[GBC_LEVEL_COUNT];
    uint8_t coins[GBC_LEVEL_COUNT];         /* bitmask */
    uint16_t attempts[GBC_LEVEL_COUNT];
} SaveData;
extern SaveData g_save;
void save_load(void);
void save_write(void);

/* --- play.c, play_ui.c, menu.c (the last two in a ROM bank of their own) --- */
void play_level(uint8_t level, uint8_t practice);
void ui_ground(uint8_t width) BANKED;
void ui_hud_init(void) BANKED;
void ui_attempt(uint16_t attempts) BANKED;
void ui_new_best(uint8_t pc) BANKED;
void ui_pause(uint8_t practice) BANKED;
void ui_results(uint8_t practice, uint16_t attempts, uint16_t jumps, uint16_t secs, uint8_t ncoins,
                uint8_t coins) BANKED;
void title_screen(void) BANKED;
/* Returns 0 to go back to the title, else plays; *level, *practice chosen. */
uint8_t select_screen(uint8_t *level, uint8_t *practice) BANKED;

#endif
