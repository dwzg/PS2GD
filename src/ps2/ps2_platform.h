/* PS2 frontend internals. */
#ifndef PD_PS2_PLATFORM_H
#define PD_PS2_PLATFORM_H

#include "../core/common.h"

int gfx_ps2_init(void);
int gfx_ps2_is_pal(void);
float gfx_ps2_refresh_hz(void);
void gfx_ps2_begin(void);
/* Send the frame to the GS; it draws in the background. */
void gfx_ps2_submit(void);
/* Show the submitted frame. Call right after a vblank has started. */
void gfx_ps2_flip(void);

/* embedded = modules were not loaded from the BIOS; use ps2_drivers. */
int pad_ps2_init(int embedded);
uint32_t pad_ps2_read(void);
void pad_ps2_debug(int *state0, int *raw0, int *open0);

int audio_ps2_init(void);
/* Number of audio chunks streamed so far (diagnostics). */
unsigned audio_ps2_chunks(void);
void audio_ps2_debug(int *loops, int *avail, int *queued);
/* EE cycles spent synthesizing so far, wrapping (PD_PERF builds only). */
unsigned audio_ps2_mix_cycles(void);

int save_ps2_init(int embedded);

/* Builds the memory card icon model (.icn) into buf; returns its size. */
int icon_ps2_build(uint8_t *buf, int cap);

/* EE cycle counter (COP0 Count: 294.912 MHz on hardware; emulators may
 * count executed instructions instead). */
static inline unsigned ps2_cycles(void)
{
    unsigned c;
    __asm__ volatile("mfc0 %0, $9" : "=r"(c));
    return c;
}
#define PS2_EE_HZ 294912000.0f

#endif
