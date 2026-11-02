/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap4bc.S — DEBUG_LL for the BlackBerry PlayBook (OMAP4430 "winchester").
 *
 * UART3 is wired internally but no pads are externally accessible, so plain
 * DEBUG_LL output would be invisible.  Every character is therefore written
 * to UART3 (0x48020000, as usual) AND mirrored into THREE ring buffers in
 * DRAM.  Per ring (base page): u32 monotonic count at +0x80, u32 wrapping
 * index at +0x84, 32512 chars at +0x100..+0x7FFF.  Rings (scatter-proven
 * survivor pages — kexec/scatter.c, SESSION-HANDOFF ROUND 5):
 *
 *	0x88000000 / 0x90000000 / 0x94000000
 *
 * The pages survive the WDT2 warm reset (and are no-map reserved in the DTS
 * so the kernel will not allocate over them).  They are read back from the
 * QNX side with memdump3 after the reboot.
 *
 * L2 (PL310) flush: with MMU on the ring VAs are cached-normal; DCCMVAC only
 * pushes L1->L2, and the ring lines are HOT (re-dirtied every char) so they
 * never evict — a warm reset then destroys dirty PL310 lines and the capture
 * is lost (observed).  Direct PL310 access works on this unit (ROUND 4
 * smcprobe experiments), so every put also issues a PL310 clean+invalidate
 * by physical address (0x48242000+0x768) to force the lines to DRAM.
 * MMU on:  PL310 reached through the static map at 0xFEB42000 (set up in
 *          debug_ll_io_init, arch/arm/mm/mmu.c) and ring PAs derived via
 *          LOWMEM_DELTA.
 * MMU off: writes already bypass L1 but still allocate in L2 — same PL310
 *          CI-by-PA sequence at its physical address, then dsb.
 *
 * Register constraints inside printascii/printch (arch/arm/kernel/debug.S):
 *   r0 = string ptr (dead in busyuart), r1 = char (dead after busyuart),
 *   r2 = \rd scratch (dead), r3 = \rx UART base (MUST survive busyuart).
 * We push {r1,r2,r3,r4} for the capture (r4 callee-saved for the C caller);
 * the bounded LSR drain afterwards uses r1 (free) as countdown.
 */

#include <linux/serial_reg.h>

#define UART3_BASE	0x48020000

/*
 * Non-cacheable ring aliases (see head.S and omap44xx_io_desc):
 *	0xFE800000 -> PA 0x88000000
 *	0xFE900000 -> PA 0x90000000
 *	0xFEA00000 -> PA 0x94000000
 * With MMU on the debug code uses these; with MMU off it uses the PAs.
 * Non-cacheable means every character is in DRAM immediately, so there is
 * no cache maintenance here at all.  (The old cacheable alias + PL310
 * clean-by-PA broke once the L2C driver took secure ownership of the
 * PL310: non-secure maintenance then aborted inside printk.)
 */

		.macro	addruart, rp, rv, tmp
		ldr	\rp, =UART3_BASE
		ldr	\rv, =0xFED20000	@ static map by debug_ll_io_init()
						@ (section VA 0xFED00000 -> PA
						@  0x48000000, also early-mapped
						@  from __create_page_tables via
						@  this same macro)
		.endm

.macro	senduart, rd, rx
		@ PlayBook 2026-09-02: UART3 write REMOVED. When L4PER auto-idles,
		@ a posted store to the dead THR never completes and the next ring
		@ load stalls on the store buffer (the 132-console-wall). The three
		@ DRAM rings carry the console text instead.
		.endm

		.macro	waituarttxrdy, rd, rx
		.endm

		.macro	waituartcts, rd, rx
		.endm

		@ ring_put <count-addr-reg> <char-reg> <scratch-reg>
		@   count addr in \base on entry (and restored on exit);
		@   \sc left holding the char slot index (0..0x7EFF).
		.macro	ring_put, base, ch, sc
		ldr	\sc, [\base]		@ monotonic count
		add	\sc, \sc, #1
		str	\sc, [\base]
		ldr	\sc, [\base, #4]	@ wrapping index
		add	\sc, \sc, #1
		cmp	\sc, #0x7F00		@ 32512-char ring (32 KB page)
		subcs	\sc, \sc, #0x7F00
		str	\sc, [\base, #4]
		add	\sc, \sc, #0x80		@ char addr = count+0x80+idx
		strb	\ch, [\base, \sc]	@   (= ring base + 0x100 + idx)
		sub	\sc, \sc, #0x80
		.endm

		@ busyuart: mirror char into the 3 breadcrumb rings.  The rings
		@ are mapped non-cacheable, so the stores are already DRAM truth;
		@ just drain the write buffer every 8 chars.  A dead/gated UART
		@ must not hang the boot, so nothing is read back from it.
		.macro	busyuart, rd, rx
		push	{r1, r2, r3, r4, r5}
		mov	r2, r1			@ char
		mrc	p15, 0, r1, c1, c0, 0	@ SCTLR
		tst	r1, #1			@ MMU on?
		mov	r4, #0			@ remember MMU state in r4: the
		movne	r4, #1			@ ring_put cmps clobber the flags
		@ ring 3: 0x94000000 (PA) / 0xFEA00000 (VA)
		cmp	r4, #1
		ldreq	r1, =0xFEA00080
		ldrne	r1, =0x94000080
		ring_put r1, r2, r3
		@ ring 2: 0x90000000 / 0xFE900000
		cmp	r4, #1
		ldreq	r1, =0xFE900080
		ldrne	r1, =0x90000080
		ring_put r1, r2, r3
		@ ring 1: 0x88000000 / 0xFE800000
		cmp	r4, #1
		ldreq	r1, =0xFE800080
		ldrne	r1, =0x88000080
		ring_put r1, r2, r3
		ldr	r5, [r1]		@ ring1 monotonic count
		tst	r5, #7			@ drain the write buffer every 8th
		bne	95f
		dsb
95:
		b	98f

98:
		@ PlayBook 2026-09-02: LSR drain removed too - a read from a dead
		@ UART stalls forever (non-posted). Rings only.
		pop	{r1, r2, r3, r4, r5}
		.endm