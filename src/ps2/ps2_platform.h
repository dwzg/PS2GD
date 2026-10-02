/* PS2 frontend internals. */
#ifndef PD_PS2_PLATFORM_H
#define PD_PS2_PLATFORM_H

#include "../core/common.h"

int gfx_ps2_init(void);
int gfx_ps2_is_pal(void);
void gfx_ps2_begin(void);
void gfx_ps2_end(void);

/* embedded = modules were not loaded from the BIOS; use ps2_drivers. */
int pad_ps2_init(int embedded);
uint32_t pad_ps2_read(void);
void pad_ps2_debug(int *state0, int *raw0, int *open0);

int audio_ps2_init(void);
/* Number of audio chunks streamed so far (diagnostics). */
unsigned audio_ps2_chunks(void);
void audio_ps2_debug(int *loops, int *avail, int *queued);

int save_ps2_init(int embedded);

/* Builds the memory card icon model (.icn) into buf; returns its size. */
int icon_ps2_build(uint8_t *buf, int cap);

#endif
