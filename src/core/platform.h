/* Functions every frontend (PS2, PC) provides to the portable core. */
#ifndef PD_PLATFORM_H
#define PD_PLATFORM_H

#include "common.h"

/* Read the save blob. Returns bytes read, or < 0 if there is no save. */
int plat_save_read(void *buf, int size);
/* Write the save blob. Returns 0 on success. */
int plat_save_write(const void *buf, int size);

/* Short label of the platform shown in the options screen. */
const char *plat_name(void);

#endif
