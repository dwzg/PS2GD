/*
 * Memory layout of the Game Boy Advance build.
 *
 * The cartridge ROM holds the code (Thumb) and constant data, the levels
 * (parsed at build time) among it. IWRAM (32 KB, fast) holds what runs
 * most: code marked IWRAM_CODE (the particles, the interrupt handler, the
 * drawing's inner loops), the physics (src/core/sim.c, compiled as ARM
 * code), libgcc's single-precision soft-float routines and divisions, and
 * data marked IWRAM_DATA / IWRAM_BSS. Everything else that is writable
 * (.data, .bss, the heap) is in EWRAM (256 KB). The stacks are at the top
 * of IWRAM (crt0.s).
 */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
    iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 32K - 4K /* then the stacks (3.75 KB) and the BIOS's area */
    ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
}

SECTIONS {
    .crt0 : { KEEP(*(.crt0)) } > rom

    /* before .text, so that these files' code is taken out of it */
    .iwram : ALIGN(4) {
        __iwram_start = .;
        *(.iwram .iwram.*)
        *(.iwram_data .iwram_data.*)
        *core/sim.o(.text .text.* .rodata .rodata.*)
        /* libgcc's single-precision floats and divisions (the doubles,
         * used only for printing, stay in the ROM) */
        *libgcc.a:_arm_addsubsf3.o(.text .text.*)
        *libgcc.a:_arm_muldivsf3.o(.text .text.*)
        *libgcc.a:_arm_cmpsf2.o(.text .text.*)
        *libgcc.a:_arm_fixsfsi.o(.text .text.*)
        *libgcc.a:_arm_fixunssfsi.o(.text .text.*)
        *libgcc.a:_arm_unordsf2.o(.text .text.*)
        *libgcc.a:_divsi3.o(.text .text.*)
        *libgcc.a:_udivsi3.o(.text .text.*)
        *libgcc.a:_clzsi2.o(.text .text.*)
        . = ALIGN(4);
        __iwram_end = .;
    } > iwram AT > rom
    __iwram_lma = LOADADDR(.iwram);

    .iwram_bss (NOLOAD) : ALIGN(4) {
        __iwram_bss_start = .;
        *(.iwram_bss .iwram_bss.*)
        . = ALIGN(4);
        __iwram_bss_end = .;
    } > iwram

    .text : ALIGN(4) {
        *(.text .text.*)
        *(.glue_7 .glue_7t .vfp11_veneer .v4_bx)
        . = ALIGN(4);
    } > rom
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
        /* "SRAM_V113": tells emulators and flash cartridges the save type */
        KEEP(*(.save_type))
        /* where the emulator test finds the game's state (main.c) */
        KEEP(*(.test_info))
        . = ALIGN(4);
    } > rom
    .ARM.exidx : { *(.ARM.exidx*) } > rom
    .init_array : { *(.init_array*) } > rom

    .ewram : ALIGN(4) {
        __ewram_start = .;
        *(.ewram .ewram.*)
        *(.data .data.*)
        . = ALIGN(4);
        __ewram_end = .;
    } > ewram AT > rom
    __ewram_lma = LOADADDR(.ewram);

    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ewram

    /* the heap (malloc, for the parsed levels): the rest of EWRAM */
    . = ALIGN(8);
    __heap_start = .;
    __heap_end = ORIGIN(ewram) + LENGTH(ewram);

    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
}
