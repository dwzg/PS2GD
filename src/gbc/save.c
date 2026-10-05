/*
 * Progress in the cartridge's battery-backed RAM (MBC5, bank 0 at 0xa000):
 * a magic word, the SaveData block and a checksum. In the menus' ROM bank.
 */
#pragma bank 15
#include <string.h>

#include "gbc.h"

SaveData g_save;

#define SRAM ((uint8_t *)0xa000)
static const uint8_t MAGIC[4] = {'P', 'D', 'G', '2'};

/* The first version's save ("PDG1"): six levels' bests, coins and 16-bit
 * attempts, in the game's level order. */
static const uint8_t MAGIC_1[4] = {'P', 'D', 'G', '1'};
#define V1_LEVELS 6
#define V1_SIZE (V1_LEVELS * 5)

/* s = 0x5a, then for each byte s = (s rotated left by 1) ^ byte; in
 * assembly (every death saves, during play) */
static uint8_t checksum(const uint8_t *p, uint16_t n) __naked
{
    p;
    n;
    __asm
    ld l, #0x5a
1$:
    ld a, b
    or a, c
    jr z, 2$
    ld a, l
    rlca
    ld l, a
    ld a, (de)
    inc de
    xor a, l
    ld l, a
    dec bc
    jr 1$
2$:
    ld a, l
    ret
    __endasm;
}

static void defaults(void)
{
    memset(&g_save, 0, sizeof(SaveData));
    g_save.col2 = 1;
}

void save_load(void) BANKED
{
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (!memcmp(SRAM, MAGIC, 4) && SRAM[4 + sizeof(SaveData)] == checksum(SRAM + 4, sizeof(SaveData))) {
        memcpy(&g_save, SRAM + 4, sizeof(SaveData));
    } else {
        defaults();
        if (!memcmp(SRAM, MAGIC_1, 4) && SRAM[4 + V1_SIZE] == checksum(SRAM + 4, V1_SIZE)) {
            const uint8_t *v1 = SRAM + 4;
            uint8_t i;
            for (i = 0; i < V1_LEVELS; i++) {
                g_save.progress.best[i] = v1[i];
                g_save.progress.best_practice[i] = v1[V1_LEVELS + i];
                g_save.progress.coins[i] = v1[2 * V1_LEVELS + i];
                g_save.progress.attempts[i] = v1[3 * V1_LEVELS + 2 * i] | (uint16_t)v1[3 * V1_LEVELS + 2 * i + 1] << 8;
            }
        }
    }
    DISABLE_RAM_MBC5;
}

void save_write(void) BANKED
{
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memcpy(SRAM, MAGIC, 4);
    memcpy(SRAM + 4, &g_save, sizeof(SaveData));
    SRAM[4 + sizeof(SaveData)] = checksum(SRAM + 4, sizeof(SaveData));
    DISABLE_RAM_MBC5;
}
