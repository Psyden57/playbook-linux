/*
 * PlayBook OMAP4430 bring-up — interactive bench console on /dev/ttyGS0.
 *
 * The USB gadget is a configfs CDC-ACM function (report 114), so the Linux
 * side of it is /dev/ttyGS0 and the PC side is /dev/ttyACM0.  This is a
 * purpose-built static binary: there is no busybox, no shell and no library
 * beyond libc in the initramfs, and none is wanted.
 *
 * Design notes that matter on this hardware:
 *
 *  - Every physical-memory access runs in a *forked worker* with a 3 s alarm.
 *    A read of a clock-gated OMAP target can abort the L4 transaction and the
 *    kernel then delivers SIGBUS to whoever touched the mapping.  In a worker
 *    that costs one child and the console keeps running; in-process it would
 *    kill the console (report 112 §2: the DSI PHY page did exactly this).
 *  - The DRAM console ring (report 104/109) is written with the same
 *    count/index protocol the kernel and /init use (rlog), so a host that
 *    never connects still leaves a transcript in the capture.
 *  - `reboot` asks PID 1 to do it (a byte on an inherited pipe) and only
 *    falls back to reboot(2) itself, because PID 1 is the only process that
 *    is *supposed* to call it here.
 *  - The WDT2 pet: the kernel already kicks TGR from a 2 s timer_list
 *    (report 109 §8.1) and there is no userspace knob for that.  `kick on`
 *    therefore adds a *userspace* pet of the same TGR (read/invert/write) and
 *    prints CRR/LDR so the always-on watchdog can be watched from the bench;
 *    `kick off` stops the userspace pet and says plainly that the kernel's
 *    own pet cannot be disabled from here.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <linux/random.h>

#include <linux/loop.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/spi/spidev.h>
#include <linux/fb.h>

#define RING_PA		0x88000000UL
#define RING_SIZE	0x7f00

#define BOOT_LIFETIME_S	180		/* must match init.c */
#define CAT_MAX		4096
#define PEEK_MAX	64
#define KICK_MS		2000

enum { M_PEEK = 1, M_POKE, M_KICK };

static int cfd = -1;			/* /dev/ttyGS0 */
static int rb_fd = -1;			/* pipe to PID 1: any byte = reboot */
static volatile unsigned char *ring;
static int kick_on;
static int swallow_nl;
static long long last_kick_ms;

/* ------------------------------------------------------------------ output */

static void ow(const char *s)
{
	if (cfd >= 0)
		write(cfd, s, strlen(s));
}

/* newline = CRLF: the device side is raw (no ONLCR) and the host terminal
 * needs the CR. */
static void line(const char *fmt, ...)
{
	char b[320];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	if (n <= 0)
		return;
	b[n++] = '\r';
	b[n++] = '\n';
	write(cfd, b, n);
}

static long long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ------------------------------------------------- userspace DRAM-ring log */

static void rlog(const char *s)
{
	unsigned int count, idx;

	if (!ring)
		return;
	count = *(volatile unsigned int *)(ring + 0x80);
	idx = *(volatile unsigned int *)(ring + 0x84);
	if (idx >= RING_SIZE)
		idx = 0;
	for (; *s; s++) {
		ring[0x100 + idx] = *s;
		idx = (idx + 1) % RING_SIZE;
		count++;
	}
	ring[0x100 + idx] = '\n';
	count++;
	idx = (idx + 1) % RING_SIZE;
	*(volatile unsigned int *)(ring + 0x80) = count;
	*(volatile unsigned int *)(ring + 0x84) = idx;
}

/* ------------------------------------------------ /dev/mem worker (forked) */

/*
 * Runs in the child.  Writes plain "\n"-terminated text into fd; the parent
 * converts to CRLF.  Anything that aborts the bus aborts *this* process.
 */
static void worker(unsigned long pa, long count, int mode, unsigned int val,
		   int fd)
{
	char b[160];
	int n, fdmem, i;

	fdmem = open("/dev/mem", O_RDWR | O_SYNC);
	if (fdmem < 0) {
		n = snprintf(b, sizeof(b), "worker: open /dev/mem: %s\n",
			     strerror(errno));
		write(fd, b, n);
		_exit(2);
	}
	alarm(3);

	if (mode == M_KICK) {
		void *m = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
			       fdmem, 0x4a314000UL);
		volatile unsigned int *r = m;
		unsigned int tgr, crr0, crr1, ldr;

		if (m == MAP_FAILED) {
			n = snprintf(b, sizeof(b), "kick: map failed\n");
			write(fd, b, n);
			_exit(3);
		}
		/* The kick is TGR = ~TGR (what omap4430-wdtkick and the kernel
		 * both do).  TGR's readback ping-pongs, so the proof that the
		 * kick landed is CRR going *backwards*: it counts up from LDR
		 * and a trigger reloads it.  */
		crr0 = r[0x28 / 4];
		tgr = r[0x30 / 4];
		r[0x30 / 4] = ~tgr;
		crr1 = r[0x28 / 4];
		ldr = r[0x2c / 4];
		n = snprintf(b, sizeof(b),
			     "WDT2: TGR %08x -> wrote %08x | CRR %08x -> %08x "
			     "(LDR %08x: %s)\n",
			     tgr, ~tgr, crr0, crr1, ldr,
			     crr1 < crr0 ? "RELOADED" : "no reload?");
		write(fd, b, n);
		_exit(0);
	}

	for (i = 0; i < count; i++) {
		unsigned long a = pa + 4 * i;
		void *m;
		volatile unsigned int *p;

		m = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
			 fdmem, a & ~0xfffUL);
		if (m == MAP_FAILED) {
			n = snprintf(b, sizeof(b), "%08lx: map failed (%s)\n", a,
				     strerror(errno));
			write(fd, b, n);
			_exit(3);
		}
		p = (volatile unsigned int *)((char *)m + (a & 0xfff));
		if (mode == M_POKE) {
			unsigned int rb;

			*p = val;
			rb = *p;
			n = snprintf(b, sizeof(b),
				     "%08lx: wrote %08x, readback %08x%s\n", a,
				     val, rb, rb == val ? "" : "  (DIFFERS)");
		} else {
			n = snprintf(b, sizeof(b), "%08lx: %08x\n", a, *p);
		}
		write(fd, b, n);
		munmap(m, 0x1000);
	}
	_exit(0);
}

static void drain_and_report(int fd, pid_t pid)
{
	char buf[1024], out[1100];
	ssize_t n;
	int st = 0;

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		ssize_t i, o = 0;

		for (i = 0; i < n; i++) {		/* LF -> CRLF */
			if (buf[i] == '\n') {
				out[o++] = '\r';
			}
			out[o++] = buf[i];
			if (o >= (ssize_t)sizeof(out) - 3) {
				write(cfd, out, o);
				o = 0;
			}
		}
		if (o)
			write(cfd, out, o);
	}
	waitpid(pid, &st, 0);
	if (WIFSIGNALED(st)) {
		int sig = WTERMSIG(st);

		if (sig == SIGBUS)
			line("!! worker killed by SIGBUS: the access was aborted "
			     "(gated block / L4 firewall).  Try a different "
			     "address.");
		else if (sig == SIGALRM)
			line("!! worker timed out after 3 s (hung bus cycle)");
		else
			line("!! worker killed by signal %d", sig);
	} else if (WEXITSTATUS(st) != 0) {
		line("!! worker exit %d", WEXITSTATUS(st));
	}
}

static int mem_op(unsigned long pa, long count, int mode, unsigned int val)
{
	int pfd[2];
	pid_t pid;

	if (pipe(pfd))
		return -1;
	pid = fork();
	if (pid == 0) {
		close(pfd[0]);
		worker(pa, count, mode, val, pfd[1]);
		_exit(0);
	}
	if (pid < 0) {
		close(pfd[0]);
		close(pfd[1]);
		return -1;
	}
	close(pfd[1]);
	drain_and_report(pfd[0], pid);
	close(pfd[0]);
	return 0;
}

/* ---------------------------------------- display bring-up (report 118) */

static int hexval(const char *s, unsigned long *out);

/*
 * Replays the reference OS's display programming, read back register by
 * register in report 118.  Everything is a /dev/mem poke; no kernel display
 * driver is involved.  It runs in a forked child so a bus abort on a gated
 * target costs the child, not the console.
 *
 * The order is not cosmetic.  PM_DSS_PWRSTCTRL.POWERSTATE must be ON before
 * CM_DSS_DSS_CLKCTRL.MODULEMODE can leave idle: with the domain off the
 * MODULEMODE write latches, IDLEST stays 3 ("disabled and cannot be
 * accessed"), and every DISPC/DSI access aborts.
 */

#define PM_DSS_PWRSTCTRL	0x4a307100UL
#define PM_DSS_PWRSTST		0x4a307104UL
#define CM_DSS_CLKSTCTRL	0x4a009100UL
#define CM_DSS_DSS_CLKCTRL	0x4a009120UL
#define DSS_CONTROL		0x48040040UL
#define DISPC_BASE		0x48041000UL
#define DSI_PE_BASE		0x48044000UL
#define DSI_PHY_BASE		0x48044200UL
#define DSI_PLL_BASE		0x48044300UL

static volatile unsigned int *mapreg(int mem, unsigned long pa)
{
	void *m = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
		       mem, pa & ~0xfffUL);

	if (m == MAP_FAILED)
		return NULL;
	return (volatile unsigned int *)((char *)m + (pa & 0xfff));
}

static int poll_bits(volatile unsigned int *r, unsigned int mask,
		     unsigned int want, int ms)
{
	int i;

	for (i = 0; i < ms; i++) {
		if ((*r & mask) == want)
			return 0;
		usleep(1000);
	}
	return -1;
}

static int dsi_pll_lock(int mem)
{
	volatile unsigned int *clk = mapreg(mem, DSI_PE_BASE + 0x54);
	volatile unsigned int *st = mapreg(mem, DSI_PLL_BASE + 0x04);
	volatile unsigned int *go = mapreg(mem, DSI_PLL_BASE + 0x08);
	volatile unsigned int *c1 = mapreg(mem, DSI_PLL_BASE + 0x0c);
	volatile unsigned int *c2 = mapreg(mem, DSI_PLL_BASE + 0x10);
	int i;

	if (!clk || !st || !go || !c1 || !c2)
		return -1;

	/* PLL power ON_ALL (CMD bits[31:30], STATUS bits[29:28]) */
	*clk = 0xa0346005;
	if (poll_bits(clk, 0x30000000, 0x20000000, 100) < 0)
		line("DSI: PLL power ON_ALL did not acknowledge (clk=%08x)",
		     *clk);

	*c1 = 0x14a5e8be;		/* REGM 756, REGN 95, M4=M5=6 */
	*c2 = 0x00056000;		/* REFEN, PHY_CLKINEN, M4/M5 enable */
	*go = 1;
	for (i = 0; i < 200; i++) {
		if (*st & 0x2)		/* PLL_LOCK */
			return 0;
		usleep(1000);
	}
	return -1;
}

static void disp_child(const char *what)
{
	static const unsigned int dsi_pe[][2] = {
		{ 0x40, 0x00eaef9d },	/* DSI_CTRL: video mode */
		{ 0x48, 0x2a054321 },	/* COMPLEXIO_CFG1: 2 data + clk lane */
		{ 0x58, 0x79190999 },	/* TIMING1 */
		{ 0x5c, 0x7fff7fff },	/* TIMING2 */
		{ 0x60, 0x01079074 },	/* VM_TIMING1: HSA 1, HFP 121, HBP 116 */
		{ 0x64, 0x04010c0c },	/* VM_TIMING2: VSA 1, VFP 12, VBP 12 */
		{ 0x68, 0x03f00258 },	/* VM_TIMING3: TL 1008, VACT 600 */
		{ 0x6c, 0x00000e0b },	/* CLK_TIMING */
		{ 0x70, 0x00002000 },	/* TX_FIFO_VC_SIZE */
		{ 0x74, 0x00002000 },	/* RX_FIFO_VC_SIZE */
		{ 0x78, 0x00030000 },	/* GNQ */
		{ 0x80, 0x00000000 },	/* VM_TIMING4 */
		{ 0x84, 0x00003f00 },	/* VM_TIMING5 */
		{ 0x88, 0x00000000 },	/* VM_TIMING6 */
		{ 0x8c, 0x000000c8 },	/* VM_TIMING7 */
		{ 0x90, 0x000b0009 },
		{ 0x94, 0x00000080 },	/* STOPCLK_TIMING */
		{ 0x100, 0x20808791 },	/* VC_CTRL(0) */
	};
	static const unsigned int phy[][2] = {
		{ 0x00, 0x0b190d13 }, { 0x04, 0x42040a22 },
		{ 0x08, 0xb8800009 }, { 0x0c, 0x625d21a0 },
		{ 0x10, 0x625d21a0 }, { 0x14, 0xff000000 },
		{ 0x18, 0x00010000 }, { 0x20, 0x00000410 },
		{ 0x24, 0x00020001 }, { 0x2c, 0x7d20a59a },
		{ 0x30, 0x04320000 }, { 0x38, 0x3e080000 },
		{ 0x3c, 0x01400003 },
	};
	volatile unsigned int *pm, *pmst, *cmst, *cm, *dss, *d;
	int mem, i;

	mem = open("/dev/mem", O_RDWR | O_SYNC);
	if (mem < 0) {
		line("disp: open /dev/mem: %s", strerror(errno));
		return;
	}

	pm = mapreg(mem, PM_DSS_PWRSTCTRL);
	pmst = mapreg(mem, PM_DSS_PWRSTST);
	cmst = mapreg(mem, CM_DSS_CLKSTCTRL);
	cm = mapreg(mem, CM_DSS_DSS_CLKCTRL);
	dss = mapreg(mem, DSS_CONTROL);
	d = mapreg(mem, DISPC_BASE);
	if (!pm || !pmst || !cmst || !cm || !dss || !d) {
		line("disp: register map failed");
		close(mem);
		return;
	}

	if (!strcmp(what, "off")) {
		*d = 0;					/* DISPC_CONTROL */
		line("disp: DISPC disabled (CONTROL=%08x)", *d);
		close(mem);
		return;
	}

	if (!strcmp(what, "stat")) {
		line("PM_DSS_PWRSTCTRL %08x  PWRSTST %08x", *pm, *pmst);
		line("CM_DSS_CLKSTCTRL %08x  DSS_CLKCTRL %08x", *cmst, *cm);
		line("DSS_CONTROL       %08x", *dss);
		line("DISPC CONTROL %08x CONFIG %08x LINE %08x IRQ %08x",
		     d[0x40 / 4], d[0x44 / 4], d[0x5c / 4], d[0x18 / 4]);
		line("DISPC TIM_H %08x TIM_V %08x POL %08x DIV %08x",
		     d[0x64 / 4], d[0x68 / 4], d[0x6c / 4], d[0x70 / 4]);
		line("DISPC SIZE_LCD %08x DEFAULT_COLOR %08x",
		     d[0x7c / 4], d[0x4c / 4]);
		{
			volatile unsigned int *p = mapreg(mem, DSI_PE_BASE);
			volatile unsigned int *ph = mapreg(mem, DSI_PHY_BASE);
			volatile unsigned int *pl = mapreg(mem, DSI_PLL_BASE);

			if (p)
				line("DSI SYSSTATUS %08x CTRL %08x CLK_CTRL %08x "
				     "VM1 %08x VM2 %08x VM3 %08x",
				     p[0x14 / 4], p[0x40 / 4], p[0x54 / 4],
				     p[0x60 / 4], p[0x64 / 4], p[0x68 / 4]);
			if (pl)
				line("DSI PLL STATUS %08x CFG1 %08x CFG2 %08x",
				     pl[0x04 / 4], pl[0x0c / 4], pl[0x10 / 4]);
			if (ph)
				line("DSI PHY CFG0 %08x CFG1 %08x CFG2 %08x",
				     ph[0x00 / 4], ph[0x04 / 4], ph[0x08 / 4]);
		}
		close(mem);
		return;
	}

	if (!strcmp(what, "mux")) {
		/*
		 * Only this subcommand touches the DSS top target, because that
		 * is the one access that can hang the interconnect.  DISPC and
		 * DSI are separate targets below and are unaffected.
		 */
		if (*cm & (1u << 18)) {
			line("disp: module in standby (CLKCTRL=%08x), cycling "
			     "MODULEMODE 0 -> 2", *cm);
			*cm = *cm & ~0x3u;
			poll_bits(cm, 0x30000, 0x30000, 200);
			*cm = (*cm & ~0xf03u) | 0xf02u;
			poll_bits(cm, 0x30000, 0x0, 500);
			line("disp: after cycle CLKCTRL=%08x%s", *cm,
			     (*cm & (1u << 18)) ? "  STILL IN STANDBY" : "");
		}
		if (*cm & (1u << 18)) {
			line("disp: STILL in standby -- refusing to write "
			     "DSS_CONTROL (a write there hangs the bus)");
			close(mem);
			return;
		}
		*dss = 0x3;		/* DISPC_CLK_SWITCH | DSI1_CLK_SWITCH */
		line("disp: DSS_CONTROL <- %08x (DISPC+DSI1 clock muxes), "
		     "readback %08x", 0x3, *dss);
		close(mem);
		return;
	}

	/* ---- 1. power domain, clock domain, module (report 118 §1) ---- */
	*pm = (*pm & ~0x3u) | 0x3u;			/* POWERSTATE = ON */
	if (poll_bits(pmst, 0x3, 0x3, 200) < 0)
		line("disp: PM_DSS did not reach ON (PWRSTST=%08x)", *pmst);
	*cmst = (*cmst & ~0x3u) | 0x2u;			/* CLKTRCTRL = SW_WAKEUP */
	*cm = (*cm & ~0xf03u) | 0xf02u;			/* MODULEMODE=2 + opts */
	if (poll_bits(cm, 0x30000, 0x0, 500) < 0)
		line("disp: DSS module stuck idle (CLKCTRL=%08x) -- "
		     "everything below will abort", *cm);
	line("disp: PM_DSS %08x  CM %08x/%08x  (CLKCTRL bit18 = standby %d)",
	     *pmst, *cmst, *cm, !!(*cm & (1u << 18)));

	/* ---- 2. DISPC timings (report 118 §2) ---- */
	d[0x40 / 4] = 0;				/* CONTROL: disable */
	usleep(20000);
	d[0x44 / 4] = 0x00000004;			/* CONFIG */
	d[0x64 / 4] = 0x0890a113;			/* HBP 137, HFP 161, HSW 19 */
	d[0x68 / 4] = 0x00c00c00;			/* VBP 12, VFP 12, VSW 0 */
	d[0x6c / 4] = 0x00030000;			/* POL_FREQ: ONOFF, RF */
	d[0x70 / 4] = 0x00010002;			/* DIVISOR: PCD 1, LCD 2 */
	d[0x7c / 4] = 0x025703ff;			/* SIZE_LCD: 1024x600 */
	if (d[0x4c / 4] == 0)				/* keep any colour */
		d[0x4c / 4] = 0x001f1f1f;		/* DEFAULT_COLOR (grey) */
	line("disp: DISPC programmed, SYSSTATUS=%08x SIZE=%08x",
	     d[0x14 / 4], d[0x7c / 4]);

	/* ---- 3. DSI: lanes, PLL, PHY, video timing ---- */
	{
		volatile unsigned int *p = mapreg(mem, DSI_PE_BASE);
		volatile unsigned int *ph = mapreg(mem, DSI_PHY_BASE);

		if (!p || !ph) {
			line("disp: DSI map failed");
			close(mem);
			return;
		}
		p[0x10 / 4] = 0x00000010;		/* SYSCONFIG */
		if (poll_bits(&p[0x14 / 4], 0x1, 0x1, 200) < 0)
			line("disp: DSI RESET_DONE not set (%08x)",
			     p[0x14 / 4]);

		if (dsi_pll_lock(mem) < 0)
			line("disp: DSI PLL did NOT lock (STATUS=%08x)",
			     mapreg(mem, DSI_PLL_BASE + 4) ?
			     *mapreg(mem, DSI_PLL_BASE + 4) : 0);
		else
			line("disp: DSI PLL locked (STATUS=%08x)",
			     *mapreg(mem, DSI_PLL_BASE + 4));

		for (i = 0; i < (int)(sizeof(dsi_pe) / sizeof(dsi_pe[0])); i++)
			p[dsi_pe[i][0] / 4] = dsi_pe[i][1];
		for (i = 0; i < (int)(sizeof(phy) / sizeof(phy[0])); i++)
			ph[phy[i][0] / 4] = phy[i][1];

		line("disp: DSI CTRL %08x VM1 %08x VM2 %08x VM3 %08x "
		     "IRQ %08x VC0 %08x", p[0x40 / 4], p[0x60 / 4],
		     p[0x64 / 4], p[0x68 / 4], p[0x18 / 4], p[0x118 / 4]);
		line("disp: PHY CFG0 %08x CFG1 %08x CFG2 %08x",
		     ph[0x00 / 4], ph[0x04 / 4], ph[0x08 / 4]);
	}

	/* ---- 4. go: LCDENABLE | STNTFT | 24-bit | GOLCD ---- */
	d[0x40 / 4] = 0x00000329;
	usleep(50000);
	{
		unsigned int l1 = d[0x5c / 4], i1 = d[0x18 / 4], l2, i2;

		usleep(100000);
		l2 = d[0x5c / 4];
		i2 = d[0x18 / 4];
		line("disp: CONTROL %08x  LINE %x -> %x  IRQ %08x -> %08x",
		     d[0x40 / 4], l1, l2, i1, i2);
		if (l1 != l2 || i2 & 0x2)
			line("disp: raster looks LIVE (line moved / VSYNC)");
		else
			line("disp: raster NOT advancing -- check the DSS "
			     "clock and the DSI PLL");
	}

	close(mem);
}

static void disp_run(const char *what)
{
	pid_t pid = fork();
	int st = 0;

	if (pid == 0) {
		alarm(20);
		disp_child(what);
		_exit(0);
	}
	if (pid < 0) {
		line("disp: fork failed: %s", strerror(errno));
		return;
	}
	/*
	 * Bounded wait.  A write to a target that is in module standby does not
	 * abort -- it never completes, and the child sits in an uninterruptible
	 * bus cycle (alarm(2) cannot break it).  Blocking here would make the
	 * console useless, so give up after 25 s and leave the child; the
	 * console keeps working even though the bench cycle is lost.
	 */
	{
		int waited = 0;

		while (waitpid(pid, &st, WNOHANG) != pid) {
			if (waited >= 250) {
				line("!! disp worker did not finish in 25 s -- "
				     "abandoning it (hung bus cycle; the bus is "
				     "probably wedged, expect a reset)");
				return;
			}
			usleep(100000);
			waited++;
		}
	}
	if (WIFSIGNALED(st)) {
		int sig = WTERMSIG(st);

		if (sig == SIGBUS)
			line("!! disp worker killed by SIGBUS: a gated target "
			     "was touched (is PM_DSS on?)");
		else if (sig == SIGALRM)
			line("!! disp worker timed out (hung bus cycle)");
		else
			line("!! disp worker killed by signal %d", sig);
	}
}

/* ---- LM3532 backlight over /dev/i2c-* (report 111 §4.2) ---- */

static int i2c_open(int *bus_out)
{
	int bus, fd;
	char p[32];

	for (bus = 0; bus < 4; bus++) {
		snprintf(p, sizeof(p), "/dev/i2c-%d", bus);
		fd = open(p, O_RDWR);
		if (fd >= 0) {
			*bus_out = bus;
			return fd;
		}
	}
	return -1;
}

static int i2c_wr(int fd, int addr, unsigned char reg, unsigned char val)
{
	unsigned char b[2] = { reg, val };
	struct i2c_msg m = { .addr = addr, .flags = 0, .len = 2, .buf = b };
	struct i2c_rdwr_ioctl_data d = { .msgs = &m, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

static int i2c_rd(int fd, int addr, unsigned char reg, unsigned char *out)
{
	unsigned char b[1];
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = b },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };

	if (ioctl(fd, I2C_RDWR, &d) < 0)
		return -1;
	*out = b[0];
	return 0;
}

/* plain 1-byte read: the standard "is anything there?" probe */
static int i2c_ping(int fd, int addr)
{
	unsigned char b;
	struct i2c_msg m = { .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &b };
	struct i2c_rdwr_ioctl_data d = { .msgs = &m, .nmsgs = 1 };

	return ioctl(fd, I2C_RDWR, &d) < 0 ? -1 : 0;
}

static void bl_scan_child(int bus)
{
	char p[32];
	int fd, a, n = 0;

	snprintf(p, sizeof(p), "/dev/i2c-%d", bus);
	fd = open(p, O_RDWR);
	if (fd < 0) {
		line("scan: %s: %s", p, strerror(errno));
		return;
	}
	for (a = 0x08; a < 0x78; a++) {
		if (i2c_ping(fd, a) == 0) {
			line("scan: ACK at 0x%02x", a);
			n++;
		}
	}
	line("scan: i2c-%d done, %d device(s)", bus, n);
	close(fd);
}

static void cmd_bl(char **av, int ac)
{
	int bus = -1, fd;
	unsigned char v;
	unsigned long reg, val;
	int i;
	static const unsigned char init[][2] = {
		{ 0x70, 0xff }, { 0x17, 0x19 }, { 0x13, 0x06 }, { 0x1d, 0x01 },
		{ 0x11, 0xff }, { 0x12, 0xff }, { 0x70, 0xff },
	};

	/*
	 * `bl scan [bus]` runs in a forked worker with a bounded wait: a scan
	 * touches addresses that may belong to an unpowered slave, and an
	 * unpowered slave holds the bus and never completes the transfer
	 * (report 118 §9 / trap 16).
	 */
	if (ac > 1 && !strcmp(av[1], "scan")) {
		int b = ac > 2 ? atoi(av[2]) : 0;
		pid_t pid = fork();
		int st = 0, waited = 0;

		if (pid == 0) {
			alarm(10);
			bl_scan_child(b);
			_exit(0);
		}
		if (pid < 0) {
			line("scan: fork failed");
			return;
		}
		while (waitpid(pid, &st, WNOHANG) != pid) {
			if (waited++ >= 200) {
				line("!! scan worker did not finish in 20 s -- "
				     "abandoning it (hung bus)");
				return;
			}
			usleep(100000);
		}
		if (WIFSIGNALED(st))
			line("!! scan worker killed by signal %d",
			     WTERMSIG(st));
		return;
	}

	fd = i2c_open(&bus);
	if (fd < 0) {
		line("bl: no /dev/i2c-* (is i2c1 enabled and I2C_CHARDEV=y?)");
		return;
	}
	if (i2c_rd(fd, 0x38, 0x70, &v) < 0 && i2c_wr(fd, 0x38, 0x70, 0) < 0) {
		line("bl: no LM3532 at 0x38 on i2c-%d", bus);
		close(fd);
		return;
	}

	if (ac == 1 || !strcmp(av[1], "init")) {
		for (i = 0; i < (int)(sizeof(init) / sizeof(init[0])); i++)
			if (i2c_wr(fd, 0x38, init[i][0], init[i][1]) < 0)
				line("bl: write %02x<-%02x failed",
				     init[i][0], init[i][1]);
		line("bl: LM3532 initialised on i2c-%d (full brightness)", bus);
		return;
	}
	if (!strcmp(av[1], "dump")) {
		static const unsigned char r[] = { 0x11, 0x12, 0x13, 0x17,
						   0x1d, 0x70 };

		for (i = 0; i < (int)sizeof(r); i++)
			if (i2c_rd(fd, 0x38, r[i], &v) == 0)
				line("bl: reg %02x = %02x", r[i], v);
		close(fd);
		return;
	}
	if (!strcmp(av[1], "rd") && ac == 3) {
		if (hexval(av[2], &reg) || reg > 0xff) {
			line("bl: bad register");
			close(fd);
			return;
		}
		if (i2c_rd(fd, 0x38, (unsigned char)reg, &v) == 0)
			line("bl: reg %02lx = %02x", reg, v);
		else
			line("bl: read %02lx failed: %s", reg, strerror(errno));
		close(fd);
		return;
	}
	if (!strcmp(av[1], "wr") && ac == 4) {
		if (hexval(av[2], &reg) || hexval(av[3], &val) ||
		    reg > 0xff || val > 0xff) {
			line("bl: bad argument");
			close(fd);
			return;
		}
		if (i2c_wr(fd, 0x38, (unsigned char)reg, (unsigned char)val) == 0)
			line("bl: reg %02lx <- %02lx", reg, val);
		else
			line("bl: write failed: %s", strerror(errno));
		close(fd);
		return;
	}
	line("bl: use 'bl init', 'bl dump', 'bl rd <reg>', 'bl wr <reg> <val>'");
	close(fd);
}

/* the body, run in a child so a stuck transfer cannot wedge the console */
static void i2c_do(char **av, int ac)
{
	char p[32];
	unsigned long bus, addr, reg, val;
	unsigned char v;
	int fd;

	if (ac < 5 || (av[1][0] != 'w' && av[1][0] != 'r')) {
		line("i2c: use 'i2c w <bus> <addr> <reg> <val>' or "
		     "'i2c r <bus> <addr> <reg>'  (all hex)");
		return;
	}
	if (hexval(av[2], &bus) || hexval(av[3], &addr) || hexval(av[4], &reg)) {
		line("i2c: bad hex argument");
		return;
	}
	snprintf(p, sizeof(p), "/dev/i2c-%lu", bus);
	fd = open(p, O_RDWR);
	if (fd < 0) {
		line("i2c: %s: %s", p, strerror(errno));
		return;
	}
	if (av[1][0] == 'w') {
		if (ac < 6 || hexval(av[5], &val)) {
			line("i2c: need a value");
			close(fd);
			return;
		}
		if (i2c_wr(fd, (int)addr, (unsigned char)reg,
			   (unsigned char)val) == 0)
			line("i2c: %s 0x%02lx[0x%02lx] <- 0x%02lx", p, addr,
			     reg, val);
		else
			line("i2c: write 0x%02lx[0x%02lx] failed: %s", addr,
			     reg, strerror(errno));
	} else {
		if (i2c_rd(fd, (int)addr, (unsigned char)reg, &v) == 0)
			line("i2c: %s 0x%02lx[0x%02lx] = 0x%02x", p, addr, reg,
			     v);
		else
			line("i2c: read 0x%02lx[0x%02lx] failed: %s", addr, reg,
			     strerror(errno));
	}
	close(fd);
}

static void cmd_i2c(char **av, int ac)
{
	pid_t pid;
	int st = 0, waited = 0;

	if (ac < 2) {
		line("i2c: use 'i2c w <bus> <addr> <reg> <val>' or "
		     "'i2c r <bus> <addr> <reg>'  (all hex)");
		return;
	}
	pid = fork();
	if (pid == 0) {
		alarm(5);
		i2c_do(av, ac);
		_exit(0);
	}
	if (pid < 0) {
		line("i2c: fork failed");
		return;
	}
	while (waitpid(pid, &st, WNOHANG) != pid) {
		if (waited++ >= 100) {
			line("!! i2c worker did not finish in 10 s -- abandoning "
			     "it (hung bus)");
			return;
		}
		usleep(100000);
	}
	if (WIFSIGNALED(st))
		line("!! i2c worker killed by signal %d", WTERMSIG(st));
}

/* ---------------------------------------------------------------- i2c16 */
/*
 * I2C with 16-bit register addressing, for devices the plain 8-bit-register
 * `i2c` command cannot reach (the TC358764 DSI/LVDS bridge uses 16-bit
 * addresses and 32-bit registers).  Bus 0 has an unidentified responder at
 * 0x09 — the bridge is the obvious candidate.
 */
static int i2c16_rd(int fd, int addr, unsigned reg, unsigned char *out, int n)
{
	unsigned char rb[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg msgs[2] = {
		{ .addr = addr, .flags = 0, .len = 2, .buf = rb },
		{ .addr = addr, .flags = I2C_M_RD, .len = (unsigned short)n,
		  .buf = out },
	};
	struct i2c_rdwr_ioctl_data x = { .msgs = msgs, .nmsgs = 2 };

	return ioctl(fd, I2C_RDWR, &x);
}

static int i2c16_wr(int fd, int addr, unsigned reg, unsigned long val, int n)
{
	unsigned char buf[8];
	struct i2c_msg msg = { .addr = addr, .flags = 0, .len = n + 2,
			       .buf = buf };
	struct i2c_rdwr_ioctl_data x = { .msgs = &msg, .nmsgs = 1 };

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;
	for (int i = 0; i < n; i++)
		buf[2 + i] = (val >> (8 * i)) & 0xff;

	return ioctl(fd, I2C_RDWR, &x);
}

static void i2c16_do(char **av, int ac)
{
	char p[32];
	unsigned long bus, addr, reg, val = 0, n = 1;
	unsigned char buf[8];
	int fd, i;

	if (ac < 5) {
		line("i2c16: 'r <bus> <addr> <reg16> [n]' or "
		     "'w <bus> <addr> <reg16> <val> [n]'  (all hex)");
		return;
	}
	if (hexval(av[2], &bus) || hexval(av[3], &addr) ||
	    hexval(av[4], &reg)) {
		line("i2c16: bad hex argument");
		return;
	}
	snprintf(p, sizeof(p), "/dev/i2c-%lu", bus);
	fd = open(p, O_RDWR);
	if (fd < 0) {
		line("i2c16: %s: %s", p, strerror(errno));
		return;
	}
	if (av[1][0] == 'w') {
		if (ac < 6 || hexval(av[5], &val)) {
			line("i2c16: need a value");
			close(fd);
			return;
		}
		if (ac > 6 && hexval(av[6], &n)) {
			line("i2c16: bad length");
			close(fd);
			return;
		}
		if (n < 1 || n > 4) {
			line("i2c16: length 1..4");
			close(fd);
			return;
		}
		if (i2c16_wr(fd, (int)addr, (unsigned)reg, val, (int)n) == 0)
			line("i2c16: %s 0x%02lx[0x%04lx] <- 0x%lx (%lu byte)", p,
			     addr, reg, val, n);
		else
			line("i2c16: write failed: %s", strerror(errno));
	} else {
		if (ac > 5 && hexval(av[5], &n)) {
			line("i2c16: bad length");
			close(fd);
			return;
		}
		if (n < 1 || n > 8) {
			line("i2c16: length 1..8");
			close(fd);
			return;
		}
		if (i2c16_rd(fd, (int)addr, (unsigned)reg, buf, (int)n) == 0) {
			for (i = 0; i < (int)n; i++)
				line("i2c16: %s 0x%02lx[0x%04lx].%d = 0x%02x", p,
				     addr, reg, i, buf[i]);
		} else {
			line("i2c16: read failed: %s", strerror(errno));
		}
	}
	close(fd);
}

static void cmd_i2c16(char **av, int ac)
{
	pid_t pid;
	int st = 0, waited = 0;

	if (ac < 2) {
		line("i2c16: 'r <bus> <addr> <reg16> [n]' or "
		     "'w <bus> <addr> <reg16> <val> [n]'  (all hex)");
		return;
	}
	pid = fork();
	if (pid == 0) {
		alarm(5);
		i2c16_do(av, ac);
		_exit(0);
	}
	if (pid < 0) {
		line("i2c16: fork failed");
		return;
	}
	while (waitpid(pid, &st, WNOHANG) != pid) {
		if (waited++ >= 100) {
			line("!! i2c16 worker did not finish in 10 s -- "
			     "abandoning it (hung bus)");
			return;
		}
		usleep(100000);
	}
	if (WIFSIGNALED(st))
		line("!! i2c16 worker killed by signal %d", WTERMSIG(st));
}

/* ------------------------------------------------------------------ spi */
/*
 * The panel TCON's init table (report 111 §3.5).  QNX sends each u16 as two
 * bytes, low byte first, one transfer per word, with a 100 ms gap after word
 * 0, word 2 and the last word.  spidev is /dev/spidev3.0 (DT alias spi3 ->
 * mcspi4, the controller QNX calls /dev/spi3).
 */
static const unsigned short tcon_hitachi[] = {
	0x0029, 0x0025, 0x0840, 0x00AD, 0x0430, 0x385F, 0x3CA4, 0x3409, 0x4041
};

static int spi_word(int fd, unsigned short w)
{
	unsigned char b[2] = { w & 0xff, w >> 8 };
	struct spi_ioc_transfer tr;

	memset(&tr, 0, sizeof(tr));
	tr.tx_buf = (unsigned long)b;
	tr.len = 2;
	tr.speed_hz = 450000;
	tr.bits_per_word = 8;
	return ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0 ? -errno : 0;
}

/*
 * The reference sends each u16 high byte first: winch_lcdctl swaps the word
 * before the transfer (0x1c51c: lsr/orr r3,r2,r3,lsl#8).  Report 123 §2.
 */
static int spi_word_be(int fd, unsigned short w)
{
	unsigned char b[2] = { w >> 8, w & 0xff };
	struct spi_ioc_transfer tr;

	memset(&tr, 0, sizeof(tr));
	tr.tx_buf = (unsigned long)b;
	tr.len = 2;
	tr.speed_hz = 450000;
	tr.bits_per_word = 8;
	return ioctl(fd, SPI_IOC_MESSAGE(1), &tr) < 0 ? -errno : 0;
}

static void spi_do(char **av, int ac)
{
	const char *dev = "/dev/spidev3.0";
	unsigned int mode = 0, bits = 8, speed = 450000;
	unsigned long w;
	int fd, i, ret, nw;

	fd = open(dev, O_RDWR);
	if (fd < 0) {
		line("spi: %s: %s", dev, strerror(errno));
		return;
	}
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) ||
	    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed))
		line("spi: setup: %s", strerror(errno));

	if (ac >= 2 && !strcmp(av[1], "tcon")) {
		nw = (int)(sizeof(tcon_hitachi) / sizeof(tcon_hitachi[0]));
		for (i = 0; i < nw; i++) {
			ret = spi_word(fd, tcon_hitachi[i]);
			line("spi: tcon[%d] = 0x%04x %s", i, tcon_hitachi[i],
			     ret ? strerror(-ret) : "ok");
			if (ret)
				break;
			if (i == 0 || i == 2 || i == nw - 1)
				usleep(100000);
		}
	} else if (ac >= 2 && !strcmp(av[1], "tconbe")) {
		/* the reference's byte order: each u16 high byte first */
		nw = (int)(sizeof(tcon_hitachi) / sizeof(tcon_hitachi[0]));
		for (i = 0; i < nw; i++) {
			ret = spi_word_be(fd, tcon_hitachi[i]);
			line("spi: tconbe[%d] = 0x%04x %s", i, tcon_hitachi[i],
			     ret ? strerror(-ret) : "ok");
			if (ret)
				break;
			if (i == 0 || i == 2 || i == nw - 1)
				usleep(100000);
		}
	} else if (ac >= 3 && av[1][0] == 'w') {
		for (i = 2; i < ac; i++) {
			if (hexval(av[i], &w)) {
				line("spi: bad hex word '%s'", av[i]);
				break;
			}
			ret = spi_word(fd, (unsigned short)w);
			line("spi: w 0x%04lx %s", w,
			     ret ? strerror(-ret) : "ok");
			if (ret)
				break;
		}
	} else {
		line("spi: use 'spi tcon|tconbe' or 'spi w <hexword> ...'");
	}
	close(fd);
}

static void cmd_spi(char **av, int ac)
{
	pid_t pid;
	int st = 0, waited = 0;

	if (ac < 2) {
		line("spi: use 'spi tcon|tconbe' or 'spi w <hexword> ...'");
		return;
	}
	pid = fork();
	if (pid == 0) {
		alarm(10);
		spi_do(av, ac);
		_exit(0);
	}
	if (pid < 0) {
		line("spi: fork failed");
		return;
	}
	while (waitpid(pid, &st, WNOHANG) != pid) {
		if (waited++ >= 200) {
			line("!! spi worker did not finish in 20 s -- abandoning "
			     "it");
			return;
		}
		usleep(100000);
	}
	if (WIFSIGNALED(st))
		line("!! spi worker killed by signal %d", WTERMSIG(st));
}

/* ------------------------------------------------------------------- fb */
/*
 * Paint the framebuffer a solid colour.  This is the only way to tell "the
 * panel shows nothing" from "the panel shows a black framebuffer": omapdrm's
 * primary plane covers the whole screen, so DISPC_DEFAULT_COLOR is invisible.
 */
static void fb_do(char **av, int ac)
{
	const char *dev = "/dev/fb0";
	struct fb_fix_screeninfo fi;
	struct fb_var_screeninfo vi;
	unsigned long color;
	unsigned int *p;
	unsigned char *m;
	size_t n;
	int fd;

	if (ac < 2 || hexval(av[1], &color)) {
		line("fb: use 'fb <rrggbb>'");
		return;
	}
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		line("fb: %s: %s", dev, strerror(errno));
		return;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fi) ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &vi)) {
		line("fb: ioctl: %s", strerror(errno));
		close(fd);
		return;
	}
	m = mmap(NULL, fi.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (m == MAP_FAILED) {
		line("fb: mmap: %s", strerror(errno));
		close(fd);
		return;
	}
	n = fi.line_length * vi.yres_virtual;
	if (n > fi.smem_len)
		n = fi.smem_len;
	p = (unsigned int *)m;
	for (size_t i = 0; i < n / 4; i++)
		p[i] = (unsigned int)color;
	msync(m, n, MS_SYNC);
	munmap(m, fi.smem_len);
	line("fb: %ux%u %ubpp, %zu bytes <- 0x%06lx", vi.xres, vi.yres,
	     vi.bits_per_pixel, n, color);
	close(fd);
}

static void cmd_fb(char **av, int ac)
{
	pid_t pid;
	int st = 0, waited = 0;

	if (ac < 2) {
		line("fb: use 'fb <rrggbb>'");
		return;
	}
	pid = fork();
	if (pid == 0) {
		alarm(10);
		fb_do(av, ac);
		_exit(0);
	}
	if (pid < 0) {
		line("fb: fork failed");
		return;
	}
	while (waitpid(pid, &st, WNOHANG) != pid) {
		if (waited++ >= 200) {
			line("!! fb worker did not finish in 20 s");
			return;
		}
		usleep(100000);
	}
	if (WIFSIGNALED(st))
		line("!! fb worker killed by signal %d", WTERMSIG(st));
}

/* ----------------------------------------------------------------- kmsg */
/*
 * Dump the kernel log ring.  /dev/kmsg records look like
 * "prio,seq,usec,flags;message".  Optional substring filter, because the
 * console link is slow and a full boot log is mostly noise.
 */
static void cmd_kmsg(char **av, int ac)
{
	const char *pat = ac > 1 ? av[1] : NULL;
	char buf[2048];
	int fd, i, n, shown = 0;

	fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		line("kmsg: %s", strerror(errno));
		return;
	}
	lseek(fd, 0, SEEK_SET);
	for (i = 0; i < 20000; i++) {
		char *m;
		int len;

		n = read(fd, buf, sizeof(buf) - 1);
		if (n <= 0)
			break;
		buf[n] = 0;
		m = strchr(buf, ';');
		m = m ? m + 1 : buf;
		len = strlen(m);
		while (len && (m[len - 1] == '\n' || m[len - 1] == '\r'))
			m[--len] = 0;
		if (!pat || strstr(m, pat)) {
			line("%s", m);
			shown++;
		}
	}
	line("kmsg: %d record(s)%s%s", shown, pat ? " matching " : "",
	     pat ? pat : "");
	close(fd);
}

/* ------------------------------------------------------------------ blk */
/*
 * Read-only block access to the eMMC.  Session 16's first task is to prove the
 * storage path still returns exactly what was written now that the DSS clock
 * fix moves the PER-PLL output dividers at probe time, so this hashes raw
 * sectors.  There is deliberately no write operation: SAFETY.md forbids eMMC
 * writes from the agent.  Report 134.
 */
#define BLK_DEV	"/dev/mmcblk1"
#define BLK_SS	512

static int blk_hash_bytes(unsigned long long off, unsigned long long len,
			  unsigned long long *out)
{
	unsigned char buf[65536];
	unsigned long long h = 0xcbf29ce484222325ULL, total = 0;
	int fd;

	fd = open(BLK_DEV, O_RDONLY);
	if (fd < 0) {
		line("blk: %s: %s", BLK_DEV, strerror(errno));
		return -1;
	}
	while (total < len) {
		size_t want = (size_t)(len - total);
		ssize_t got;

		if (want > sizeof(buf))
			want = sizeof(buf);
		got = pread(fd, buf, want, (off_t)(off + total));
		if (got <= 0) {
			line("blk: read off 0x%llx+0x%llx: %s", off, total,
			     got < 0 ? strerror(errno) : "short read");
			close(fd);
			return -1;
		}
		for (ssize_t i = 0; i < got; i++) {
			h ^= buf[i];
			h *= 0x100000001b3ULL;
		}
		total += (unsigned long long)got;
	}
	close(fd);
	*out = h;
	return 0;
}

static int blk_hash(unsigned long lba, unsigned long nsect,
		    unsigned long long *out)
{
	return blk_hash_bytes((unsigned long long)lba * BLK_SS,
			      (unsigned long long)nsect * BLK_SS, out);
}

static void blk_do(char **av, int ac)
{
	unsigned long lba, nsect = 1;
	unsigned long long h;

	if (ac < 2) {
		line("blk: use 'blk info', 'blk sum <lba> [sectors]' or "
		     "'blk rd <lba> [sectors]' (lba hex, count decimal)");
		return;
	}
	if (!strcmp(av[1], "info")) {
		struct stat st;
		int fd = open(BLK_DEV, O_RDONLY);

		if (fd < 0) {
			line("blk: %s: %s", BLK_DEV, strerror(errno));
			return;
		}
		if (fstat(fd, &st) == 0)
			line("blk: %s size %llu B (%llu MiB) mode %04o",
			     BLK_DEV, (unsigned long long)st.st_size,
			     (unsigned long long)st.st_size >> 20,
			     st.st_mode & 07777);
		close(fd);
		return;
	}
	if (ac < 3 || hexval(av[2], &lba)) {
		line("blk: need a hex LBA");
		return;
	}
	if (!strcmp(av[1], "sumb")) {
		/* byte offset (hex) + length (decimal): hashes anything */
		char *end;
		unsigned long long len;

		if (ac != 4) {
			line("blk: use 'blk sumb <byteoff-hex> <len>'");
			return;
		}
		len = strtoull(av[3], &end, 0);
		if (*end || !len) {
			line("blk: bad length");
			return;
		}
		if (blk_hash_bytes((unsigned long long)lba, len, &h) == 0)
			line("blk: sumb off 0x%lx len %llu: fnv1a64 %016llx",
			     lba, len, h);
		return;
	}
	if (ac > 3) {
		char *end;

		nsect = strtoul(av[3], &end, 0);
		if (*end || !nsect || nsect > 0x100000) {
			line("blk: count 1..1048576");
			return;
		}
	}
	if (!strcmp(av[1], "sum")) {
		if (blk_hash(lba, nsect, &h) == 0)
			line("blk: sum lba 0x%lx %lu sect: fnv1a64 %016llx",
			     lba, nsect, h);
	} else if (!strcmp(av[1], "rd")) {
		unsigned char buf[BLK_SS * 4];
		size_t want = nsect > 4 ? BLK_SS * 4 : (size_t)nsect * BLK_SS;
		int fd = open(BLK_DEV, O_RDONLY);
		ssize_t got;

		if (fd < 0) {
			line("blk: %s: %s", BLK_DEV, strerror(errno));
			return;
		}
		got = pread(fd, buf, want, (off_t)lba * BLK_SS);
		if (got <= 0) {
			line("blk: read lba 0x%lx: %s", lba,
			     got < 0 ? strerror(errno) : "short read");
			close(fd);
			return;
		}
		for (ssize_t i = 0; i < got; i += 16) {
			char b[128];
			int o = snprintf(b, sizeof(b), "%08llx:",
					 (unsigned long long)((off_t)lba * BLK_SS + i));

			for (ssize_t j = 0; j < 16 && i + j < got; j++)
				o += snprintf(b + o, sizeof(b) - o, " %02x",
					      buf[i + j]);
			line("%s", b);
		}
		close(fd);
	} else {
		line("blk: use 'info', 'sum <lba> [n]' or 'rd <lba> [n]'");
	}
}

/* ----------------------------------------------------------------- recv */
/*
 * Receive a byte stream from the console link and write it to raw media.
 * This is the hd3-shrink write path (report 135): the Linux initramfs is the
 * only environment where hd3 is *not* mounted, so it must be able to write a
 * pre-built filesystem image to /dev/mmcblk1.  The host side is
 * tools/pbrecv.py: it sends `recv <byteoff-hex> <len>`, waits for
 * "recv: ready", streams exactly <len> bytes, then reads the FNV-1a64 the
 * device computed over what it wrote.
 *
 * The console link is raw (cfmakeraw), so no byte translation happens.
 */
static void cmd_recv(char **av, int ac)
{
	unsigned long long off, len, got = 0;
	unsigned long long h = 0xcbf29ce484222325ULL;
	unsigned char *buf;
	char *end;
	int fd;

	if (ac != 3) {
		line("recv: use 'recv <byteoff-hex> <len-decimal>'");
		return;
	}
	errno = 0;
	off = strtoull(av[1], &end, 16);
	if (errno || *end) {
		line("recv: bad offset");
		return;
	}
	errno = 0;
	len = strtoull(av[2], &end, 0);
	if (errno || *end || !len) {
		line("recv: bad length");
		return;
	}
	fd = open(BLK_DEV, O_RDWR);
	if (fd < 0) {
		line("recv: %s: %s", BLK_DEV, strerror(errno));
		return;
	}
	buf = malloc(65536);
	if (!buf) {
		line("recv: out of memory");
		close(fd);
		return;
	}
	line("recv: ready off 0x%llx len %llu", off, len);
	while (got < len) {
		size_t want = (size_t)(len - got);
		ssize_t n;

		if (want > 65536)
			want = 65536;
		n = read(cfd, buf, want);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			line("recv: read failed: %s", strerror(errno));
			break;
		}
		if (n == 0)
			break;
		if (pwrite(fd, buf, n, (off_t)(off + got)) != n) {
			line("recv: write at 0x%llx failed: %s",
			     off + got, strerror(errno));
			break;
		}
		for (ssize_t i = 0; i < n; i++) {
			h ^= buf[i];
			h *= 0x100000001b3ULL;
		}
		got += (unsigned long long)n;
		if ((got & 0x3fffffffULL) == 0 || got == len)
			line("recv: %llu/%llu bytes", got, len);
	}
	fsync(fd);
	close(fd);
	free(buf);
	line("recv: done %llu/%llu bytes fnv1a64 %016llx%s", got, len, h,
	     got == len ? "" : "  (SHORT)");
}

/* ----------------------------------------------------------------- sink */
/*
 * Measure the *console link* alone: read <len> bytes from the host and drop
 * them.  Comparing this with `blkwr` separates a slow USB/ACM path from a slow
 * media path (report 136: the old 17-32 KB/s figure was taken with per-request
 * printk to the fbcon console enabled).
 */
static long long now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void cmd_sink(char **av, int ac)
{
	unsigned long long len, got = 0;
	unsigned char *buf;
	long long t0, us;
	char *end;

	if (ac != 2) {
		line("sink: use 'sink <len>'");
		return;
	}
	errno = 0;
	len = strtoull(av[1], &end, 0);
	if (errno || *end || !len) {
		line("sink: bad length");
		return;
	}
	buf = malloc(65536);
	if (!buf) {
		line("sink: out of memory");
		return;
	}
	line("sink: ready len %llu", len);
	t0 = now_us();
	while (got < len) {
		size_t want = (size_t)(len - got);
		ssize_t n;

		if (want > 65536)
			want = 65536;
		n = read(cfd, buf, want);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			line("sink: read failed: %s", strerror(errno));
			break;
		}
		if (n == 0)
			break;
		got += (unsigned long long)n;
	}
	us = now_us() - t0;
	free(buf);
	line("sink: done %llu/%llu bytes in %lld us (%llu KB/s)%s", got, len,
	     us, us > 0 ? got * 1000 / us : 0, got == len ? "" : "  (SHORT)");
}

/* ---------------------------------------------------------------- blkwr */
/*
 * Self-timed raw write, independent of the console link.  O_DIRECT (one
 * request per chunk, no page cache), deterministic pattern
 *
 *     buf[i] = (unsigned char)(byteoff + written + i)
 *
 * so the host can reproduce the FNV-1a64 and verify the media with
 * `blk sumb <byteoff> <len>`.  Test writes belong in region 1 (raw
 * 0x28000000..0x47F00000, empty); never point it at region 0, NVRAM, RPMB or
 * the TableOS FS (SAFETY.md).
 */
static void cmd_blkwr(char **av, int ac)
{
	unsigned long long off, len, chunk = 1 << 20, done = 0;
	unsigned long long h = 0xcbf29ce484222325ULL;
	unsigned char *buf;
	long long t0, us;
	char *end;
	int fd;

	if (ac < 3) {
		line("blkwr: use 'blkwr <byteoff-hex> <len> [chunk]'");
		return;
	}
	errno = 0;
	off = strtoull(av[1], &end, 16);
	if (errno || *end) {
		line("blkwr: bad offset");
		return;
	}
	errno = 0;
	len = strtoull(av[2], &end, 0);
	if (errno || *end || !len) {
		line("blkwr: bad length");
		return;
	}
	if (ac > 3) {
		errno = 0;
		chunk = strtoull(av[3], &end, 0);
		if (errno || *end || !chunk) {
			line("blkwr: bad chunk");
			return;
		}
	}
	if ((off | len | chunk) & 4095) {
		line("blkwr: offset, length and chunk must be 4096-aligned");
		return;
	}
	if (chunk > (16 << 20)) {
		line("blkwr: chunk <= 16 MiB");
		return;
	}
	if (posix_memalign((void **)&buf, 4096, (size_t)chunk)) {
		line("blkwr: out of memory");
		return;
	}
	fd = open(BLK_DEV, O_RDWR | O_DIRECT);
	if (fd < 0) {
		line("blkwr: %s: %s", BLK_DEV, strerror(errno));
		free(buf);
		return;
	}
	line("blkwr: off 0x%llx len %llu chunk %llu", off, len, chunk);
	t0 = now_us();
	while (done < len) {
		size_t n = (size_t)((len - done) < chunk ? (len - done) : chunk);
		ssize_t w;

		for (size_t i = 0; i < n; i++) {
			unsigned char b = (unsigned char)(off + done + i);

			buf[i] = b;
			h ^= b;
			h *= 0x100000001b3ULL;
		}
		w = pwrite(fd, buf, n, (off_t)(off + done));
		if (w != (ssize_t)n) {
			line("blkwr: write at 0x%llx failed: %s (%zd)",
			     off + done, w < 0 ? strerror(errno) : "short", w);
			break;
		}
		done += (unsigned long long)n;
	}
	fsync(fd);
	us = now_us() - t0;
	close(fd);
	free(buf);
	line("blkwr: done %llu/%llu bytes in %lld us (%llu KB/s) fnv1a64 %016llx%s",
	     done, len, us, us > 0 ? done * 1000 / us : 0, h,
	     done == len ? "" : "  (SHORT)");
}

/* ----------------------------------------------------------------- grep */
/*
 * Substring-filter a (possibly large) text file.  `cat` stops at CAT_MAX,
 * which is too small for /sys/kernel/debug/clk/clk_summary; this scans the
 * whole file and prints only matching lines.
 */
static void cmd_grep(char **av, int ac)
{
	const char *pat, *path;
	char buf[4096], lb[1024];
	size_t ll = 0;
	int fd, n, shown = 0;

	if (ac < 3) {
		line("grep: use 'grep <substr> <path>'");
		return;
	}
	pat = av[1];
	path = av[2];
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		line("grep: %s: %s", path, strerror(errno));
		return;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0 && shown < 600) {
		for (int i = 0; i < n && shown < 600; i++) {
			if (buf[i] == '\n') {
				lb[ll] = 0;
				if (strstr(lb, pat)) {
					line("%s", lb);
					shown++;
				}
				ll = 0;
			} else if (ll < sizeof(lb) - 1) {
				lb[ll++] = buf[i];
			}
		}
	}
	if (ll && shown < 600) {
		lb[ll] = 0;
		if (strstr(lb, pat)) {
			line("%s", lb);
			shown++;
		}
	}
	line("grep: %d line(s) matching '%s' in %s", shown, pat, path);
	close(fd);
}

static void cmd_disp(char **av, int ac)
{
	const char *what = ac > 1 ? av[1] : "init";

	if (!strcmp(what, "col") && ac == 3) {
		unsigned long c;
		pid_t pid;
		int st;

		if (hexval(av[2], &c)) {
			line("disp: bad colour");
			return;
		}
		pid = fork();
		if (pid == 0) {
			int mem = open("/dev/mem", O_RDWR | O_SYNC);
			volatile unsigned int *d = mem >= 0 ?
				mapreg(mem, DISPC_BASE) : NULL;

			if (d)
				d[0x4c / 4] = (unsigned int)c;
			line("disp: DEFAULT_COLOR <- %08lx (readback %08x)",
			     c, d ? d[0x4c / 4] : 0);
			_exit(0);
		}
		waitpid(pid, &st, 0);
		return;
	}
	disp_run(what);
}

/* ------------------------------------------------------------- commands */

static void cmd_help(void)
{
	line("PlayBook bench console (Linux bring-up, /dev/ttyGS0 <-> ttyACM0)");
	line("");
	line("  help                     this text");
	line("  ls [path]                list a directory (default /)");
	line("  cat <path>               print a file, first %d bytes", CAT_MAX);
	line("  peek <hexaddr> [count]   read count 32-bit words via /dev/mem");
	line("                           (default 1, max %d), one per line,", PEEK_MAX);
	line("                           printed as  <addr>: <value>");
	line("  poke <hexaddr> <hexval>  write one 32-bit word, read it back");
	line("  kick [on|off]            status / toggle the userspace WDT2 TGR");
	line("                           pet (2 s) and show CRR/LDR");
	line("  disp [init|stat|off]     replay the QNX display mode (report 118):");
	line("                           PM_DSS on, DISPC 1024x600@60, DSI PLL,");
	line("                           PHY, video timing, then LCDENABLE + GOLCD");
	line("  disp mux                 DSS_CONTROL <- 3 (DISPC/DSI1 clock mux);");
	line("                           refuses to run while the module is in");
	line("                           standby, because that write HANGS the bus");
	line("  disp col <rgb24>         set DISPC_DEFAULT_COLOR (solid field,");
	line("                           no overlay needed)");
	line("  bl [init|dump]           LM3532 backlight: init = full brightness");
	line("  bl rd <reg>              read an LM3532 register");
	line("  bl wr <reg> <val>        write an LM3532 register");
	line("  bl scan [bus]            probe every address on an i2c bus");
	line("                           (forked: an unpowered slave can hold");
	line("                           the bus and hang the transfer)");
	line("  i2c r <bus> <addr> <reg> read any i2c register (hex)");
	line("  i2c w <bus> <addr> <reg> <val>");
	line("  i2c16 r|w <bus> <addr> <reg16> ... 16-bit-register i2c");
	line("  spi tcon|tconbe          send the panel TCON table (low|high byte first)");
	line("  spi w <hexword> ...      send raw 16-bit words (low byte first)");
	line("  fb <rrggbb>              paint /dev/fb0 a solid colour");
	line("  kmsg [substr]            dump the kernel log (optionally filtered)");
	line("  grep <substr> <path>     print matching lines of any-size text file");
	line("  blk info                 eMMC size/mode (%s, read-only)", BLK_DEV);
	line("  blk sum <lba> [sectors]  FNV-1a64 of raw sectors (hex LBA)");
	line("  blk sumb <byteoff> <len> FNV-1a64 at a byte offset (hex off)");
	line("  blk rd <lba> [sectors]   hex dump up to 4 sectors");
	line("  recv <byteoff> <len>     receive a raw stream from the host and");
	line("                           write it to %s (tools/pbrecv.py)", BLK_DEV);
	line("  sink <len>               receive <len> bytes and discard them: the");
	line("                           console-link rate with no media (tools/pbsink.py)");
	line("  blkwr <byteoff> <len> [chunk]  timed O_DIRECT write of a known pattern");
	line("                           (hex off, 4096-aligned; region 1 test only)");
	line("  write <path> <text>      write a string to a file (sysfs)");
	line("  entropy <hex>            credit entropy from the host (root ioctl);");
	line("                           the SoC RNG is firewalled, so the CRNG");
	line("                           never seeds itself (trap 87)");
	line("  qnx6 [off-hex]           mount hd3 read-only on /mnt (default raw");
	line("                           0x49000000; read-only loop, never writable)");
	line("  rootfs [off-hex]         mount the region-1 ext4 root read-only on");
	line("                           /rootfs (default raw 0x29000000, noload)");
	line("  run <path> [args...]     run a program with stdio on the console");
	line("                           (e.g. 'run /pbrpmb probe')");
	line("  reboot                   reboot now (unit returns to the bootrom)");
	line("");
	line("  * addresses are PHYSICAL (e.g. 48040000 = DSS_REVISION).");
	line("  * every memory access runs in a forked worker: a bus abort is a");
	line("    SIGBUS in the worker, never in the console.");
	line("  * this console is a transcript-too: every command is also");
	line("    written to the DRAM ring at 0x88000000.");
	line("  * /init does NOT reboot on a timer (since session 15): the unit");
	line("    stays up until 'reboot' or the power button, one BOOTCTR unit.");
	line("");
	line("  bench example:  peek 4a314028 2   (WDT2 CRR + LDR)");
}

static int cmd_ls(const char *path)
{
	static char *names[256];
	int n = 0, i, j;
	DIR *d;
	struct dirent *de;

	if (!path || !*path)
		path = "/";
	d = opendir(path);
	if (!d) {
		line("ls: %s: %s", path, strerror(errno));
		return -1;
	}
	while ((de = readdir(d)) && n < (int)(sizeof(names) / sizeof(names[0]))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		names[n] = strdup(de->d_name);
		if (names[n])
			n++;
	}
	closedir(d);
	for (i = 1; i < n; i++) {			/* insertion sort: small */
		char *k = names[i];

		for (j = i - 1; j >= 0 && strcmp(names[j], k) > 0; j--)
			names[j + 1] = names[j];
		names[j + 1] = k;
	}
	for (i = 0; i < n; i++) {
		char full[512];
		struct stat st;
		int isdir = 0;

		snprintf(full, sizeof(full), "%s%s%s", path,
			 strcmp(path, "/") ? "/" : "", names[i]);
		if (!stat(full, &st))
			isdir = S_ISDIR(st.st_mode);
		line("%s%s", names[i], isdir ? "/" : "");
		free(names[i]);
	}
	if (!n)
		line("(empty)");
	return 0;
}

static int cmd_cat(const char *path)
{
	char buf[512];
	int fd, n, total = 0;

	if (!path) {
		line("cat: need a path");
		return -1;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		line("cat: %s: %s", path, strerror(errno));
		return -1;
	}
	while (total < CAT_MAX && (n = read(fd, buf, sizeof(buf))) > 0) {
		int i, o = 0;
		char out[1100];

		if (n > CAT_MAX - total)
			n = CAT_MAX - total;
		for (i = 0; i < n; i++) {
			if (buf[i] == '\n')
				out[o++] = '\r';
			if (buf[i] == '\r')
				continue;
			out[o++] = buf[i];
			if (o >= (int)sizeof(out) - 3) {
				write(cfd, out, o);
				o = 0;
			}
		}
		if (o)
			write(cfd, out, o);
		total += n;
	}
	close(fd);
	line("");
	line("-- %s: %d byte(s) shown%s", path, total,
	     total >= CAT_MAX ? " (truncated at the cap)" : "");
	return 0;
}

static int hexval(const char *s, unsigned long *out)
{
	char *end;

	if (!s || !*s)
		return -1;
	errno = 0;
	*out = strtoul(s, &end, 16);
	return (errno || *end) ? -1 : 0;
}

static void cmd_peek(char **av, int ac)
{
	unsigned long pa, count = 1;

	if (ac < 2) {
		line("peek: need a hex address");
		return;
	}
	if (hexval(av[1], &pa)) {
		line("peek: bad hex address '%s'", av[1]);
		return;
	}
	if (ac > 2) {
		char *end;

		count = strtoul(av[2], &end, 0);
		if (*end) {
			line("peek: bad count '%s'", av[2]);
			return;
		}
	}
	if (!count || count > PEEK_MAX) {
		line("peek: count must be 1..%d", PEEK_MAX);
		return;
	}
	mem_op(pa, (long)count, M_PEEK, 0);
}

static void cmd_poke(char **av, int ac)
{
	unsigned long pa, v;

	if (ac < 3) {
		line("poke: need <hexaddr> <hexval>");
		return;
	}
	if (hexval(av[1], &pa) || hexval(av[2], &v)) {
		line("poke: bad hex argument");
		return;
	}
	mem_op(pa, 1, M_POKE, (unsigned int)v);
}

static void kick_status(void)
{
	line("kick: userspace WDT2 pet %s (every %d ms)", kick_on ? "ON" : "OFF",
	     KICK_MS);
	line("kick: kernel pets TGR from a 2 s timer_list unconditionally "
	     "(no userspace knob)");
	mem_op(0, 0, M_KICK, 0);
}

static void cmd_kick(char **av, int ac)
{
	if (ac == 1) {
		kick_status();
		return;
	}
	if (!strcmp(av[1], "on")) {
		kick_on = 1;
		last_kick_ms = now_ms();
		line("kick: userspace pet ON");
		mem_op(0, 0, M_KICK, 0);
	} else if (!strcmp(av[1], "off")) {
		kick_on = 0;
		line("kick: userspace pet OFF");
		line("kick: note -- the kernel's own 2 s TGR pet cannot be "
		     "disabled from userspace (no knob in this kernel)");
		mem_op(0, 0, M_KICK, 0);
	} else {
		line("kick: use 'kick', 'kick on' or 'kick off'");
	}
}

static void cmd_write(char **av, int ac)
{
	int fd, n;

	if (ac < 3) {
		line("write: need <path> <text>");
		return;
	}
	fd = open(av[1], O_WRONLY);
	if (fd < 0) {
		line("write: %s: %s", av[1], strerror(errno));
		return;
	}
	n = write(fd, av[2], strlen(av[2]));
	line("write: %s <- '%s' (%d bytes)%s", av[1], av[2], n,
	     n < 0 ? strerror(errno) : "");
	close(fd);
}

/*
 * entropy <hex> — credit real entropy into the kernel pool (root-only ioctl).
 *
 * The OMAP4 hardware RNG is behind the secure firewall: the board DTS keeps
 * rng_target disabled because a non-secure read raises an L3 error and an
 * imprecise external abort.  The CRNG therefore never initialises on this unit
 * and /dev/urandom stays predictable — which matters because the RPMB nonce is
 * covered by the frame MAC (trap 87).  The console is root in the initramfs, so
 * the host can hand bytes over and RNDADDENTROPY credits them.  Run it before
 * any RPMB write; the RPMB tool reports whether it saw a seeded CRNG.
 */
static void cmd_entropy(char **av, int ac)
{
	struct rand_pool_info *rp;
	unsigned char b[64];
	const char *h;
	int fd, avail = -1, n = 0;

	if (ac < 2) {
		line("entropy: need <hex>, at least 32 hex digits");
		return;
	}
	for (h = av[1]; h[0] && h[1] && n < (int)sizeof(b); h += 2, n++) {
		unsigned v;

		if (sscanf(h, "%2x", &v) != 1)
			break;
		b[n] = (unsigned char)v;
	}
	if (n < 16) {
		line("entropy: %d byte(s) parsed, need at least 16", n);
		return;
	}
	rp = calloc(1, sizeof(*rp) + n);
	if (!rp) {
		line("entropy: out of memory");
		return;
	}
	rp->entropy_count = n * 8;
	rp->buf_size = n;
	memcpy(rp->buf, b, n);

	fd = open("/dev/random", O_RDWR);
	if (fd < 0) {
		line("entropy: open /dev/random: %s", strerror(errno));
		free(rp);
		return;
	}
	ioctl(fd, RNDGETENTCNT, &avail);
	if (ioctl(fd, RNDADDENTROPY, rp) < 0)
		line("entropy: RNDADDENTROPY: %s", strerror(errno));
	else
		line("entropy: credited %d bits (avail was %d)", n * 8, avail);
	close(fd);
	free(rp);
}

/* ------------------------------------------------------- read-only mounts */
/*
 * `qnx6` and `rootfs` share one mechanism: a LO_FLAGS_READ_ONLY loop device
 * over /dev/mmcblk1 at a byte offset, mounted MS_RDONLY.  This is the approved
 * read-only eMMC policy (docs/contradictions/
 * safety-no-emmc-mount-vs-readonly-root.md): the loop cannot write, and
 * ext4 additionally gets `noload` so a journal cannot be replayed.
 */
static void loop_mount_ro(const char *tag, unsigned long long off,
			  const char *type, const char *mnt, const char *data,
			  const char *what)
{
	struct loop_info64 li;
	char loopdev[32];
	int ctl, lfd, bfd, n;

	ctl = open("/dev/loop-control", O_RDWR);
	if (ctl < 0) {
		line("%s: /dev/loop-control: %s", tag, strerror(errno));
		return;
	}
	n = ioctl(ctl, LOOP_CTL_GET_FREE);
	close(ctl);
	if (n < 0) {
		line("%s: no free loop device (%s)", tag, strerror(errno));
		return;
	}
	snprintf(loopdev, sizeof(loopdev), "/dev/loop%d", n);
	bfd = open(BLK_DEV, O_RDONLY);
	if (bfd < 0) {
		line("%s: %s: %s", tag, BLK_DEV, strerror(errno));
		return;
	}
	lfd = open(loopdev, O_RDWR);
	if (lfd < 0) {
		line("%s: %s: %s", tag, loopdev, strerror(errno));
		close(bfd);
		return;
	}
	if (ioctl(lfd, LOOP_SET_FD, bfd) < 0) {
		line("%s: LOOP_SET_FD: %s", tag, strerror(errno));
		goto out;
	}
	memset(&li, 0, sizeof(li));
	li.lo_offset = (typeof(li.lo_offset))off;
	li.lo_flags = LO_FLAGS_READ_ONLY;
	if (ioctl(lfd, LOOP_SET_STATUS64, &li) < 0) {
		line("%s: LOOP_SET_STATUS64: %s", tag, strerror(errno));
		ioctl(lfd, LOOP_CLR_FD, 0);
		goto out;
	}
	mkdir(mnt, 0755);
	if (mount(loopdev, mnt, type, MS_RDONLY, data) < 0) {
		line("%s: mount %s at %s: %s", tag, loopdev, mnt, strerror(errno));
		ioctl(lfd, LOOP_CLR_FD, 0);
		goto out;
	}
	line("%s: %s (%s) over %s at 0x%llx mounted read-only on %s",
	     tag, loopdev, what, BLK_DEV, off, mnt);
out:
	close(lfd);
	close(bfd);
}

/*
 * `qnx6 [off-hex]` — the TabletOS root on hd3.  Its raw start was measured
 * (session 17) at 0x49000000 by writing an image and reading it back, i.e.
 * loader block 0x4915 under the (block - 0x15) rule; the QNX6 superblock is at
 * +0x2000.  This is how a post-boot hd3 is inspected (report 138).
 */
#define HD3_RAW_OFF	0x49000000ULL

static void cmd_qnx6(char **av, int ac)
{
	unsigned long long off = HD3_RAW_OFF;
	char *end;

	if (ac > 1) {
		errno = 0;
		off = strtoull(av[1], &end, 16);
		if (errno || *end) {
			line("qnx6: bad offset");
			return;
		}
	}
	loop_mount_ro("qnx6", off, "qnx6", "/mnt", NULL, "QNX6 hd3");
}

/*
 * `rootfs [off-hex]` — the region-1 Linux root (ext4, no journal) that
 * tools/build_rootfs.py builds and tools/pre_os_ifs_flash.py writes at raw
 * 0x29000000 (region 1 + 16 MiB, loader block 0x2915).  It is mounted at
 * /rootfs so `ls /rootfs` and `cat /rootfs/...` can inspect it.
 */
#define ROOTFS_RAW_OFF	0x29000000ULL

static void cmd_rootfs(char **av, int ac)
{
	unsigned long long off = ROOTFS_RAW_OFF;
	char *end;

	if (ac > 1) {
		errno = 0;
		off = strtoull(av[1], &end, 16);
		if (errno || *end) {
			line("rootfs: bad offset");
			return;
		}
	}
	loop_mount_ro("rootfs", off, "ext4", "/rootfs", "noload",
		      "ext4 region-1 root");
}

/* ------------------------------------------------------------------ run */
/*
 * Run a program from the initramfs with its stdio on the console.  This is how
 * tools that do not belong in the console binary itself are exercised (e.g.
 * /pbrpmb, the read-only RPMB probe).  Bounded so a hung tool cannot wedge the
 * console.
 */
static void cmd_run(char **av, int ac)
{
	char *argv[24];
	pid_t pid;
	int st = 0, waited = 0, i, n = ac - 1;

	if (ac < 2) {
		line("run: use 'run <path> [args...]'");
		return;
	}
	if (n > (int)(sizeof(argv) / sizeof(argv[0])) - 1) {
		line("run: too many arguments");
		return;
	}
	for (i = 0; i < n; i++)
		argv[i] = av[i + 1];
	argv[n] = NULL;

	pid = fork();
	if (pid == 0) {
		dup2(cfd, 0);
		dup2(cfd, 1);
		dup2(cfd, 2);
		execv(argv[0], argv);
		line("run: exec %s: %s", argv[0], strerror(errno));
		_exit(127);
	}
	if (pid < 0) {
		line("run: fork failed: %s", strerror(errno));
		return;
	}
	while (waitpid(pid, &st, WNOHANG) != pid) {
		if (waited++ >= 2400) {
			line("!! run: still running after 120 s, abandoning it");
			return;
		}
		usleep(50000);
	}
	if (WIFSIGNALED(st))
		line("run: killed by signal %d", WTERMSIG(st));
	else if (WEXITSTATUS(st))
		line("run: exit %d", WEXITSTATUS(st));
}

static void cmd_reboot(void)
{
	line("reboot: asking PID 1 to reboot now ...");
	rlog("PB: console: reboot");
	if (rb_fd >= 0 && write(rb_fd, "R", 1) == 1)
		pause();			/* PID 1 does it; never returns */
	line("reboot: pipe unavailable, calling reboot(2) myself");
	sync();
	reboot(RB_AUTOBOOT);
	line("reboot: reboot(2) returned: %s", strerror(errno));
}

static void run_line(char *l)
{
	char *av[24];
	int ac = 0;
	char *p;

	for (p = l; *p; p++)
		if (*p == '\t')
			*p = ' ';
	p = l;
	while (*p && ac < (int)(sizeof(av) / sizeof(av[0]))) {
		while (*p == ' ')
			p++;
		if (!*p)
			break;
		av[ac++] = p;
		while (*p && *p != ' ')
			p++;
		if (*p)
			*p++ = 0;
	}
	if (!ac)
		return;
	rlog(l);

	if (!strcmp(av[0], "help") || !strcmp(av[0], "?"))
		cmd_help();
	else if (!strcmp(av[0], "ls"))
		cmd_ls(ac > 1 ? av[1] : "/");
	else if (!strcmp(av[0], "cat"))
		cmd_cat(ac > 1 ? av[1] : NULL);
	else if (!strcmp(av[0], "peek"))
		cmd_peek(av, ac);
	else if (!strcmp(av[0], "poke"))
		cmd_poke(av, ac);
	else if (!strcmp(av[0], "kick"))
		cmd_kick(av, ac);
	else if (!strcmp(av[0], "disp"))
		cmd_disp(av, ac);
	else if (!strcmp(av[0], "bl"))
		cmd_bl(av, ac);
	else if (!strcmp(av[0], "i2c"))
		cmd_i2c(av, ac);
	else if (!strcmp(av[0], "i2c16"))
		cmd_i2c16(av, ac);
	else if (!strcmp(av[0], "spi"))
		cmd_spi(av, ac);
	else if (!strcmp(av[0], "fb"))
		cmd_fb(av, ac);
	else if (!strcmp(av[0], "kmsg"))
		cmd_kmsg(av, ac);
	else if (!strcmp(av[0], "blk"))
		blk_do(av, ac);
	else if (!strcmp(av[0], "recv"))
		cmd_recv(av, ac);
	else if (!strcmp(av[0], "sink"))
		cmd_sink(av, ac);
	else if (!strcmp(av[0], "blkwr"))
		cmd_blkwr(av, ac);
	else if (!strcmp(av[0], "grep"))
		cmd_grep(av, ac);
	else if (!strcmp(av[0], "write"))
		cmd_write(av, ac);
	else if (!strcmp(av[0], "entropy"))
		cmd_entropy(av, ac);
	else if (!strcmp(av[0], "qnx6"))
		cmd_qnx6(av, ac);
	else if (!strcmp(av[0], "rootfs"))
		cmd_rootfs(av, ac);
	else if (!strcmp(av[0], "run"))
		cmd_run(av, ac);
	else if (!strcmp(av[0], "reboot"))
		cmd_reboot();
	else
		line("%s: no such command ('help')", av[0]);
}

/* ------------------------------------------------------------- tty / loop */

static void banner(void)
{
	int fd = open("/proc/version", O_RDONLY);
	char b[200];
	int n = fd >= 0 ? read(fd, b, sizeof(b) - 1) : -1;

	line("");
	line("=== PlayBook Linux bring-up console (no auto-reboot) ===");
	if (n > 0) {
		b[n] = 0;
		if (b[n - 1] == '\n')
			b[n - 1] = 0;
		line("%s", b);
	}
	if (fd >= 0)
		close(fd);
	line("type 'help'; the unit stays up until 'reboot'");
}

static int open_tty(void)
{
	struct termios t;
	int fd, tries;

	/* /dev/ttyGS0 exists only once the ACM function is bound; retry a
	 * little so a console started early does not give up. */
	for (tries = 0; tries < 200; tries++) {
		fd = open("/dev/ttyGS0", O_RDWR);
		if (fd >= 0 || errno != ENOENT)
			break;
		usleep(100000);
	}
	if (fd < 0) {
		rlog("PB: console: cannot open /dev/ttyGS0");
		return -1;
	}
	/* The device side is made raw: no kernel echo (we echo), no ICANON
	 * (we assemble lines), no CR/NL translation (we emit CRLF). */
	if (!tcgetattr(fd, &t)) {
		cfmakeraw(&t);
		cfsetispeed(&t, B115200);
		cfsetospeed(&t, B115200);
		tcsetattr(fd, TCSANOW, &t);
	}
	return fd;
}

int main(int argc, char **argv)
{
	char l[256];
	int ll = 0;
	int kfd;
	unsigned char c;

	if (argc > 1)
		rb_fd = atoi(argv[1]);

	kfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (kfd >= 0) {
		void *m = mmap(NULL, 0x8000, PROT_READ | PROT_WRITE, MAP_SHARED,
			       kfd, RING_PA);
		if (m != MAP_FAILED)
			ring = m;
		close(kfd);
	}

	signal(SIGCHLD, SIG_DFL);
	signal(SIGPIPE, SIG_IGN);

	for (;;) {
		long long last_io;
		int reprompted = 0;
		int started = 0;

		cfd = open_tty();
		if (cfd < 0)
			return 1;
		last_io = now_ms();
		/*
		 * Say NOTHING at open.  gs_open() does not wait for the host, and
		 * anything written before the PC puts ttyACM0 into raw/-echo is
		 * echoed back to us by the *host* line discipline -- we then read
		 * our own banner as if it were typed input (observed: the first
		 * session started with garbage in the line buffer).  Staying
		 * silent until the host speaks makes that impossible: the first
		 * byte we receive is a real keystroke.
		 */
		rlog("PB: console: /dev/ttyGS0 open (silent until host bytes)");

		ll = 0;
		for (;;) {
			struct pollfd pf = { .fd = cfd, .events = POLLIN };
			int r = poll(&pf, 1, 500);

			if (kick_on && now_ms() - last_kick_ms >= KICK_MS) {
				last_kick_ms = now_ms();
				mem_op(0, 0, M_KICK, 0);	/* prints its line */
				ow("pb> ");
				if (ll) {
					l[ll] = 0;
					ow(l);
				}
				last_io = now_ms();
			}
			if (r == 0 && started && !reprompted &&
			    now_ms() - last_io >= 5000) {
				ow("\r\n[console idle] pb> ");
				last_io = now_ms();
				reprompted = 1;
			}
			if (r <= 0)
				continue;
			if (read(cfd, &c, 1) != 1) {
				/* host closed the port (or the gadget reset):
				 * reopen and wait for the next session */
				rlog("PB: console: ttyGS0 read ended, reopening");
				close(cfd);
				cfd = -1;
				break;
			}
			last_io = now_ms();
			reprompted = 0;
			if (!started) {
				started = 1;
				banner();
			}
			if (c == '\r' || c == '\n') {
				if (swallow_nl) {		/* the \n after ^C */
					swallow_nl = 0;
					continue;
				}
				ow("\r\n");
				l[ll] = 0;
				if (ll)
					run_line(l);
				ll = 0;
				ow("pb> ");
				last_io = now_ms();
			} else if (c == 0x7f || c == 0x08) {
				if (ll) {
					ll--;
					ow("\b \b");
				}
			} else if (c == 0x03) {		/* ^C */
				ll = 0;
				swallow_nl = 1;
				ow("^C\r\npb> ");
			} else if (c == 0x15) {		/* ^U */
				while (ll) {
					ll--;
					ow("\b \b");
				}
			} else if (c >= 0x20 && c < 0x7f) {
				if (ll < (int)sizeof(l) - 1) {
					l[ll++] = (char)c;
					write(cfd, &c, 1);	/* echo */
				}
			}
		}
	}
}
