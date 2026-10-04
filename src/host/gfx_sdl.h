#ifndef PD_GFX_SDL_H
#define PD_GFX_SDL_H

#include <SDL.h>

/* Start a frame targeting renderer r; virtual coords are multiplied by scale. */
void gfx_sdl_begin(SDL_Renderer *r, float scale_x, float scale_y);
/* Shift what is drawn from here on by (dx, dy) virtual pixels (gfx_sdl_begin
 * resets it to 0, 0). */
void gfx_sdl_offset(float dx, float dy);
/* Submit any batched triangles. Call before SDL_RenderPresent / reading pixels. */
void gfx_sdl_flush(void);

/* Primitive counts since the last gfx_sdl_begin (r may be NULL to only count). */
typedef struct {
    int tris, quads, rects, blend_switches;
} GfxStats;
const GfxStats *gfx_sdl_stats(void);

#endif
