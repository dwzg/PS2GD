/* PSP frontend internals. */
#ifndef PD_PSP_PLATFORM_H
#define PD_PSP_PLATFORM_H

#include "../core/common.h"

/* The PSP's screen, and the row length of its frame buffers in pixels. */
#define PSP_SCR_W 480
#define PSP_SCR_H 272
#define PSP_BUF_W 512

void gfx_psp_init(void);
/* Start a frame that must be finished by `deadline` (sceKernelGetSystemTimeLow
 * time: the vblank it is to be shown at), which decides whether there is
 * time to smooth it (gfx_gu.c). */
void gfx_psp_begin(unsigned deadline);
/* Close the frame's display list: the GE draws it while the CPU goes on. */
void gfx_psp_submit(void);
/* Wait until the GE has finished the frame, smoothing it if it can. */
void gfx_psp_sync(void);
/* Show the finished frame. Call right after a vblank has started. */
void gfx_psp_flip(void);
/* Primitives left out because the frame's vertex buffer was full. */
unsigned gfx_psp_dropped(void);
/* The smoothing since the last call (PD_PERF reports): frames smoothed,
 * the second drawing's and the blend's GE time in microseconds (total and
 * most), frames that had no time for it, frames that ended closer to the
 * vblank than the guard allows, the most batches a frame had; and frames
 * that began with a clear (the others start with a background covering the
 * screen). */
typedef struct {
    unsigned frames, again_us, again_max_us, blend_us, blend_max_us, skipped, close, batches_max, clears;
} GfxPspStats;
void gfx_psp_stats(GfxPspStats *out);

void pad_psp_init(void);
uint32_t pad_psp_read(void);
/* Whether the system's HOME dialog was up at the last read: at once with
 * the first read that says so, and until a few in a row have said not. */
int pad_psp_home(void);

int audio_psp_init(void);
/* Microseconds the mixing thread has run so far (its own run time, the
 * system calls it makes included), wrapping. */
unsigned audio_psp_mix_us(void);

/* Saves go into the folder of the running EBOOT (eboot_path: argv[0]). */
int save_psp_init(const char *eboot_path);
/* Wait until saves handed to plat_save_write() are written (or timeout_ms passed). */
void save_psp_flush(int timeout_ms);

#endif
