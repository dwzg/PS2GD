/* The Makefiles pass PD_VERSION (from `git describe`) for this file only and
 * rebuild it every time. */
#include "version.h"

#ifndef PD_VERSION
#define PD_VERSION "dev"
#endif

const char *const g_version = PD_VERSION;
