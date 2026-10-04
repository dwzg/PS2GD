/*
 * Progress in the cartridge's battery-backed RAM (MBC5, bank 0 at 0xa000):
 * a magic word, the SaveData block and a checksum.
 */
#include <string.h>

#include "gbc.h"

SaveData g_save;

#define SRAM ((uint8_t *)0xa000)
static const uint8_t MAGIC[4] = {'P', 'D', 'G', '1'};

static uint8_t checksum(const uint8_t *p, uint16_t n)
{
    uint8_t s = 0x5a;
    while (n--) s = (uint8_t)((s << 1 | s >> 7) ^ *p++);
    return s;
}

void save_load(void)
{
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    if (!memcmp(SRAM, MAGIC, 4) && SRAM[4 + sizeof(SaveData)] == checksum(SRAM + 4, sizeof(SaveData)))
        memcpy(&g_save, SRAM + 4, sizeof(SaveData));
    else
        memset(&g_save, 0, sizeof(SaveData));
    DISABLE_RAM_MBC5;
}

void save_write(void)
{
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    memcpy(SRAM, MAGIC, 4);
    memcpy(SRAM + 4, &g_save, sizeof(SaveData));
    SRAM[4 + sizeof(SaveData)] = checksum(SRAM + 4, sizeof(SaveData));
    DISABLE_RAM_MBC5;
}
