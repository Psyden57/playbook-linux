@ chainloader_kernel.S.in — CRC-path chainloader: copy zImage+DTB high, jump
@ Placeholders __...__ are substituted by build_kernel_image.py.
@ Entry: 0x80100000, ARM, SVC, MMU off, IRQ/FIQ masked, WDT2 armed.

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

        @ Epoch test (report 108 §6.3): delay kernel entry by a fixed wall
        @ clock time, measured on the always-on WDT2 counter (32.768 kHz,
        @ CRR counts UP from LDR towards 0xffffffff, overflow = reset).
        @ WDT2 is the one timer we *know* is running here: it is what
        @ resets the SoC ~58.6 s after the kernel halts.  0 disables.
        .equ WDT2_CRR,    0x4A314028
        .equ WDT2_BASE,   0x4A314000
        .equ SYNC32K,     0x4A304010
        .equ EP_TICKS,    0x00000000

        @ WDT2 reload/kick is done from the kernel instead (report 109 §8.1):
        @ a loader-side write diagnostic wedged the loader and produced no
        @ trustworthy readback.  Loader keeps its hands off the watchdog.

        .equ ZIMAGE_SRC,  0x803BF800
        .equ ZIMAGE_SIZE, 0x0081A768
        .equ DTB_SRC,     0x80BD9F68
        .equ DTB_SIZE,    0x000166C0
        .equ KERNEL_DEST, 0x84000000
        .equ DTB_DEST,    0x8481A768

        @ Clear the DEBUG_LL ring state (count +0x80, index +0x84) and the
        @ first page of characters, so every boot's earlyprintk log starts
        @ clean instead of accumulating across the WDT2 reset loop.  Only the
        @ header really matters: the reader uses count/index to read exactly
        @ the bytes written this boot, so the rest of the 32 KB ring is
        @ overwritten before it can be read.  The breadcrumb words at
        @ +0x00..+0x7C are left alone (the ring starts at +0x80).
        .macro clear_ring, base
        ldr     r0, =\base
        add     r0, r0, #0x80
        mov     r1, #0
        mov     r2, #0x3E0               @ (0x1000 - 0x80) / 4 words
99:     str     r1, [r0], #4
        subs    r2, r2, #1
        bne     99b
        .endm

_start:
        ldr     sp, =0x800F0000

        @ Sample the always-on WDT2 counter as early as it is safe to, so the
        @ epoch block can separate "bootrom hand-over variance" from "this
        @ chainloader's own duration".  Kept in r11 until it can be stored.
        ldr     r5, =WDT2_CRR
        ldr     r11, [r5]

        mrc     p15, 0, r0, c1, c0, 0
        bic     r0, r0, #(1 << 2)
        bic     r0, r0, #(1 << 12)
        mcr     p15, 0, r0, c1, c0, 0

        ldr     r0, =kernel_park
        mcr     p15, 0, r0, c12, c0, 0

        ldr     r4, =BC_PRIMARY
        ldr     r0, =0x4C424B43          @ "KBLB"
        str     r0, [r4, #0]
        mov     r0, #1
        str     r0, [r4, #4]
        ldr     r0, =0x4B45524E          @ "KERN"
        str     r0, [r4, #8]
        bl      bc_mirrors
        clear_ring 0x88000000
        clear_ring 0x90000000
        clear_ring 0x94000000
        clear_ring 0x9FE00000
        str     r11, [r4, #0xa8]         @ CRR on entry to this chainloader
        mov     r0, #1
        

        @ copy zImage to 0x84000000
        ldr     r0, =ZIMAGE_SRC
        ldr     r1, =KERNEL_DEST
        ldr     r2, =ZIMAGE_SIZE
        bl      copy_words

        mov     r0, #2
        str     r0, [r4, #4]
        bl      bc_mirrors
        mov     r0, #2
        

        @ copy DTB after the zImage
        ldr     r0, =DTB_SRC
        ldr     r1, =DTB_DEST
        ldr     r2, =DTB_SIZE
        bl      copy_words

        @ DTB magic check
        ldr     r1, =DTB_DEST
        ldr     r0, [r1]
        ldr     r2, =0xEDFE0DD0          @ DTB magic, little-endian word
        cmp     r0, r2
        bne     kernel_fail

        mov     r0, #3
        str     r0, [r4, #4]
        bl      bc_mirrors
        mov     r0, #3
        

        @ Epoch-test block: publish the configuration, then (delay builds
        @ only) hold kernel entry for EP_TICKS WDT2 ticks.  Lives at
        @ breadcrumb +0x88: +0x00..+0x7C belong to the kernel event log,
        @ +0x80/+0x84 to the ring header and +0x100.. to the characters.
        ldr     r4, =BC_PRIMARY
        ldr     r0, =0x48435045          @ "EPCH"
        str     r0, [r4, #0x88]
        ldr     r0, =EP_TICKS
        str     r0, [r4, #0xa0]
        mov     r0, #0
        str     r0, [r4, #0x8c]
        str     r0, [r4, #0x90]
        str     r0, [r4, #0x94]
        str     r0, [r4, #0x98]
        str     r0, [r4, #0x9c]
        str     r0, [r4, #0xa4]
        @ +0xa8 is written much earlier (CRR at chainloader entry): do not
        @ clear it here or the hand-over split is lost.
        mov     r0, #0x100               @ bit8: delay-capable chainloader
        str     r0, [r4, #0x8c]
        bl      ep_publish

        @ Always take one WDT2 sample here (CRR + LDR) so that even a D=0
        @ build measures the pre-kernel duration on the always-on clock,
        @ and so the read is proven safe before a delay build relies on it.
        ldr     r5, =WDT2_CRR
        ldr     r6, [r5]                 @ CRR (counts up from LDR)
        str     r6, [r4, #0x90]
        ldr     r7, [r5, #4]             @ LDR at 0x4A31402c
        str     r7, [r4, #0xa4]
        mov     r0, #0x108               @ bit3: WDT2 sampled
        str     r0, [r4, #0x8c]
        bl      ep_publish

        @ Absolute epoch: the 32k sync counter is NOT re-armed by the boot,
        @ so it is the only timebase comparable across boots.  Sampled last,
        @ because a gated read here would take the run with it - by now the
        @ block above is already in DRAM.
        ldr     r5, =SYNC32K
        ldr     r6, [r5]
        str     r6, [r4, #0xb0]
        ldr     r0, [r4, #0x8c]
        orr     r0, r0, #0x10            @ bit4: 32k sampled
        str     r0, [r4, #0x8c]
        bl      ep_publish
        .if EP_TICKS > 0
        bl      epoch_delay
        .endif

        @ ARM Linux boot protocol, jump to the zImage
        mov     r0, #0
        mvn     r1, #0
        ldr     r2, =DTB_DEST
        ldr     pc, =KERNEL_DEST

kernel_fail:
        mov     r0, #0xE1
        str     r0, [r4, #4]
        bl      bc_mirrors
        bl      led_off
kernel_park:
        b       kernel_park

@ ---------------------------------------------------------------------------
@ r0 = src, r1 = dst, r2 = bytes (rounded up to words)
copy_words:
        add     r2, r2, #3
        lsr     r2, r2, #2
1:      cmp     r2, #0
        beq     2f
        ldr     r3, [r0], #4
        str     r3, [r1], #4
        subs    r2, r2, #1
        bne     1b
2:      bx      lr

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
@ Epoch-test block layout, relative to each breadcrumb page (BC_PRIMARY and
@ the three mirrors):
@   +0x88  magic "EPCH" (0x48435045)
@   +0x8c  status: bit0 = WDT2 counter observed ticking
@                   bit1 = anomaly (no timebase, or overshoot)
@                   bit2 = delay loop completed
@                   bit3 = WDT2 sampled before kernel entry
@                   bit8 = this chainloader can delay
@   +0x90  CRR at loop entry (loop start); pre-jump sample when no delay
@   +0x94  CRR last read (updated every iteration)
@   +0x98  elapsed ticks = last - start  (32.768 kHz)
@   +0x9c  loop iterations
@   +0xa0  target ticks (EP_TICKS)
@   +0xa4  LDR as read just before kernel entry (arming value)
@ The values are published every iteration, so if the CPU dies mid-delay
@ the block records exactly when, in always-on timer ticks.
ep_publish:                              @ r4 = BC_PRIMARY
        push    {r0-r3}
        ldr     r1, =BC_MIR1
        ldr     r2, =BC_MIR2
        ldr     r3, =BC_MIR3
        mov     r12, #0x88
1:      ldr     r0, [r4, r12]
        str     r0, [r1, r12]
        str     r0, [r2, r12]
        str     r0, [r3, r12]
        add     r12, r12, #4
        cmp     r12, #0xb4
        blt     1b
        pop     {r0-r3}
        bx      lr

@ ---------------------------------------------------------------------------
@ Hold kernel entry for EP_TICKS ticks of the always-on WDT2 counter.
@ WDT2 counts down at 32.768 kHz and is armed by the RIM bootloader, so it is
@ the only timebase guaranteed to be live before any kernel exists.  A gated
@ or stopped module reads back a constant; that is detected (two reads must
@ differ) and the delay falls back to a fixed iteration count.
epoch_delay:
        push    {r4-r11, lr}
        ldr     r4, =BC_PRIMARY
        ldr     r5, =WDT2_CRR
        ldr     r8, =EP_TICKS

        ldr     r6, [r5]                 @ loop-start CRR
        str     r6, [r4, #0x90]
        str     r6, [r4, #0x94]

        mov     r7, #0x2000              @ short settle loop
1:      subs    r7, r7, #1
        bne     1b
        ldr     r0, [r5]
        cmp     r0, r6
        beq     2f                       @ constant -> no usable timebase
        ldr     r0, [r4, #0x8c]          @ keep bit3/bit8 (RMW, never store)
        orr     r0, r0, #0x1             @ ticking
        str     r0, [r4, #0x8c]
        ldr     r7, =0xffffffff          @ cap: none, ticks govern
        b       3f
2:      ldr     r0, [r4, #0x8c]
        orr     r0, r0, #0x2             @ no timebase (anomaly)
        str     r0, [r4, #0x8c]
        ldr     r7, =0x600000            @ fallback: fixed busy loop
3:
        mov     r9, #0                   @ iterations
4:      add     r9, r9, #1
        ldr     r10, [r5]                @ CRR now
        str     r10, [r4, #0x94]
        sub     r11, r10, r6             @ elapsed ticks (CRR counts UP from LDR)
        str     r11, [r4, #0x98]
        str     r9, [r4, #0x9c]
        bl      ep_publish
        cmp     r11, r8
        bhs     5f                       @ target reached
        cmp     r9, r7
        blo     4b                       @ neither target nor cap yet
        ldr     r0, [r4, #0x8c]
        orr     r0, r0, #0x2             @ cap hit
        str     r0, [r4, #0x8c]
        b       6f
5:      ldr     r0, [r4, #0x8c]
        orr     r0, r0, #0x4             @ completed on the timebase
        cmp     r11, r8, lsl #2          @ more than 4x the target?
        orrhi   r0, r0, #0x2             @ counter reloaded under us
        str     r0, [r4, #0x8c]
6:      bl      ep_publish
        pop     {r4-r11, lr}
        bx      lr

led_off:
        ldr     r0, =GPIO1
        ldr     r1, =EN_BIT
        str     r1, [r0, #GPIO_CLEAR]
        bx      lr

@ The LED blink stages (blink_n/delay/led_on) are gone: 12 uncached
@ 0x04000000-iteration loops cost 1.8-4.8 s and varied with the bootloader's
@ MPU clock, which also moved where in kernel time a capture died.  The stage
@ breadcrumbs (+0x04) and the epoch block carry the same information.
