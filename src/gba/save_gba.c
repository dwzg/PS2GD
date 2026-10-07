/*
 * Saves in the cartridge's battery-backed SRAM: the core's SaveData
 * (src/core/save.c), byte for byte the save of the other versions. SRAM
 * has an 8-bit bus, so it is read and written a byte at a time (no
 * memcpy, which would use word accesses).
 */
#include "gba.h"
#include "platform.h"
#include "save.h"

/* Emulators and flash cartridges look for this string to pick the save type. */
__attribute__((used, section(".save_type"))) static const char s_save_type[] = "SRAM_V113";

/* Two copies, each in a slot of its own: a 16-byte header (a tag, so an
 * erased (0xFF) or foreign SRAM reads as no save; a sequence number; the
 * data's length and checksum), then the data. A save goes to the slot not
 * holding the latest one, its tag written last, so a save cut short (the
 * power going off as a level is finished) leaves the one before it. */
#define SLOT_SIZE 0x200
#define HEADER 16
static const uint8_t TAG[4] = {'P', 'D', 'A', '1'};
static int s_latest = -1; /* the slot holding the latest save, if any */
static uint32_t s_seq;    /* its sequence number */

typedef char save_fits[sizeof(SaveData) <= SLOT_SIZE - HEADER ? 1 : -1];

static uint32_t rd32(int at)
{
    return SRAM[at] | (uint32_t)SRAM[at + 1] << 8 | (uint32_t)SRAM[at + 2] << 16 | (uint32_t)SRAM[at + 3] << 24;
}

static void wr32(int at, uint32_t v)
{
    int i;
    for (i = 0; i < 4; i++) SRAM[at + i] = (uint8_t)(v >> (i * 8));
}

/* FNV-1a over the slot's data as it is in the SRAM */
static uint32_t slot_sum(int at, int len)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < len; i++) h = (h ^ SRAM[at + HEADER + i]) * 16777619u;
    return h;
}

/* a slot's data length, or -1 if it holds no whole save */
static int slot_len(int slot)
{
    int at = slot * SLOT_SIZE, i, len;
    for (i = 0; i < 4; i++)
        if (SRAM[at + i] != TAG[i]) return -1;
    len = (int)rd32(at + 8);
    if (len <= 0 || len > SLOT_SIZE - HEADER || slot_sum(at, len) != rd32(at + 12)) return -1;
    return len;
}

int plat_save_read(void *buf, int size)
{
    uint8_t *b = (uint8_t *)buf;
    int slot, i, len = -1, at;
    s_latest = -1;
    for (slot = 0; slot < 2; slot++) {
        int n = slot_len(slot);
        uint32_t seq = rd32(slot * SLOT_SIZE + 4);
        if (n < 0 || (s_latest >= 0 && (int32_t)(seq - s_seq) <= 0)) continue;
        s_latest = slot;
        s_seq = seq;
        len = n;
    }
    if (s_latest < 0) return -1;
    if (size > len) size = len;
    at = s_latest * SLOT_SIZE + HEADER;
    for (i = 0; i < size; i++) b[i] = SRAM[at + i];
    return size;
}

int plat_save_write(const void *buf, int size)
{
    const uint8_t *b = (const uint8_t *)buf;
    int slot = s_latest == 0 ? 1 : 0, at = slot * SLOT_SIZE, i;
    if (size <= 0 || size > SLOT_SIZE - HEADER) return -1;
    SRAM[at] = 0; /* not a save until it is whole */
    for (i = 0; i < size; i++) SRAM[at + HEADER + i] = b[i];
    wr32(at + 4, s_seq + 1);
    wr32(at + 8, (uint32_t)size);
    wr32(at + 12, slot_sum(at, size));
    for (i = 0; i < 4; i++) SRAM[at + i] = TAG[i];
    s_latest = slot;
    s_seq++;
    return 0;
}

const char *plat_name(void)
{
    return "GBA";
}
