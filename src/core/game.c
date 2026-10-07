/* The game's flow: screens, the menus' input and choices, saves. Drawn by
 * game_draw.c in the vector family, by the platform's own code elsewhere. */
#include <stdio.h>

#include "game_internal.h"
#include "audio.h"
#include "fx.h"
#include "platform.h"

Game g_game;

#define FADE_TIME 0.22f

float beat_pulse(void)
{
    const Game *g = &g_game;
    const PlayState *ps = &g->play;
    int song = audio_current_song();
    if (song < 0) return 0.0f;
    float b;
    if (g->screen == SCR_PLAY && !ps->practice && ps->phase == PH_RUN) {
        /* the song started with the attempt: the level clock is exact and
         * smoother than the audio thread's chunked position */
        float t = ps->attempt_time - (1.0f - g->alpha) * TICK_DT;
        b = t * audio_song_bpm(song) / 60.0f;
    } else {
        b = audio_song_beat();
    }
    if (b < 0.0f) return 0.0f;
    float f = b - floorf(b);
    float accent = ((int)b % 4) == 0 ? 1.0f : 0.65f; /* stronger on each bar's downbeat */
    return accent * expf(-f * 5.0f);
}

void menu_palette(Palette *out)
{
    Game *g = &g_game;
    int a = (int)(g->t / 6.0f) % PALETTE_COUNT;
    int b = (a + 1) % PALETTE_COUNT;
    float f = fmodf(g->t, 6.0f) / 6.0f;
    f = smoothstepf((f - 0.8f) / 0.2f);
    palette_lerp(out, &g_palettes[a], &g_palettes[b], f);
}

static void on_enter(int scr)
{
    Game *g = &g_game;
    switch (scr) {
    case SCR_TITLE:
    case SCR_SELECT:
    case SCR_GARAGE:
    case SCR_OPTIONS:
        if (audio_current_song() != SONG_MENU) audio_play_song(SONG_MENU, 0.0f);
        if (g->save_dirty) {
            save_store(&g->save);
            g->save_dirty = 0;
        }
        break;
    case SCR_PLAY:
        play_start(g->sel_level, g->start_practice);
        break;
    }
}

void screen_go(int scr)
{
    Game *g = &g_game;
    if (g->fading) return;
    g->next_screen = scr;
    g->fading = 1;
}

void game_flush_save(void)
{
    if (g_game.save_dirty) {
        save_store(&g_game.save);
        g_game.save_dirty = 0;
    }
}

void game_suspend(int on)
{
    Game *g = &g_game;
    if (!on) {
        /* every button counts as down already: one held now is pressed
         * when it has been let go and comes down again; and a direction
         * held doesn't repeat until then (its timer, put far back here,
         * starts again from 0 when it is let go) */
        g->prev = ~0u;
        for (int i = 0; i < 4; i++) g->rep_t[i] = -1e9f;
        return;
    }
    /* a run, or one being faded into; not one being left */
    if (g->fading > 0 ? g->next_screen == SCR_PLAY : g->screen == SCR_PLAY) play_suspend(g->fading > 0);
}

void game_status(char *buf, int cap)
{
    static const char *names[] = {"title", "select", "play", "garage", "options"};
    const Game *g = &g_game;
    if (g->screen == SCR_PLAY && g->play.L)
        snprintf(buf, (size_t)cap, "screen=play level=%d attempt=%d phase=%d x=%.1f%s best=%d", g->play.level_idx,
                 g->play.attempt, g->play.phase, (double)sim_x(&g->play.p), g->play.practice ? " practice" : "",
                 g->save.progress.best[g->play.level_idx % SAVE_MAX_LEVELS]);
    else
        snprintf(buf, (size_t)cap, "screen=%s sel=%d level=%d song=%d", names[g->screen % 5], g->menu_sel,
                 g->sel_level, audio_current_song());
}

unsigned game_attempts_started(void)
{
    return play_attempts_started();
}

void game_init(void)
{
    Game *g = &g_game;
    memset(g, 0, sizeof(*g));
    game_draw_init();
    fx_clear();
    save_load(&g->save);
    audio_set_volume(g->save.music_vol, g->save.sfx_vol);
    audio_set_user_delay(g->save.audio_delay * 0.01f);
    if (g->save.speaker != OUTPUT_AUTO) audio_set_output(AUDIO_OUTPUT_OPTION && g->save.speaker == OUTPUT_SPEAKER);
#if FLICKER_OPTION
    plat_flicker_filter(g->save.flicker);
#endif
    g->screen = SCR_TITLE;
    g->menu_sel = 1;
    g->alpha = 1.0f;
    g->fade = 1.0f;
    g->fading = -1;
    on_enter(SCR_TITLE);
}

static void update_input(uint32_t held)
{
    Game *g = &g_game;
    g->held = held;
    g->pressed = held & ~g->prev;
    g->prev = held;
    g->repeat = g->pressed;
    static const uint32_t dirs[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
    for (int i = 0; i < 4; i++) {
        if (!(held & dirs[i])) {
            g->rep_t[i] = 0.0f;
            continue;
        }
        g->rep_t[i] += TICK_DT;
        if (g->rep_t[i] > 0.38f) {
            g->rep_t[i] -= 0.09f;
            g->repeat |= dirs[i];
        }
    }
}

void game_tick(uint32_t held)
{
    Game *g = &g_game;
    update_input(held);
    g->t += TICK_DT;

    if (g->fading > 0) {
        g->fade += TICK_DT / FADE_TIME;
        if (g->fade >= 1.0f) {
            g->fade = 1.0f;
            if (g->screen == SCR_PLAY && g->next_screen != SCR_PLAY) play_exit();
            g->screen = g->next_screen;
            g->fading = -1;
            fx_clear_space(FX_SCREEN);
            on_enter(g->screen);
        }
        fx_update(TICK_DT);
        return;
    }
    if (g->fading < 0) {
        g->fade -= TICK_DT / FADE_TIME;
        if (g->fade <= 0.0f) {
            g->fade = 0.0f;
            g->fading = 0;
        }
    }

    if (g->screen == SCR_PLAY) play_tick();
    else menus_tick();
    fx_update(TICK_DT);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

/* --- title ---------------------------------------------------------- */

/* The menu song's beat as it is heard, or NULL while it isn't playing (with
 * no audio output its clock stands still). */
static const float *menu_beat(void)
{
    static float last, beat;
    static int still;
    float t = audio_song_time();
    still = t == last ? still + 1 : 0;
    last = t;
    beat = audio_song_beat();
    return audio_current_song() == SONG_MENU && still < 30 ? &beat : NULL;
}

static void title_tick(void)
{
    Game *g = &g_game;
    demo_tick(&g->demo, menu_beat());

    if (g->repeat & BTN_LEFT) { g->menu_sel = (g->menu_sel + 2) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_RIGHT) { g->menu_sel = (g->menu_sel + 1) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->pressed & (BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_MENU_SELECT);
        static const int dest[3] = {SCR_GARAGE, SCR_SELECT, SCR_OPTIONS};
        screen_go(dest[g->menu_sel]);
    }
}

/* --- level select --------------------------------------------------- */

static void select_tick(void)
{
    Game *g = &g_game;
    int n = g_level_count;
    if (g->repeat & BTN_LEFT) {
        g->sel_level--;
        audio_sfx(SFX_MENU_MOVE);
    }
    if (g->repeat & BTN_RIGHT) {
        g->sel_level++;
        audio_sfx(SFX_MENU_MOVE);
    }
    /* wrap while keeping the scroll animation continuous */
    if (g->sel_level < 0) { g->sel_level += n; g->sel_scroll += n; }
    if (g->sel_level >= n) { g->sel_level -= n; g->sel_scroll -= n; }
    g->sel_scroll = lerpf(g->sel_scroll, (float)g->sel_level, 1.0f - expf(-TICK_DT * 12.0f));

    if (g->pressed & (BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_START);
        g->start_practice = 0;
        screen_go(SCR_PLAY);
    } else if (g->pressed & BTN_SQUARE) {
        audio_sfx(SFX_START);
        g->start_practice = 1;
        screen_go(SCR_PLAY);
    } else if (g->pressed & BTN_CIRCLE) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

/* --- garage --------------------------------------------------------- */

static void garage_tick(void)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) { g->garage_row = (g->garage_row + 2) % 3; audio_sfx(SFX_MENU_MOVE); }
    if (g->repeat & BTN_DOWN) { g->garage_row = (g->garage_row + 1) % 3; audio_sfx(SFX_MENU_MOVE); }
    int d = 0;
    if (g->repeat & BTN_LEFT) d = -1;
    if (g->repeat & BTN_RIGHT) d = 1;
    if (d) {
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
        switch (g->garage_row) {
        case 0: g->save.icon = (uint8_t)((g->save.icon + ICON_COUNT + d) % ICON_COUNT); break;
        case 1: g->save.col1 = (uint8_t)((g->save.col1 + PLAYER_COLOR_COUNT + d) % PLAYER_COLOR_COUNT); break;
        default: g->save.col2 = (uint8_t)((g->save.col2 + PLAYER_COLOR_COUNT + d) % PLAYER_COLOR_COUNT); break;
        }
    }
    if (g->pressed & (BTN_CIRCLE | BTN_CROSS | BTN_START)) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

/* --- options -------------------------------------------------------- */

static void options_tick(void)
{
    Game *g = &g_game;
    if (g->repeat & BTN_UP) {
        g->options_sel = (g->options_sel + OPT_COUNT - 1) % OPT_COUNT;
        audio_sfx(SFX_MENU_MOVE);
        g->erase_confirm = 0;
    }
    if (g->repeat & BTN_DOWN) {
        g->options_sel = (g->options_sel + 1) % OPT_COUNT;
        audio_sfx(SFX_MENU_MOVE);
        g->erase_confirm = 0;
    }
    /* the metronome plays while the audio delay is being set */
    int song = g->options_sel == OPT_DELAY ? SONG_METRONOME : SONG_MENU;
    if (audio_current_song() != song) audio_play_song(song, 0.0f);

    int d = 0;
    if (g->repeat & BTN_LEFT) d = -1;
    if (g->repeat & BTN_RIGHT) d = 1;
    if (d && (g->options_sel == OPT_MUSIC || g->options_sel == OPT_SFX)) {
        uint8_t *v = g->options_sel == OPT_MUSIC ? &g->save.music_vol : &g->save.sfx_vol;
        *v = (uint8_t)clampi(*v + d, 0, 10);
        audio_set_volume(g->save.music_vol, g->save.sfx_vol);
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
    }
#if AUDIO_OUTPUT_OPTION
    if ((d || (g->pressed & BTN_CROSS)) && g->options_sel == OPT_OUTPUT) {
        /* HEADPHONES, SPEAKER(S), and AUTO where the frontend can tell */
        const int n = AUDIO_OUTPUT_AUTO ? 3 : 2;
        g->save.speaker = (uint8_t)((g->save.speaker % n + (d < 0 ? n - 1 : 1)) % n);
        if (g->save.speaker != OUTPUT_AUTO) audio_set_output(g->save.speaker == OUTPUT_SPEAKER);
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
    }
#endif
#if FLICKER_OPTION
    if ((d || (g->pressed & BTN_CROSS)) && g->options_sel == OPT_FLICKER) {
        g->save.flicker = !g->save.flicker;
        plat_flicker_filter(g->save.flicker);
        audio_sfx(SFX_MENU_MOVE);
        g->save_dirty = 1;
    }
#endif
    if (d && g->options_sel == OPT_DELAY) {
        g->save.audio_delay = (int8_t)clampi(g->save.audio_delay + d, -SAVE_AUDIO_DELAY_MAX, SAVE_AUDIO_DELAY_MAX);
        audio_set_user_delay(g->save.audio_delay * 0.01f);
        g->save_dirty = 1;
    }
    if (g->erase_confirm) {
        g->erase_t -= TICK_DT;
        if (g->erase_t <= 0.0f) g->erase_confirm = 0;
    }
    if (g->pressed & BTN_CROSS) {
        if (g->options_sel == OPT_ERASE) {
            if (!g->erase_confirm) {
                g->erase_confirm = 1;
                g->erase_t = 3.0f;
                audio_sfx(SFX_MENU_SELECT);
            } else {
                /* progress goes, settings stay */
                uint8_t mv = g->save.music_vol, sv = g->save.sfx_vol, spk = g->save.speaker;
                int8_t delay = g->save.audio_delay;
#if FLICKER_OPTION
                uint8_t fl = g->save.flicker;
#endif
                save_defaults(&g->save);
                g->save.music_vol = mv;
                g->save.sfx_vol = sv;
                g->save.audio_delay = delay;
                g->save.speaker = spk;
#if FLICKER_OPTION
                g->save.flicker = fl;
#endif
                save_store(&g->save);
                g->erase_confirm = 0;
                audio_sfx(SFX_DEATH);
            }
        } else if (g->options_sel == OPT_BACK) {
            audio_sfx(SFX_MENU_BACK);
            screen_go(SCR_TITLE);
        }
    }
    if (g->pressed & BTN_CIRCLE) {
        audio_sfx(SFX_MENU_BACK);
        screen_go(SCR_TITLE);
    }
}

void menus_tick(void)
{
    switch (g_game.screen) {
    case SCR_TITLE: title_tick(); break;
    case SCR_SELECT: select_tick(); break;
    case SCR_GARAGE: garage_tick(); break;
    case SCR_OPTIONS: options_tick(); break;
    }
}
