/*
 * PC build of Pulse Dash (SDL2). Used for development and playtesting; the
 * game code is identical to the PS2 build.
 *
 * Keys: Space/Up/W/Z/click = jump (Cross), Enter = Cross, Backspace/X = Circle,
 * Q = Square (checkpoint), E = Triangle, arrows = D-pad, Esc/P = Start.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "gfx_sdl.h"
#include "../core/audio.h"
#include "../core/draw.h"
#include "../core/game.h"
#include "../core/platform.h"

static char s_save_path[1024] = "pulsedash.sav";

int plat_save_read(void *buf, int size)
{
    FILE *f = fopen(s_save_path, "rb");
    if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)size, f);
    fclose(f);
    return n;
}

int plat_save_write(const void *buf, int size)
{
    FILE *f = fopen(s_save_path, "wb");
    if (!f) return -1;
    int n = (int)fwrite(buf, 1, (size_t)size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

const char *plat_name(void) { return "PC"; }

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    audio_mix((int16_t *)stream, len / 4);
}

static uint32_t key_buttons(const Uint8 *ks)
{
    uint32_t b = 0;
    if (ks[SDL_SCANCODE_SPACE] || ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_RETURN] ||
        ks[SDL_SCANCODE_KP_ENTER])
        b |= BTN_CROSS;
    if (ks[SDL_SCANCODE_BACKSPACE] || ks[SDL_SCANCODE_X]) b |= BTN_CIRCLE;
    if (ks[SDL_SCANCODE_Q]) b |= BTN_SQUARE;
    if (ks[SDL_SCANCODE_E]) b |= BTN_TRIANGLE;
    if (ks[SDL_SCANCODE_UP]) b |= BTN_UP;
    if (ks[SDL_SCANCODE_DOWN]) b |= BTN_DOWN;
    if (ks[SDL_SCANCODE_LEFT]) b |= BTN_LEFT;
    if (ks[SDL_SCANCODE_RIGHT]) b |= BTN_RIGHT;
    if (ks[SDL_SCANCODE_ESCAPE] || ks[SDL_SCANCODE_P]) b |= BTN_START;
    if (ks[SDL_SCANCODE_TAB]) b |= BTN_SELECT;
    return b;
}

static uint32_t pad_buttons(SDL_GameController *gc)
{
    if (!gc) return 0;
    static const struct { SDL_GameControllerButton b; uint32_t m; } map[] = {
        {SDL_CONTROLLER_BUTTON_A, BTN_CROSS},        {SDL_CONTROLLER_BUTTON_B, BTN_CIRCLE},
        {SDL_CONTROLLER_BUTTON_X, BTN_SQUARE},       {SDL_CONTROLLER_BUTTON_Y, BTN_TRIANGLE},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, BTN_UP},     {SDL_CONTROLLER_BUTTON_DPAD_DOWN, BTN_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, BTN_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, BTN_RIGHT},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, BTN_L1}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, BTN_R1},
        {SDL_CONTROLLER_BUTTON_START, BTN_START},    {SDL_CONTROLLER_BUTTON_BACK, BTN_SELECT},
    };
    uint32_t b = 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (SDL_GameControllerGetButton(gc, map[i].b)) b |= map[i].m;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) b |= BTN_L2;
    if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) b |= BTN_R2;
    return b;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    char *pref = SDL_GetPrefPath("PulseDash", "PulseDash");
    if (pref) {
        snprintf(s_save_path, sizeof(s_save_path), "%spulsedash.sav", pref);
        SDL_free(pref);
    }

    SDL_Window *win = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * 2, SCREEN_H * 2,
                                       SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);

    audio_init();
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (dev) {
        /* one callback buffer queued on average, plus the system mixer */
        audio_set_latency((float)have.samples / (float)have.freq + 0.02f);
        SDL_PauseAudioDevice(dev, 0);
    }
    else fprintf(stderr, "no audio: %s\n", SDL_GetError());

    game_init();

    SDL_GameController *gc = NULL;
    uint32_t mouse = 0;
    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    /* The game ticks at 60 Hz; the simulation runs up to one tick ahead of
     * real time and each frame is drawn interpolated back to real time. */
    double ahead = 0.0;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_CONTROLLERDEVICEADDED && !gc) gc = SDL_GameControllerOpen(e.cdevice.which);
            else if (e.type == SDL_MOUSEBUTTONDOWN) mouse = BTN_CROSS;
            else if (e.type == SDL_MOUSEBUTTONUP) mouse = 0;
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double real_dt = (double)(now - last) / (double)freq;
        last = now;
        ahead -= real_dt < 0.25 ? real_dt : 0.25;
        uint32_t held = key_buttons(SDL_GetKeyboardState(NULL)) | pad_buttons(gc) | mouse;
        while (ahead < 0.0) {
            if (dev) SDL_LockAudioDevice(dev);
            game_tick(held);
            if (dev) SDL_UnlockAudioDevice(dev);
            ahead += 1.0 / TICK_HZ;
        }

        int w, h;
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        /* letterbox to the virtual screen's shape */
        float scale = (float)w / SCREEN_W;
        if (SCREEN_H * scale > h) scale = (float)h / SCREEN_H;
        SDL_Rect vp = {(int)((w - SCREEN_W * scale) / 2), (int)((h - SCREEN_H * scale) / 2),
                       (int)(SCREEN_W * scale), (int)(SCREEN_H * scale)};
        SDL_RenderSetViewport(ren, &vp);
        gfx_sdl_begin(ren, scale, scale);
        draw_set_pixel_grid(scale); /* text and lines on whole window pixels */
        game_render((float)(1.0 - ahead * TICK_HZ));
        gfx_sdl_flush();
        SDL_RenderSetViewport(ren, NULL);
        SDL_RenderPresent(ren);
    }
    game_flush_save();
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
