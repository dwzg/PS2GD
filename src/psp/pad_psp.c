/* PSP buttons via sceCtrl; the analog nub doubles as the d-pad. */
#include <pspctrl.h>

#include "psp_platform.h"

/* the HOME dialog goes away once the bit has been clear for this many reads
 * (a bit that drops out for a frame doesn't let the game go on for one) */
#define HOME_CLEAR_READS 3

static int s_nub_armed; /* see stick_dpad() */
static int s_home, s_home_clear; /* see pad_psp_home() */

void pad_psp_init(void)
{
    sceCtrlSetSamplingCycle(0); /* sample at each vblank */
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

uint32_t pad_psp_read(void)
{
    SceCtrlData d;
    if (sceCtrlPeekBufferPositive(&d, 1) <= 0) return 0;
    /* in user mode the HOME bit is set while the system's HOME dialog is up */
    if (d.Buttons & PSP_CTRL_HOME) {
        s_home = 1;
        s_home_clear = 0;
    } else if (s_home && ++s_home_clear >= HOME_CLEAR_READS) {
        s_home = 0;
    }
    static const struct {
        unsigned psp;
        uint32_t btn;
    } map[] = {
        {PSP_CTRL_CROSS, BTN_CROSS},     {PSP_CTRL_CIRCLE, BTN_CIRCLE}, {PSP_CTRL_SQUARE, BTN_SQUARE},
        {PSP_CTRL_TRIANGLE, BTN_TRIANGLE}, {PSP_CTRL_UP, BTN_UP},       {PSP_CTRL_DOWN, BTN_DOWN},
        {PSP_CTRL_LEFT, BTN_LEFT},       {PSP_CTRL_RIGHT, BTN_RIGHT},   {PSP_CTRL_LTRIGGER, BTN_L1},
        {PSP_CTRL_RTRIGGER, BTN_R1},     {PSP_CTRL_START, BTN_START},   {PSP_CTRL_SELECT, BTN_SELECT},
    };
    uint32_t b = 0;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (d.Buttons & map[i].psp) b |= map[i].btn;
    return b | stick_dpad(d.Lx, d.Ly, &s_nub_armed);
}

int pad_psp_home(void)
{
    return s_home;
}
