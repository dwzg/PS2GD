/* The frame loop's counters (main.c), shared with the interrupt handler. */
#ifndef PD_GBA_FRAME_H
#define PD_GBA_FRAME_H

#include <stdint.h>

typedef struct {
    volatile uint32_t vblanks; /* vertical blanks since boot */
    uint32_t ticks;            /* game ticks run (catches up with vblanks) */
    volatile uint32_t late;    /* vertical blanks with no new picture ready */
    volatile uint8_t ready;    /* a picture is prepared, to show at the next one */
    uint8_t running;           /* a picture has been shown (or is about to be) */
    uint16_t keys;             /* the pad in the latest tick (GBA KEY_* bits) */
    uint16_t work;             /* the latest frame's work (ticks and drawing), in scanlines */
    uint16_t work_max;         /* the most since start-up (for tests and profiling) */
    uint32_t begun;            /* the vertical blank the frame being made began after */
    uint16_t irq, irq_max;     /* the vertical blank's interrupt, in scanlines: the latest, the most */
} GbaFrames;

/* The scanlines the frame being made has left before the next vertical
 * blank, 0 if it is already late (for work that can wait for the next
 * frame: packing text). */
int frame_lines_left(void);

extern GbaFrames g_frames;

#endif
