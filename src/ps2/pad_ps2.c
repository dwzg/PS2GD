/* DualShock 2 input via libpad (port 1, falling back to port 2). */
#include <kernel.h>
#include <stdio.h>
#include <libpad.h>
#include <ps2_joystick_driver.h>

#include "ps2_platform.h"

static char s_pad_buf[2][256] __attribute__((aligned(64)));
static int s_open[2];
static int s_last_state[2], s_last_raw[2];

int pad_ps2_init(int embedded)
{
    if (embedded) {
        /* ps2_drivers loads sio2man/padman from the ELF and calls padInit */
        int ret = init_joystick_driver(true);
        if (ret < 0) {
            printf("pulsedash: joystick driver failed (%d)\n", ret);
            return -1;
        }
    } else if (padInit(0) != 1) {
        printf("pulsedash: padInit failed\n");
        return -1;
    }
    for (int port = 0; port < 2; port++) s_open[port] = padPortOpen(port, 0, s_pad_buf[port]) != 0;
    return 0;
}

static uint32_t read_port(int port)
{
    if (!s_open[port]) return 0;
    int state = padGetState(port, 0);
    s_last_state[port] = state;
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return 0;
    struct padButtonStatus st;
    if (padRead(port, 0, &st) == 0) return 0;
    uint32_t d = 0xffffu ^ st.btns;
    s_last_raw[port] = (int)d;
    uint32_t b = 0;
    if (d & PAD_CROSS) b |= BTN_CROSS;
    if (d & PAD_CIRCLE) b |= BTN_CIRCLE;
    if (d & PAD_SQUARE) b |= BTN_SQUARE;
    if (d & PAD_TRIANGLE) b |= BTN_TRIANGLE;
    if (d & PAD_UP) b |= BTN_UP;
    if (d & PAD_DOWN) b |= BTN_DOWN;
    if (d & PAD_LEFT) b |= BTN_LEFT;
    if (d & PAD_RIGHT) b |= BTN_RIGHT;
    if (d & PAD_L1) b |= BTN_L1;
    if (d & PAD_R1) b |= BTN_R1;
    if (d & PAD_L2) b |= BTN_L2;
    if (d & PAD_R2) b |= BTN_R2;
    if (d & PAD_START) b |= BTN_START;
    if (d & PAD_SELECT) b |= BTN_SELECT;
    /* left analog stick doubles as a d-pad when the pad reports analog data */
    if ((st.mode >> 4) == 0x7) {
        if (st.ljoy_h < 0x30) b |= BTN_LEFT;
        if (st.ljoy_h > 0xD0) b |= BTN_RIGHT;
        if (st.ljoy_v < 0x30) b |= BTN_UP;
        if (st.ljoy_v > 0xD0) b |= BTN_DOWN;
    }
    return b;
}

uint32_t pad_ps2_read(void)
{
    return read_port(0) | read_port(1);
}

void pad_ps2_debug(int *state0, int *raw0, int *open0)
{
    *state0 = s_last_state[0];
    *raw0 = s_last_raw[0];
    *open0 = s_open[0];
}
