/* PS2 frontend internals. */
#ifndef PD_PS2_PLATFORM_H
#define PD_PS2_PLATFORM_H

#include "../core/common.h"

int gfx_ps2_init(void);
int gfx_ps2_is_pal(void);
void gfx_ps2_begin(void);
void gfx_ps2_end(void);

int pad_ps2_init(void);
uint32_t pad_ps2_read(void);

int audio_ps2_init(void);

int save_ps2_init(void);

/* Builds the memory card icon model (.icn) into buf; returns its size. */
int icon_ps2_build(uint8_t *buf, int cap);

#endif
