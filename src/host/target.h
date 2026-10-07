/*
 * The PC build plays the PS2's layout, or the PSP's when built with PSP=1
 * (PD_PSP, to try it on a PC), or the DS's with NDS=1 (PD_NDS): see
 * src/ps2/target.h.
 */
#ifdef PD_PSP
#include "../psp/target.h"
#elif defined(PD_NDS)
#include "../nds/target.h"
#else
#include "../ps2/target.h"
#endif
