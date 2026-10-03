#ifndef PD_GFX_SDL_H
#define PD_GFX_SDL_H

#include <SDL.h>

/* Start a frame targeting renderer r; virtual coords are multiplied by scale. */
void gfx_sdl_begin(SDL_Renderer *r, float scale_x, float scale_y);
/* Submit any batched triangles. Call before SDL_RenderPresent / reading pixels. */
void gfx_sdl_flush(void);

#endif
