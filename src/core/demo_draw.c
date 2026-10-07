/* The vector family's drawing of the title screen's demo run (demo.c). */
#include "game_internal.h"
#include "fx.h"

void demo_render(const PlayState *ps, const Palette *pal)
{
    if (!ps->L) {
        draw_menu_backdrop(pal, g_game.t * 6.0f, beat_pulse());
        return;
    }
    View v;
    play_view(ps, &v);
    v.cam_y += DEMO_CAM_DROP;
    view_snap(&v);
    v.pal = pal;
    render_background(&v);
    render_ground(&v, 0.0f, CORRIDOR_H, 0.0f);
    render_level(&v, ps->L, &ps->p, 0);
    play_draw_player(ps, &v);
    fx_draw(FX_WORLD, v.cam_x, v.cam_y, (1.0f - g_game.alpha) * TICK_DT);
}
