/*
 * The PC build plays the PS2's layout, or the PSP's when built with PSP=1
 * (PD_PSP, to try it on a PC): see src/ps2/target.h.
 */
#ifdef PD_PSP
#include "../psp/target.h"
#else
#include "../ps2/target.h"
#endif
