/* The prepared frame and its copy to the hardware (video.h). */
#include <string.h>

#include "video.h"
#include "gen.h"

VideoRegs g_vid;
ObjAttr g_oam[128] ALIGN4; /* (EWRAM: the fast RAM went to the mixer; a frame writes it little) */
uint16_t g_pal_bg[256] ALIGN4, g_pal_obj[256] ALIGN4;

/* (a line more than the screen: DMA 0 also runs after the last line) */
static uint16_t s_hdma[2][SCR_H + 1][HDMA_COLORS] IWRAM_BSS ALIGN4; /* (fast to make, and to copy) */
uint16_t (*g_hdma)[HDMA_COLORS] = s_hdma[0];
static uint8_t s_hdma_shown; /* the table DMA 0 reads this frame */

static int s_nobj, s_naff;

/* the palettes as shown while the screen fades: background, then sprites */
static uint16_t s_faded[512] ALIGN4;
static uint8_t s_fade;

#define QUEUE_MAX 48
static struct {
    volatile void *dst;
    const void *src;
    uint32_t words;
} s_queue[QUEUE_MAX];
static volatile int s_nqueue;

void video_init_hw(void)
{
    int i;
    REG_DISPCNT = DCNT_BLANK;
    dma3_fill32(VRAM, 0, 0x18000 / 4);
    dma3_fill32((void *)MEM_PAL, 0, 0x400 / 4);
    for (i = 0; i < 128; i++) {
        g_oam[i].attr0 = ATTR0_HIDE;
        g_oam[i].attr1 = g_oam[i].attr2 = 0;
        g_oam[i].aff = 0;
    }
    dma3_copy32((void *)MEM_OAM, g_oam, sizeof(g_oam) / 4);
    memset(&g_vid, 0, sizeof(g_vid));
    g_vid.dispcnt = DCNT_MODE0 | DCNT_OBJ_1D | DCNT_BLANK;
    g_vid.bgcnt[0] = BG_PRIO(0) | BG_CBB(CB_SHARED) | BG_SBB(SB_TEXT) | BG_SIZE_64x32;
    g_vid.bgcnt[1] = BG_PRIO(1) | BG_CBB(CB_LEVEL) | BG_SBB(SB_LEVEL);
    g_vid.bgcnt[2] = BG_PRIO(2) | BG_CBB(CB_SHARED) | BG_SBB(SB_GROUND);
    g_vid.bgcnt[3] = BG_PRIO(3) | BG_CBB(CB_SHARED) | BG_SBB(SB_SQUARES) | BG_SIZE_64x32;
    /* sprites marked see-through add their light to what is below */
    g_vid.bldalpha = 16 | (16 << 8);
}

IWRAM_CODE void video_commit(void)
{
    int i;
    /* per-scanline colours: the new table from the next line on */
    REG_DMA_CNT_H(0) = 0;
    if (s_fade) {
        dma3_copy32((void *)MEM_PAL, s_faded, 512 / 2);
    } else {
        dma3_copy32((void *)MEM_PAL, g_pal_bg, 256 / 2);
        dma3_copy32((void *)(MEM_PAL + 0x200), g_pal_obj, 256 / 2);
    }
    dma3_copy32((void *)MEM_OAM, g_oam, sizeof(g_oam) / 4);
    for (i = 0; i < s_nqueue; i++) dma3_copy32(s_queue[i].dst, s_queue[i].src, s_queue[i].words);
    s_nqueue = 0;

    REG_DISPCNT = g_vid.dispcnt;
    for (i = 0; i < 4; i++) {
        REG_BGCNT(i) = g_vid.bgcnt[i];
        REG_BGHOFS(i) = g_vid.hofs[i];
        REG_BGVOFS(i) = g_vid.vofs[i];
    }
    REG_BLDCNT = g_vid.bldcnt;
    REG_BLDALPHA = g_vid.bldalpha;
    REG_BLDY = g_vid.bldy;
    REG_WIN0H = g_vid.win0h;
    REG_WIN0V = g_vid.win0v;
    REG_WIN1H = g_vid.win1h;
    REG_WIN1V = g_vid.win1v;
    REG_WININ = g_vid.winin;
    REG_WINOUT = g_vid.winout;

    s_hdma_shown = (uint8_t)(g_hdma == s_hdma[1]);
    video_hdma_restart();
    /* and the next frame is prepared in the other table */
    g_hdma = s_hdma[!s_hdma_shown];
}

IWRAM_CODE void video_hdma_restart(void)
{
    /* line 0's colours now, then DMA 0 sets line y + 1's in the horizontal
     * blank after line y (its source goes on from where it stopped: it
     * must start again at every frame's top) */
    REG_DMA_CNT_H(0) = 0;
    dma3_copy32((void *)MEM_PAL, s_hdma[s_hdma_shown][0], HDMA_COLORS / 2);
    REG_DMA_SAD(0) = (uint32_t)s_hdma[s_hdma_shown][1];
    REG_DMA_DAD(0) = MEM_PAL;
    REG_DMA_CNT_L(0) = HDMA_COLORS / 2;
    REG_DMA_CNT_H(0) = DMA_ENABLE | DMA_AT_HBLANK | DMA_REPEAT | DMA_32 | DMA_DST_RELOAD;
}

/* the colours a flash lights, a bit each, by palette bank: the world's
 * two background banks; each sprite bank's own colours (1..GLOW_FIRST-1:
 * not its glow's), not the text's or the particles', and in the misc bank
 * not the finish line's or the saws' glow */
static const uint16_t s_flash_bg[16] = {0xFFFF, 0xFFFF};
#define OWN ((1u << GLOW_FIRST) - 2u)
static const uint16_t s_flash_obj[16] = {
    OWN, 0, OWN, OWN, OWN, OWN, OWN, OWN, OWN, OWN, OWN, OWN, OWN, OWN,
    (uint16_t)(0xFFFEu & ~MISC_GATE_COLORS & ~(1u << MISC_GLOW)), 0,
};
#undef OWN

IWRAM_CODE void video_fade(int k, int flash)
{
    int i;
    uint32_t m = (uint32_t)(16 - k), w = (uint32_t)(31 * flash + 128) >> 8;
    s_fade = (uint8_t)(k || w);
    if (!s_fade) return;
    if (!w) {
        /* (a word, two colours, at a time: the palettes are one after the
         * other in s_faded) */
        for (i = 0; i < 256; i++) {
            const uint32_t *src = (const uint32_t *)(i < 128 ? g_pal_bg : g_pal_obj) + (i & 127);
            uint32_t v = *src, a = v & 0xFFFF, b = v >> 16, sa, sb;
            /* each channel times (16 - k) / 16, all three at once (5 bits
             * apart, each product under 512: spaced 10 bits) */
            sa = ((a & 31) | (a & 0x3E0) << 5 | (a & 0x7C00) << 10) * m;
            sb = ((b & 31) | (b & 0x3E0) << 5 | (b & 0x7C00) << 10) * m;
            ((uint32_t *)s_faded)[i] = (sa >> 4 & 31) | (sa >> 9 & 0x3E0) | (sa >> 14 & 0x7C00) |
                                       ((sb >> 4 & 31) | (sb >> 9 & 0x3E0) | (sb >> 14 & 0x7C00)) << 16;
        }
        return;
    }
    for (i = 0; i < 512; i++) {
        uint32_t c = i < 256 ? g_pal_bg[i] : g_pal_obj[i - 256];
        uint32_t lit = (i < 256 ? s_flash_bg : s_flash_obj)[(i >> 4) & 15];
        if (!lit && m == 16) {
            /* (a bank with nothing to do: copied, a word at a time) */
            const uint32_t *src = (const uint32_t *)(i < 256 ? &g_pal_bg[i] : &g_pal_obj[i - 256]);
            uint32_t *dst = (uint32_t *)&s_faded[i];
            int j;
            for (j = 0; j < 8; j++) dst[j] = src[j];
            i += 15;
            continue;
        }
        if (lit >> (i & 15) & 1) {
            /* white added, each channel to 31 at most */
            uint32_t r = (c & 31) + w, g = (c >> 5 & 31) + w, b = (c >> 10 & 31) + w;
            c = (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (b > 31 ? 31 : b) << 10;
        }
        if (m != 16) {
            uint32_t s = ((c & 31) | (c & 0x3E0) << 5 | (c & 0x7C00) << 10) * m;
            c = (s >> 4 & 31) | (s >> 9 & 0x3E0) | (s >> 14 & 0x7C00);
        }
        s_faded[i] = (uint16_t)c;
    }
}

void video_obj_begin(void)
{
    s_nobj = 0;
    s_naff = 0;
}

int video_obj(uint16_t attr0, uint16_t attr1, uint16_t attr2)
{
    if (s_nobj >= 128) return -1;
    g_oam[s_nobj].attr0 = attr0;
    g_oam[s_nobj].attr1 = attr1;
    g_oam[s_nobj].attr2 = attr2;
    return s_nobj++;
}

void video_obj_end(void)
{
    int i;
    for (i = s_nobj; i < 128; i++) g_oam[i].attr0 = ATTR0_HIDE;
}

int video_hdma_index(void)
{
    return g_hdma == s_hdma[1];
}

int video_obj_count(void)
{
    return s_nobj | (s_naff << 8);
}

int32_t video_sin(uint16_t angle)
{
    return g_sin_tab[angle >> 6];
}

int video_aff(uint16_t angle, int32_t sx, int32_t sy)
{
    int32_t c, s;
    int n;
    if (s_naff >= 32 || sx == 0 || sy == 0) return -1;
    n = s_naff++;
    c = video_cos(angle);
    s = video_sin(angle);
    /* the matrix maps the screen onto the picture: the inverse of rotating
     * and scaling it (y grows downwards, so a positive angle is clockwise) */
    g_oam[n * 4 + 0].aff = (int16_t)(c * 4 / sx);
    g_oam[n * 4 + 1].aff = (int16_t)(s * 4 / sx);
    g_oam[n * 4 + 2].aff = (int16_t)(-s * 4 / sy);
    g_oam[n * 4 + 3].aff = (int16_t)(c * 4 / sy);
    return n;
}

int video_queue(volatile void *dst, const void *src, uint32_t words)
{
    if (s_nqueue >= QUEUE_MAX) return 0;
    s_queue[s_nqueue].dst = dst;
    s_queue[s_nqueue].src = src;
    s_queue[s_nqueue].words = words;
    s_nqueue++;
    return 1;
}
