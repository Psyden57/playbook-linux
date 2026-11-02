@ chainloader.S — CRC-path state probe for the PlayBook
@ Linked at 0x80100000 (image + 0x800), reached by the header's ARM b +0x800.
@ Entry state: ARM, SVC, MMU off, IRQ/FIQ masked, WDT2 armed.
@ Writes breadcrumbs at 0x90000000 + mirrors, blinks the FAN5702 EN pin,
@ emits a UART3 marker, then parks (WDT2 warm-resets ~58 s later).

        .syntax unified
        .arm
        .text
        .global _start

        .equ GPIO1,       0x4A310000
        .equ GPIO_CLEAR,  0x190
        .equ GPIO_SET,    0x194
        .equ EN_BIT,      0x2000
        .equ UART3,       0x48020000
        .equ BC_PRIMARY,  0x90000000
        .equ BC_MIR1,     0x94000000
        .equ BC_MIR2,     0x88000000
        .equ BC_MIR3,     0x9FE00000

_start:
        ldr     sp, =0x800F0000          @ own stack, below the image

        @ disable D-cache and I-cache
        mrc     p15, 0, r0, c1, c0, 0
        bic     r0, r0, #(1 << 2)        @ C
        bic     r0, r0, #(1 << 12)       @ I
        mcr     p15, 0, r0, c1, c0, 0

        @ VBAR -> our park loop (any exception lands there)
        ldr     r0, =probe_park
        mcr     p15, 0, r0, c12, c0, 0

        @ breadcrumb block
        ldr     r4, =BC_PRIMARY
        ldr     r0, =0x4C424B43          @ "KBLB"
        str     r0, [r4, #0]
        mov     r0, #0
        str     r0, [r4, #4]             @ stage 0: entered
        ldr     r0, =0x50424F42          @ "PBob"
        str     r0, [r4, #8]             @ nonce
        bl      bc_mirrors

        @ --- state snapshot ---
        mrc     p15, 0, r0, c1, c0, 0
        str     r0, [r4, #0x10]          @ SCTLR
        mrs     r0, CPSR
        str     r0, [r4, #0x14]          @ CPSR
        mrc     p15, 0, r0, c12, c0, 0
        str     r0, [r4, #0x18]          @ VBAR
        mrc     p15, 0, r0, c2, c0, 0
        str     r0, [r4, #0x1C]          @ TTBR0
        mrc     p15, 0, r0, c2, c0, 2
        str     r0, [r4, #0x20]          @ TTBCR
        mrc     p15, 0, r0, c3, c0, 0
        str     r0, [r4, #0x24]          @ DACR
        str     sp, [r4, #0x28]          @ SP

        ldr     r1, =0x80025E74
        ldr     r0, [r1]
        str     r0, [r4, #0x2C]          @ loader's jump-target global
        ldr     r1, =0x8002F144
        ldr     r0, [r1]
        str     r0, [r4, #0x30]          @ descriptor pointer

        ldr     r1, =0x800FF800
        ldr     r0, [r1, #0x04]
        str     r0, [r4, #0x34]          @ LoadStart
        ldr     r0, [r1, #0x08]
        str     r0, [r4, #0x38]          @ LoadEnd
        ldr     r0, [r1, #0x24]
        str     r0, [r4, #0x3C]          @ LoadBase
        ldr     r0, [r1, #0x28]
        str     r0, [r4, #0x40]          @ MetricsPtr
        ldr     r0, [r1, #0x7C]
        str     r0, [r4, #0x44]          @ hardware_id
        ldr     r0, [r1, #0x9C]
        str     r0, [r4, #0x48]          @ magic
        ldr     r0, [r1, #0xD4]
        str     r0, [r4, #0x4C]          @ os_type
        bl      bc_mirrors

        @ stage 1: state captured, one blink, UART marker
        mov     r0, #1
        str     r0, [r4, #4]
        bl      bc_mirrors
        mov     r0, #1
        bl      blink_n
        bl      uart_marker

        @ stage 2: header captured, two blinks
        mov     r0, #2
        str     r0, [r4, #4]
        bl      bc_mirrors
        mov     r0, #2
        bl      blink_n

        @ stage 0xDE: parked; no WDT2 kicks -> warm reset ~58 s
        mov     r0, #0xDE
        str     r0, [r4, #4]
        bl      bc_mirrors
        bl      led_off

probe_park:
        b       probe_park

@ ---------------------------------------------------------------------------
@ copy the first 0x40 words of the primary block to the three mirrors
bc_mirrors:
        ldr     r1, =BC_MIR1
        ldr     r2, =BC_MIR2
        ldr     r3, =BC_MIR3
        mov     r5, #0
1:      ldr     r0, [r4, r5, lsl #2]
        str     r0, [r1, r5, lsl #2]
        str     r0, [r2, r5, lsl #2]
        str     r0, [r3, r5, lsl #2]
        add     r5, r5, #1
        cmp     r5, #0x40
        blt     1b
        bx      lr

@ ---------------------------------------------------------------------------
@ LED: FAN5702 EN = GPIO1_13. EN low = off, high = registers default (lit).
led_off:
        ldr     r0, =GPIO1
        ldr     r1, =EN_BIT
        str     r1, [r0, #GPIO_CLEAR]
        bx      lr

led_on:
        ldr     r0, =GPIO1
        ldr     r1, =EN_BIT
        str     r1, [r0, #GPIO_SET]
        bx      lr

delay:
        ldr     r2, =0x04000000
1:      subs    r2, r2, #1
        bne     1b
        bx      lr

@ r0 = number of blinks
blink_n:
        push    {lr}
        mov     r6, r0
        cmp     r6, #0
        beq     2f
1:      bl      led_off
        bl      delay
        bl      led_on
        bl      delay
        subs    r6, r6, #1
        bne     1b
2:      bl      led_off
        pop     {lr}
        bx      lr

@ ---------------------------------------------------------------------------
@ UART3 marker (may be invisible without pads; harmless)
uart_marker:
        ldr     r1, =UART3
        adr     r2, msg
1:      ldrb    r0, [r2], #1
        cmp     r0, #0
        beq     2f
3:      ldr     r3, [r1, #0x14]          @ LSR
        tst     r3, #0x20                @ THR empty
        beq     3b
        str     r0, [r1]
        b       1b
2:      bx      lr

msg:    .asciz "PB-PROBE\r\n"
        .align  2
