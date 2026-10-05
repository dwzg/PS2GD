/*
 * The GBA's presentation of the core's game (gba_draw.h): each screen of
 * the core's state drawn with the world (world.c), sprites (sprites.c)
 * and text (ui.c), as the vector renderer draws it on the other
 * platforms (game_draw.c, play_draw.c and demo_draw.c in src/core).
 */
#include <string.h>

#include "gba_draw.h"
#include "video.h"
#include "world.h"
#include "levels_gba.h"
#include "sprites.h"
#include "menus.h"
#include "ui.h"
#include "hud.h"
#include "game_internal.h"

void gba_draw_init_hw(void)
{
    video_init_hw();
}

static int s_screen = -1; /* the screen drawn last */
float g_frame_pulse;

void game_draw_init(void)
{
    world_init();
    sprites_init();
    ui_init();
    menus_init();
    levels_gba_init();
}

void gba_draw_commit(void)
{
    video_commit();
}

void gba_draw_hold(void)
{
    video_hdma_restart();
}

/* A menu's backdrop (draw_menu_backdrop): the ground and the squares
 * scrolling by, no level. */
/* the run's flash this frame (draw_world_of), as video_fade takes it */
static int s_flash;

/* the fade between screens as video_fade takes it */
static int fade_k(void)
{
    float f = g_game.fade;
    return f <= 0.0f ? 0 : f >= 1.0f ? 16 : (int)(f * 16.0f + 0.5f);
}

void draw_menu_world(const Palette *pal, float scroll)
{
    WorldView v;
    memset(&v, 0, sizeof(v));
    v.fade = fade_k();
    v.cam_x = scroll;
    v.cam_y = -2.4f;
    v.pal = pal;
    v.pulse = g_frame_pulse;
    v.corr_ceil = CORRIDOR_H;
    v.squares_x = scroll;
    world_draw(&v);
}

/* A run's world: the play screen's, or the title's demo run (cam_drop
 * lowers it under the menu). */
static void draw_world_of(const PlayState *ps, const Palette *pal, float cam_drop)
{
    WorldView v;
    v.L = ps->L;
    play_camera(ps, &v.cam_x, &v.cam_y);
    /* the flash (play_draw's: ps->flash / 2 of white added over the world,
     * not the HUD), in the colours: not under the pause menu (from the
     * frame it is shown) */
    v.flash = ps == &g_game.play && hud_pause_shown() ? 0 : clampi((int)(ps->flash * 128.0f), 0, 256);
    s_flash = v.flash;
    if (ps->phase != PH_COMPLETE) {
        /* the camera on a whole pixel from the player's: it is 5.65 blocks
         * behind the player, 67.8 pixels, and the two rounded apart put
         * the player 67 or 68 pixels from the edge, a pixel back and forth
         * as it runs */
        float px = sim_x(&ps->p) * BLOCK_PIX, behind = px - v.cam_x * BLOCK_PIX;
        v.cam_x = (floorf(px + 0.5f) - floorf(behind + 0.5f)) * (1.0f / BLOCK_PIX);
    } else {
        /* past the finish: a whole number of pixels short of where the
         * camera glides to a stop, and there once less than a pixel from
         * it. On a whole pixel from the player speeding off, the world
         * stepped a pixel back and forth with the player's fraction; and
         * that or rounded where it is, as it slows ever less, its last
         * pixel could come up to half a second after it seemed to have
         * stopped (the finish line, and all, moving by one) */
        float stop = play_camera_stop(ps) * BLOCK_PIX, behind = stop - v.cam_x * BLOCK_PIX;
        v.cam_x = (floorf(stop + 0.5f) - (float)(int)behind) * (1.0f / BLOCK_PIX);
    }
    v.cam_y += cam_drop;
    v.pal = pal;
    v.pulse = g_frame_pulse;
    v.corr_floor = ps->corr_floor;
    v.corr_ceil = ps->corr_ceil;
    v.corr_alpha = ps->corr_alpha;
    v.squares_x = v.cam_x;
    v.loops = ps == &g_game.demo;
    v.fade = fade_k();
    world_draw(&v);
}

void game_render(float alpha)
{
    Game *g = &g_game;
    (void)alpha;
    g_frame_pulse = beat_pulse();
    video_obj_begin();
    s_flash = 0;
    g_vid.dispcnt = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_OBJ | DCNT_BG(0) | DCNT_BG(1) | DCNT_BG(2) | DCNT_BG(3);
    /* see-through sprites add their light to everything behind them
     * (unless the frame's drawing asks for another effect) */
    g_vid.bldcnt = BLD_OFF | BLD_BOT(BLD_BG(0) | BLD_BG(1) | BLD_BG(2) | BLD_BG(3) | BLD_BACKDROP);
    g_vid.bldy = 0;
    if (g->screen != s_screen) {
        s_screen = g->screen;
        if (s_screen == SCR_PLAY) {
            sprites_palettes();
            hud_enter();
        } else {
            menus_enter(s_screen);
        }
    }
    if (g->fade >= 1.0f && g->screen != SCR_PLAY) {
        /* black between screens: nothing drawn (the title's run, say, is
         * about to move to where the music is). A run is drawn, black (the
         * fade below): its level's tiles go in while its song waits for
         * this picture, not once it plays. */
        video_obj_end();
        video_fade(0, 0);
        g_vid.bldcnt = BLD_ALL | BLD_BLACK;
        g_vid.bldy = 16;
        return;
    }
    sprites_garage(g->save.icon, g->save.col1, g->save.col2);
    if (g->screen == SCR_PLAY) {
        /* play_render: the run and, over it, draw_hud and the pause menu
         * or the results */
        hud_begin(&g->play);
        hud_sprites_front(&g->play);
        draw_world_of(&g->play, &g->play.pal, 0.0f);
        sprites_run(&g->play, g->play.phase != PH_DEAD);
        hud_sprites_world(&g->play);
        sprites_glows();
        hud_draw(&g->play);
    } else {
        menus_draw(g->screen);
        if (g->screen == SCR_TITLE) {
            /* demo_render: the run, or the backdrop until it is ready */
            Palette pal;
            menu_palette(&pal);
            if (g->demo.L) {
                draw_world_of(&g->demo, &pal, DEMO_CAM_DROP);
                sprites_run(&g->demo, 1);
                sprites_glows();
            } else {
                draw_menu_world(&pal, g->t * 6.0f);
            }
        }
        menus_draw_text(g->screen);
    }
    video_obj_end();
    ui_flush();
    /* the fade between screens: all of it darker, in the palettes (the
     * see-through panels' dimming, the blending, stays as it is: the
     * panels stay darker than what is around them) */
    video_fade(fade_k(), s_flash);
}
