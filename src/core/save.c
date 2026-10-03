#include "save.h"
#include "platform.h"

static uint32_t checksum(const SaveData *s)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(SaveData, checksum); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

void save_defaults(SaveData *s)
{
    memset(s, 0, sizeof(*s));
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->size = (uint16_t)sizeof(SaveData);
    s->icon = 0;
    s->col1 = 0;
    s->col2 = 1;
    s->music_vol = 8;
    s->sfx_vol = 8;
}

int save_load(SaveData *s)
{
    SaveData tmp;
    int n = plat_save_read(&tmp, (int)sizeof(tmp));
    if (n != (int)sizeof(tmp) || tmp.magic != SAVE_MAGIC || tmp.version != SAVE_VERSION ||
        tmp.size != sizeof(SaveData) || tmp.checksum != checksum(&tmp)) {
        save_defaults(s);
        return 0;
    }
    *s = tmp;
    return 1;
}

int save_store(SaveData *s)
{
    s->magic = SAVE_MAGIC;
    s->version = SAVE_VERSION;
    s->size = (uint16_t)sizeof(SaveData);
    s->checksum = checksum(s);
    return plat_save_write(s, (int)sizeof(*s));
}
