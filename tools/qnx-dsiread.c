/*
 * pbdsi — read TC358764 registers over DSI from a QNX/TabletOS user process.
 *
 * Why: the bridge's status registers (LANESTATUS0/1, INTSTATUS, DSIERRCNT,
 * SYSSTAT) have no public register map, and on Linux they are the ones that
 * say "no lane in HS RX" while the panel stays black.  TabletOS drives the
 * same panel correctly on the same unit, so reading those registers *while
 * QNX's display works* gives the missing reference values.  This is the
 * on-device oracle route that closed the I2C question (tools/qnx-i2c.c).
 *
 * Read-only: it never writes a bridge register.  The only MMIO writes are the
 * OMAP DSI command-VC packet header and the BTA enable bit, which is exactly
 * what the Linux console's pb_reg read does (dsi.c: pb_bridge_read()).
 *
 * Register map (OMAP4430 DSI protocol engine, 0x58004000 / 0x48044000):
 *   DSI_CTRL                 0x040
 *   DSI_IRQSTATUS            0x018   bit 7 = PLL_LOCK
 *   VC_CTRL(n)               0x100 + n*0x20   bit 0 VC_EN, 5 TX-empty,
 *                                            6 BTA_EN, 9 MODE_SPEED,
 *                                            16 FIFO-full, 20 RX-not-empty
 *   VC_SHORT_PACKET_HEADER(n) 0x110 + n*0x20  (also pops the RX FIFO)
 *   VC_IRQSTATUS(n)          0x118 + n*0x20   bit 2 PACKET_SENT, 5 BTA
 *
 * The packet is the MIPI generic read request, 2 parameters (DT 0x24), on
 * hardware VC 1 (the command channel QNX leaves in LP), packet VC 0 — the
 * same transport QNX uses for its bridge writes (winch_lcdctl 0x13ce0).
 * ECC byte 0 is what mainline writes too (drm_mipi_dsi.c: "TODO: compute
 * ECC"), and the bridge accepts it.
 *
 * built with the QNX 6.6 toolchain (docs/project/SETUP.md §2):
 *   $Q/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall \
 *       -o pbdsi qnx-dsiread.c
 *
 * usage:
 *   pbdsi                read the diagnostic register set, 4 samples each
 *   pbdsi 214 218 500    read the given 16-bit registers
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#define DSI_PHYS	0x58004000u
#define DSI_SIZE	0x1000u

#define DSI_CTRL		0x040
#define DSI_IRQSTATUS		0x018
#define DSI_VC_CTRL(vc)		(0x100 + (vc) * 0x20)
#define DSI_VC_SHORT_HDR(vc)	(0x110 + (vc) * 0x20)
#define DSI_VC_IRQSTATUS(vc)	(0x118 + (vc) * 0x20)

#define VC_EN			(1u << 0)
#define VC_TX_FIFO_EMPTY	(1u << 5)
#define VC_BTA_EN		(1u << 6)
#define VC_MODE_SPEED_HS	(1u << 9)
#define VC_FIFO_FULL		(1u << 16)
#define VC_RX_NOT_EMPTY		(1u << 20)

#define VC_IRQ_PACKET_SENT	(1u << 2)
#define VC_IRQ_BTA		(1u << 5)

#define DSI_DT_GENERIC_READ_REQ_2	0x24
#define DSI_DT_ACK_AND_ERR		0x02
#define DSI_DT_GENERIC_LONG_RESP	0x1a
#define DSI_DT_GENERIC_SHORT_RESP_1	0x11
#define DSI_DT_GENERIC_SHORT_RESP_2	0x12

static volatile uint32_t *dsi;

static uint32_t rd(unsigned off)
{
	uint32_t v = dsi[off / 4];

	__sync_synchronize();
	return v;
}

static void wr(unsigned off, uint32_t v)
{
	dsi[off / 4] = v;
	__sync_synchronize();
}

/*
 * Drain anything left in the RX FIFO.  Reading the short-packet-header
 * register pops one word.  A previous failed attempt can leave a packet
 * behind, exactly like the "rx fifo not empty when sending BTA" case in the
 * Linux driver.
 */
static void rx_drain(int vc, const char *why)
{
	int i;

	if (!(rd(DSI_VC_CTRL(vc)) & VC_RX_NOT_EMPTY))
		return;

	printf("  (draining stale RX FIFO %s:", why);
	for (i = 0; i < 16; i++) {
		printf(" %08x", rd(DSI_VC_SHORT_HDR(vc)));
		if (!(rd(DSI_VC_CTRL(vc)) & VC_RX_NOT_EMPTY))
			break;
	}
	printf(")\n");
}

/*
 * One generic read of a 16-bit bridge register.  Returns 0 and fills *val on
 * success, negative on failure.  Mirrors dsi_vc_generic_read() in the Linux
 * kernel: send the short packet, enable BTA, wait for the BTA IRQ, pop the
 * response out of the RX FIFO.
 */
static int bridge_read_ex(int vc, unsigned addr, uint32_t *val, int verbose)
{
	uint32_t c, word;
	uint32_t fifo[8];
	int i, nfifo = 0;

	rx_drain(vc, "before read");

	c = rd(DSI_VC_CTRL(vc));
	if (!(c & VC_EN)) {
		printf("  (VC%d disabled 0x%08x -> enabling)\n", vc, c);
		wr(DSI_VC_CTRL(vc), c | VC_EN);
		c = rd(DSI_VC_CTRL(vc));
	}
	if (c & VC_MODE_SPEED_HS)
		printf("  (warning: VC%d is in HS, expected LP: 0x%08x)\n", vc, c);

	/*
	 * Wait only for the TX FIFO not to be full.  Do NOT wait for
	 * VC_TX_FIFO_EMPTY (bit 5): QNX leaves it clear on the command VC, so
	 * waiting for it burns the whole loop on every read (measured: ~1 s
	 * per read, i.e. minutes for a register dump).
	 */
	for (i = 0; i < 100; i++) {
		c = rd(DSI_VC_CTRL(vc));
		if (!(c & VC_FIFO_FULL))
			break;
		usleep(10);
	}

	/* generic read request, 2 parameters; ECC byte 0 as mainline does */
	word = ((addr >> 8) & 0xff) << 16 | (addr & 0xff) << 8 |
	       DSI_DT_GENERIC_READ_REQ_2;
	wr(DSI_VC_SHORT_HDR(vc), word);

	/* the PE only performs the turnaround when BTA_EN is set */
	wr(DSI_VC_CTRL(vc), rd(DSI_VC_CTRL(vc)) | VC_BTA_EN);
	rd(DSI_VC_CTRL(vc));			/* flush posted write */

	for (i = 0; i < 2000; i++) {
		if (rd(DSI_VC_IRQSTATUS(vc)) & VC_IRQ_BTA)
			break;
		usleep(10);
	}
	if (i == 2000) {
		printf("  BTA timeout (irqstatus=0x%08x vc_ctrl=0x%08x)\n",
		       rd(DSI_VC_IRQSTATUS(vc)), rd(DSI_VC_CTRL(vc)));
		wr(DSI_VC_IRQSTATUS(vc), 0xffffffff);
		return -1;
	}
	wr(DSI_VC_IRQSTATUS(vc), VC_IRQ_BTA);

	/* collect everything the FIFO holds for this turnaround */
	while (nfifo < 8 && (rd(DSI_VC_CTRL(vc)) & VC_RX_NOT_EMPTY))
		fifo[nfifo++] = rd(DSI_VC_SHORT_HDR(vc));

	if (verbose) {
		printf("  fifo[%d]:", nfifo);
		for (i = 0; i < nfifo; i++)
			printf(" %08x", fifo[i]);
		printf("\n");
	}

	if (nfifo == 0) {
		printf("  BTA seen but RX FIFO empty\n");
		return -1;
	}

	/* an ACK/error report can precede the actual response */
	i = 0;
	while (i < nfifo && (fifo[i] & 0x3f) == DSI_DT_ACK_AND_ERR) {
		printf("  peripheral ACK with error report 0x%04x\n",
		       (fifo[i] >> 8) & 0xffff);
		i++;
	}
	if (i >= nfifo)
		return -1;

	word = fifo[i++];
	switch (word & 0x3f) {
	case DSI_DT_GENERIC_SHORT_RESP_1:
		*val = (word >> 8) & 0xff;
		return 0;
	case DSI_DT_GENERIC_SHORT_RESP_2:
		*val = (word >> 8) & 0xffff;
		return 0;
	case DSI_DT_GENERIC_LONG_RESP: {
		/*
		 * 32-bit register: a 4-byte long response, little-endian,
		 * followed by a 2-byte checksum that we discard.
		 */
		unsigned len = (word >> 8) & 0xffff;
		unsigned char buf[16];
		unsigned got = 0, b;

		if (len > 4)
			len = 4;		/* we only ever ask for 4 */
		while (got < len + 2 && i < nfifo) {
			uint32_t w = fifo[i++];

			for (b = 0; b < 4 && got < len + 2; b++, got++) {
				if (got < 4)
					buf[got] = (w >> (b * 8)) & 0xff;
			}
		}
		*val = buf[0] | buf[1] << 8 | buf[2] << 16 |
		       (uint32_t)buf[3] << 24;
		return 0;
	}
	default:
		printf("  unexpected response DT 0x%02x (word 0x%08x)\n",
		       word & 0x3f, word);
		return -1;
	}
}

static const unsigned diag_regs[] = {
	0x0580,	/* IDREG           (expect 0x00006500) */
	0x0214,	/* DSI_LANESTATUS0 (Linux reads 0) */
	0x0218,	/* DSI_LANESTATUS1 (Linux reads 0x1d) */
	0x0220,	/* DSI_INTSTATUS   (Linux reads 0x10400880) */
	0x0300,	/* DSIERRCNT       (Linux reads 0xc0800000) */
	0x0500,	/* SYSSTAT         (Linux reads 0) */
	0x049c,	/* LV_CFG          (Linux reads 1) */
	0x0450,	/* VP_CTRL         (Linux reads 0x03f00120) */
	0x0464,	/* VP_VFUEN        (Linux reads 1) */
	0x0110,	/* PPI_LINEINITCNT (Linux reads 0x208e) */
	0x0134,	/* PPI_LANEENABLE  (Linux reads 0x1f) */
	0x0210,	/* DSI_LANEENABLE  (Linux reads 0x1f) */
	0x013c,	/* PPI_TX_RX_TA    (reference writes 4) */
	0x0230,	/* read as 0xffffffff on Linux (unimplemented?) */
};

int main(int argc, char **argv)
{
	const int vc = 1;
	unsigned regs[64];
	int nregs = 0, i, s, verbose = 0;
	int argi = 1;

	dsi = mmap_device_memory(NULL, DSI_SIZE, PROT_READ | PROT_WRITE |
				 PROT_NOCACHE, 0, (uint64_t)DSI_PHYS);
	if (dsi == MAP_FAILED) {
		fprintf(stderr, "mmap_device_memory(0x%08x): %s\n",
			DSI_PHYS, strerror(errno));
		return 1;
	}

	if (argi < argc && !strcmp(argv[argi], "-v")) {
		verbose = 1;
		argi++;
	}

	printf("DSI PE at 0x%08x: CTRL=0x%08x IRQSTATUS=0x%08x "
	       "VC0_CTRL=0x%08x VC1_CTRL=0x%08x\n",
	       DSI_PHYS, rd(DSI_CTRL), rd(DSI_IRQSTATUS),
	       rd(DSI_VC_CTRL(0)), rd(DSI_VC_CTRL(vc)));

	/*
	 * dump <from> <to>: every 32-bit register in the range, one line each,
	 * for diffing a working TabletOS against our kernel.  Retries hide the
	 * intermittent peripheral ACK/error report.
	 */
	if (argi + 1 < argc && !strcmp(argv[argi], "dump")) {
		unsigned from = strtoul(argv[argi + 1], NULL, 16);
		unsigned to = strtoul(argv[argi + 2], NULL, 16);
		unsigned r;

		for (r = from; r <= to; r += 4) {
			uint32_t v = 0;
			int rc = -1, t;

			for (t = 0; t < 3 && rc; t++)
				rc = bridge_read_ex(vc, r, &v, verbose);
			if (rc == 0)
				printf("%04x = %08x\n", r, v);
			else
				printf("%04x = --------\n", r);
		}
		printf("end: VC1_CTRL=0x%08x VC1_IRQSTATUS=0x%08x\n",
		       rd(DSI_VC_CTRL(vc)), rd(DSI_VC_IRQSTATUS(vc)));
		munmap_device_memory((void *)dsi, DSI_SIZE);
		return 0;
	}

	if (argi < argc) {
		for (i = argi; i < argc && nregs < 64; i++)
			regs[nregs++] = strtoul(argv[i], NULL, 16);
	} else {
		for (i = 0; i < (int)(sizeof(diag_regs) / sizeof(diag_regs[0])); i++)
			regs[nregs++] = diag_regs[i];
	}

	for (i = 0; i < nregs; i++) {
		for (s = 0; s < 4; s++) {
			uint32_t v = 0;
			int rc = bridge_read_ex(vc, regs[i], &v, verbose);

			if (rc == 0)
				printf("%04x = %08x%s\n", regs[i], v,
				       s ? "  (repeat)" : "");
			else
				printf("%04x   read failed%s\n", regs[i],
				       s ? "  (repeat)" : "");
			usleep(2000);
		}
	}

	printf("end: VC1_CTRL=0x%08x VC1_IRQSTATUS=0x%08x\n",
	       rd(DSI_VC_CTRL(vc)), rd(DSI_VC_IRQSTATUS(vc)));

	munmap_device_memory((void *)dsi, DSI_SIZE);
	return 0;
}
