/*
 * IOP modules carried inside the ELF. The .irx files are ps2sdk's own
 * ($PS2SDK/iop/irx, Academic Free License 2.0); the Makefile turns them
 * into C arrays with ps2sdk's bin2c tool.
 */
#include <kernel.h>
#include <loadfile.h>
#include <stdio.h>

#include "ps2_platform.h"

#define IRX(name)                     \
    extern unsigned char name##_irx[]; \
    extern unsigned int size_##name##_irx;
IRX(libsd)
IRX(audsrv)
IRX(sio2man)
IRX(padman)
IRX(mcman)
IRX(mcserv)

static int load(const char *name, unsigned char *image, unsigned int size)
{
    int res = 0;
    int id = SifExecModuleBuffer(image, size, 0, NULL, &res);
    if (id < 0 || res == 1) { /* 1 = NO_RESIDENT_END: the module did not stay */
        printf("pulsedash: loading %s failed (%d, %d)\n", name, id, res);
        return -1;
    }
    return 0;
}

#define LOAD(name) load(#name, name##_irx, size_##name##_irx)

int irx_load_audio(void)
{
    return LOAD(libsd) < 0 || LOAD(audsrv) < 0 ? -1 : 0;
}

/* shared by the pad and memory card modules; loaded once */
static int load_sio2man(void)
{
    static int loaded;
    if (!loaded && LOAD(sio2man) < 0) return -1;
    loaded = 1;
    return 0;
}

int irx_load_pad(void)
{
    return load_sio2man() < 0 || LOAD(padman) < 0 ? -1 : 0;
}

int irx_load_memcard(void)
{
    return load_sio2man() < 0 || LOAD(mcman) < 0 || LOAD(mcserv) < 0 ? -1 : 0;
}
