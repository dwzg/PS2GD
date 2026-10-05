@ Start-up code for the Game Boy Advance build: the cartridge header, then
@ stacks, wait states, the copies of the IWRAM and EWRAM sections out of
@ the ROM, zeroed .bss, and main. The header's logo and checksum are
@ filled in after linking by gbafix (scripts/build-gba.sh fetches it).
@ Also the interrupt handler the BIOS calls, which runs the C handler on
@ the user stack (the BIOS gives IRQ mode only 160 bytes).

    .section .crt0, "ax"
    .arm
    .global _start
_start:
    b       rom_start
    .fill   156, 1, 0               @ Nintendo logo (gbafix)
    .ascii  "PULSE DASH\0\0"        @ title, 12 characters
    .ascii  "PDSE"                  @ game code
    .ascii  "00"                    @ maker code
    .byte   0x96                    @ fixed value
    .byte   0                       @ main unit
    .byte   0                       @ device type
    .fill   7, 1, 0
    .byte   0                       @ version
    .byte   0                       @ header checksum (gbafix)
    .hword  0

rom_start:
    @ IRQ mode stack, then system mode (user stack, below the BIOS area)
    mov     r0, #0x12
    msr     cpsr_c, r0
    ldr     sp, =0x03007FA0
    mov     r0, #0x1F
    msr     cpsr_c, r0
    ldr     sp, =0x03007F00

    @ cartridge wait states 3,1 with the prefetch buffer; SRAM 8 cycles
    ldr     r0, =0x04000204
    ldr     r1, =0x4317
    strh    r1, [r0]

    ldr     r0, =__iwram_lma
    ldr     r1, =__iwram_start
    ldr     r2, =__iwram_end
    bl      copy_words
    ldr     r0, =__ewram_lma
    ldr     r1, =__ewram_start
    ldr     r2, =__ewram_end
    bl      copy_words

    mov     r0, #0
    ldr     r1, =__iwram_bss_start
    ldr     r2, =__iwram_bss_end
    bl      fill_words
    mov     r0, #0
    ldr     r1, =__bss_start
    ldr     r2, =__bss_end
    bl      fill_words

    ldr     r0, =isr_master
    ldr     r1, =0x03007FFC
    str     r0, [r1]

    ldr     r0, =main
    mov     lr, pc
    bx      r0
1:  b       1b

@ copy [r1, r2) from r0, a word at a time
copy_words:
    cmp     r1, r2
    ldrlo   r3, [r0], #4
    strlo   r3, [r1], #4
    blo     copy_words
    bx      lr

@ fill [r1, r2) with r0
fill_words:
    cmp     r1, r2
    strlo   r0, [r1], #4
    blo     fill_words
    bx      lr
    .pool

@ The BIOS calls this in IRQ mode (it has saved r0-r3, r12 and lr). It
@ acknowledges the interrupts, sets the flags the BIOS's IntrWait waits
@ for, and calls gba_irq(flags) in system mode with interrupts still off.
    .section .iwram, "ax"
    .arm
    .align 2
    .global isr_master
isr_master:
    mov     r3, #0x04000000
    ldr     r2, [r3, #0x200]!       @ IE | IF << 16, r3 = 0x04000200
    and     r0, r2, r2, lsr #16     @ IE & IF
    strh    r0, [r3, #2]            @ acknowledge
    ldr     r1, =0x03007FF8
    ldrh    r2, [r1]
    orr     r2, r2, r0
    strh    r2, [r1]                @ for IntrWait

    mrs     r2, spsr
    stmfd   sp!, {r2, lr}
    mov     r2, #0x9F               @ system mode, IRQs off
    msr     cpsr_c, r2
    stmfd   sp!, {lr}
    ldr     r1, =gba_irq
    mov     lr, pc
    bx      r1
    ldmfd   sp!, {lr}
    mov     r2, #0x92               @ back to IRQ mode
    msr     cpsr_c, r2
    ldmfd   sp!, {r2, lr}
    msr     spsr_cf, r2
    bx      lr
    .pool
