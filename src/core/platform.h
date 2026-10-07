/* Functions every frontend (PS2, PC) provides to the portable core. */
#ifndef PD_PLATFORM_H
#define PD_PLATFORM_H

#include "common.h"

/* Read the save blob. Returns bytes read, or < 0 if there is no save. */
int plat_save_read(void *buf, int size);
/* Write the save blob, possibly in the background (the PS2 writes the memory
 * card from a thread of its own). Returns 0 if it was accepted. */
int plat_save_write(const void *buf, int size);

/* Short label of the platform shown in the options screen. */
const char *plat_name(void);

#if FLICKER_OPTION
/* The options' FLICKER FILTER: soften the interlaced picture against
 * flicker (on) or show it sharp; from the next frame shown. */
void plat_flicker_filter(int on);
#endif

#endif
