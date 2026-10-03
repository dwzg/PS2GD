/* PSP frontend internals. */
#ifndef PD_PSP_PLATFORM_H
#define PD_PSP_PLATFORM_H

#include "../core/common.h"

/* The PSP's screen, and the row length of its frame buffers in pixels. */
#define PSP_SCR_W 480
#define PSP_SCR_H 272
#define PSP_BUF_W 512

void gfx_psp_init(void);
void gfx_psp_begin(void);
/* Close the frame's display list: the GE draws it while the CPU goes on. */
void gfx_psp_submit(void);
/* Wait until the GE has finished the frame. */
void gfx_psp_sync(void);
/* Show the finished frame. Call right after a vblank has started. */
void gfx_psp_flip(void);
/* Primitives left out because the frame's vertex buffer was full. */
unsigned gfx_psp_dropped(void);

void pad_psp_init(void);
uint32_t pad_psp_read(void);

int audio_psp_init(void);
/* Microseconds spent synthesizing so far, wrapping (PD_PERF builds only). */
unsigned audio_psp_mix_us(void);

/* Saves go into the folder of the running EBOOT (eboot_path: argv[0]). */
int save_psp_init(const char *eboot_path);
/* Wait until saves handed to plat_save_write() are written (or timeout_ms passed). */
void save_psp_flush(int timeout_ms);

#endif
