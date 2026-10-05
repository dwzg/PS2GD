/*
 * The Game Boy Advance's hardware: the registers and memory the GBA
 * frontend uses, by their GBATEK names. No library: the build is plain
 * arm-none-eabi-gcc with its own start-up code (crt0.s, gba.ld).
 */
#ifndef PD_GBA_H
#define PD_GBA_H

#include <stdint.h>

/* Code that runs often goes into IWRAM as ARM code (32-bit bus, no wait
 * states, three times as fast as Thumb from the cartridge); big buffers
 * go into EWRAM (256 KB). Plain .bss and .data are in EWRAM too (gba.ld),
 * so IWRAM holds only what is marked for it. */
#define IWRAM_CODE __attribute__((section(".iwram"), long_call, target("arm"), noinline))
#define IWRAM_DATA __attribute__((section(".iwram_data")))
#define IWRAM_BSS __attribute__((section(".iwram_bss")))
#define ALIGN4 __attribute__((aligned(4)))

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

/* Memory */
#define MEM_EWRAM 0x02000000u
#define MEM_IWRAM 0x03000000u
#define MEM_PAL 0x05000000u
#define MEM_VRAM 0x06000000u
#define MEM_OAM 0x07000000u
#define MEM_SRAM 0x0E000000u

#define PAL_BG ((volatile uint16_t *)MEM_PAL)
#define PAL_OBJ ((volatile uint16_t *)(MEM_PAL + 0x200))
#define VRAM ((volatile uint16_t *)MEM_VRAM)
/* BG character blocks (16 KB) and screen blocks (2 KB) */
#define CHARBLOCK(n) ((volatile uint32_t *)(MEM_VRAM + (n) * 0x4000u))
#define SCREENBLOCK(n) ((volatile uint16_t *)(MEM_VRAM + (n) * 0x800u))
/* sprite tiles (1D mapping), 32 KB */
#define OBJ_TILES ((volatile uint32_t *)(MEM_VRAM + 0x10000u))
#define OAM ((volatile uint16_t *)MEM_OAM)
#define SRAM ((volatile uint8_t *)MEM_SRAM)

/* Display */
#define REG_DISPCNT REG16(0x04000000)
#define REG_DISPSTAT REG16(0x04000004)
#define REG_VCOUNT REG16(0x04000006)
#define REG_BGCNT(n) REG16(0x04000008 + 2 * (n))
#define REG_BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define REG_BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define REG_WIN0H REG16(0x04000040)
#define REG_WIN1H REG16(0x04000042)
#define REG_WIN0V REG16(0x04000044)
#define REG_WIN1V REG16(0x04000046)
#define REG_WININ REG16(0x04000048)
#define REG_WINOUT REG16(0x0400004A)
#define REG_MOSAIC REG16(0x0400004C)
#define REG_BLDCNT REG16(0x04000050)
#define REG_BLDALPHA REG16(0x04000052)
#define REG_BLDY REG16(0x04000054)

#define DCNT_MODE0 0x0000
#define DCNT_OBJ_1D 0x0040
#define DCNT_BLANK 0x0080
#define DCNT_BG(n) (0x0100 << (n))
#define DCNT_OBJ 0x1000
#define DCNT_WIN0 0x2000
#define DCNT_WIN1 0x4000
#define DCNT_WINOBJ 0x8000

#define DSTAT_VBL_IRQ 0x0008
#define DSTAT_HBL_IRQ 0x0010
#define DSTAT_VCT_IRQ 0x0020
#define DSTAT_VCT(n) ((n) << 8)

#define BG_PRIO(n) (n)
#define BG_CBB(n) ((n) << 2)
#define BG_MOSAIC 0x0040
#define BG_4BPP 0x0000
#define BG_8BPP 0x0080
#define BG_SBB(n) ((n) << 8)
#define BG_SIZE_32x32 0x0000
#define BG_SIZE_64x32 0x4000
#define BG_SIZE_32x64 0x8000

/* a screen entry: tile, flips, palette bank */
#define SE_HFLIP 0x0400
#define SE_VFLIP 0x0800
#define SE_PAL(n) ((n) << 12)

/* blending: first targets (bits 0-5), mode, second targets (bits 8-13) */
#define BLD_BG(n) (1 << (n))
#define BLD_OBJ 0x0010
#define BLD_BACKDROP 0x0020
#define BLD_ALL 0x003F
#define BLD_OFF 0x0000
#define BLD_ALPHA 0x0040
#define BLD_WHITE 0x0080
#define BLD_BLACK 0x00C0
#define BLD_BOT(x) ((x) << 8)

/* OAM attributes */
#define ATTR0_Y(y) ((y) & 0xFF)
#define ATTR0_AFFINE 0x0100
#define ATTR0_HIDE 0x0200
#define ATTR0_DOUBLE 0x0300 /* affine, drawn in a box twice the size */
#define ATTR0_BLEND 0x0400
#define ATTR0_WINDOW 0x0800
#define ATTR0_SQUARE 0x0000
#define ATTR0_WIDE 0x4000
#define ATTR0_TALL 0x8000
#define ATTR1_X(x) ((x) & 0x1FF)
#define ATTR1_AFF(n) ((n) << 9)
#define ATTR1_HFLIP 0x1000
#define ATTR1_VFLIP 0x2000
#define ATTR1_SIZE(n) ((n) << 14)
#define ATTR2_TILE(n) ((n) & 0x3FF)
#define ATTR2_PRIO(n) ((n) << 10)
#define ATTR2_PAL(n) ((n) << 12)

/* Sound */
#define REG_SOUND1CNT_L REG16(0x04000060)
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
#define REG_SOUNDBIAS REG16(0x04000088)
#define REG_FIFO_A 0x040000A0u
#define REG_FIFO_B 0x040000A4u

#define SNDA_VOL_100 0x0004
#define SNDA_R 0x0100
#define SNDA_L 0x0200
#define SNDA_TIMER1 0x0400
#define SNDA_RESET 0x0800
#define SNDB_VOL_100 0x0008
#define SNDB_R 0x1000
#define SNDB_L 0x2000
#define SNDB_RESET 0x8000
#define SNDSTAT_ENABLE 0x0080

/* DMA */
#define REG_DMA_SAD(n) REG32(0x040000B0 + 12 * (n))
#define REG_DMA_DAD(n) REG32(0x040000B4 + 12 * (n))
#define REG_DMA_CNT_L(n) REG16(0x040000B8 + 12 * (n))
#define REG_DMA_CNT_H(n) REG16(0x040000BA + 12 * (n))
#define REG_DMA_CNT(n) REG32(0x040000B8 + 12 * (n))

#define DMA_DST_INC 0x0000
#define DMA_DST_DEC 0x0020
#define DMA_DST_FIXED 0x0040
#define DMA_DST_RELOAD 0x0060
#define DMA_SRC_INC 0x0000
#define DMA_SRC_DEC 0x0080
#define DMA_SRC_FIXED 0x0100
#define DMA_REPEAT 0x0200
#define DMA_16 0x0000
#define DMA_32 0x0400
#define DMA_NOW 0x0000
#define DMA_AT_VBLANK 0x1000
#define DMA_AT_HBLANK 0x2000
#define DMA_AT_SPECIAL 0x3000 /* sound FIFO for DMA 1 and 2 */
#define DMA_IRQ 0x4000
#define DMA_ENABLE 0x8000

/* Timers */
#define REG_TM_D(n) REG16(0x04000100 + 4 * (n))
#define REG_TM_CNT(n) REG16(0x04000102 + 4 * (n))
#define TM_FREQ_1 0x0000
#define TM_FREQ_64 0x0001
#define TM_FREQ_256 0x0002
#define TM_FREQ_1024 0x0003
#define TM_CASCADE 0x0004
#define TM_IRQ 0x0040
#define TM_ENABLE 0x0080

/* Keys (a bit is 0 while the key is down) */
#define REG_KEYINPUT REG16(0x04000130)
#define KEY_A 0x0001
#define KEY_B 0x0002
#define KEY_SELECT 0x0004
#define KEY_START 0x0008
#define KEY_RIGHT 0x0010
#define KEY_LEFT 0x0020
#define KEY_UP 0x0040
#define KEY_DOWN 0x0080
#define KEY_R 0x0100
#define KEY_L 0x0200
#define KEY_MASK 0x03FF

/* Interrupts, wait states */
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_WAITCNT REG16(0x04000204)
#define REG_IME REG16(0x04000208)
#define REG_IFBIOS (*(volatile uint16_t *)0x03007FF8)
#define REG_ISR_MAIN (*(void (*volatile *)(void))0x03007FFC)

#define IRQ_VBLANK 0x0001
#define IRQ_HBLANK 0x0002
#define IRQ_VCOUNT 0x0004
#define IRQ_TIMER(n) (0x0008 << (n))
#define IRQ_DMA(n) (0x0100 << (n))
#define IRQ_KEYPAD 0x1000

/* mGBA's debug output (ignored by hardware): a line of text in its log */
#define REG_DEBUG_ENABLE REG16(0x04FFF780)
#define REG_DEBUG_FLAGS REG16(0x04FFF700)
#define REG_DEBUG_STRING ((volatile char *)0x04FFF600)

/* 15-bit colour from 8-bit channels */
#define RGB15(r, g, b) ((uint16_t)(((r) >> 3) | (((g) >> 3) << 5) | (((b) >> 3) << 10)))

/* Sleep until a vertical blank, or return at once if one came since the
 * last wait: the BIOS's IntrWait(0, VBlank). (VBlankIntrWait would forget
 * one that came between the caller's last look and the call; a stale one
 * costs the caller a look more.) */
static inline void vblank_intr_wait(void)
{
    register uint32_t r0 __asm__("r0") = 0, r1 __asm__("r1") = 1;
    __asm__ volatile("swi 0x04" : "+r"(r0), "+r"(r1) : : "r2", "r3", "memory");
}

/* DMA 3 copies (the CPU stops while it runs); counts in 32-bit words. The
 * vertical blank's interrupt uses DMA 3 too: it must not come between
 * setting the registers and starting the copy. */
static inline void dma3_copy32(volatile void *dst, const void *src, uint32_t words)
{
    uint16_t ime = REG_IME;
    REG_IME = 0;
    REG_DMA_SAD(3) = (uint32_t)src;
    REG_DMA_DAD(3) = (uint32_t)dst;
    REG_DMA_CNT(3) = words | ((uint32_t)(DMA_ENABLE | DMA_32) << 16);
    REG_IME = ime;
}

static inline void dma3_fill32(volatile void *dst, uint32_t value, uint32_t words)
{
    volatile uint32_t v = value;
    uint16_t ime = REG_IME;
    REG_IME = 0;
    REG_DMA_SAD(3) = (uint32_t)&v;
    REG_DMA_DAD(3) = (uint32_t)dst;
    REG_DMA_CNT(3) = words | ((uint32_t)(DMA_ENABLE | DMA_32 | DMA_SRC_FIXED) << 16);
    REG_IME = ime;
}

#endif
