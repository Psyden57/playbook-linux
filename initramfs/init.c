/*
 * PlayBook OMAP4430 bring-up — PID 1 for the test initramfs.
 *
 * Two jobs:
 *  1. prove the kernel keeps running (no rootfs, no media writes), and
 *  2. poke/probe hardware from *userspace* via /dev/mem, in a forked child so
 *     an L4-firewall abort costs a SIGBUS instead of a dead board.
 *
 * The probe table is report 111's R3 (is DSS/DISPC/DSI reachable at all?) plus
 * report 110's M3 (does writing CONTROL_USBOTGHS_CONTROL make MUSB see a
 * session?).  Every line goes to /dev/kmsg with a "<0>" level, so it is
 * visible at any loglevel.
 */
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <dirent.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <linux/loop.h>
#include <linux/spi/spidev.h>

static int kfd = -1;

static void out(const char *s)
{
	if (kfd >= 0)
		write(kfd, s, strlen(s));
}

static void kf(const char *fmt, ...)
{
	char b[256];
	int n;
	va_list ap;

	if (kfd < 0)
		return;
	b[0] = '<'; b[1] = '0'; b[2] = '>';
	va_start(ap, fmt);
	n = vsnprintf(b + 3, sizeof(b) - 4, fmt, ap);
	va_end(ap);
	if (n > 0)
		write(kfd, b, n + 3);
}

static void dump(const char *path)
{
	char buf[900];
	char w[906];
	int fd, n;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		kf("PLAYBOOK: open failed: %s\n", path);
		return;
	}
	kf("PLAYBOOK: ===== %s =====\n", path);
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		memcpy(w, "<0>", 3);
		memcpy(w + 3, buf, n);
		w[3 + n] = '\n';
		write(kfd, w, n + 4);
	}
	close(fd);
}

static void mount_it(const char *src, const char *tgt, const char *type)
{
	if (mount(src, tgt, type, 0, NULL) && strcmp(type, "devtmpfs"))
		out("<0>PLAYBOOK: mount failed, continuing\n");
}

/* ---- userspace DRAM-ring log -------------------------------------------
 * The kernel's printk from the probe window was getting lost (only the first
 * four peeks ever reached the ring, while PID 1 demonstrably continued), so
 * the probe writes its own lines straight into the DEBUG_LL ring page over
 * /dev/mem, with the same counter/index protocol the kernel's ring_put uses.
 * This is independent of printk and cannot be dropped.
 */
#define RING_PA		0x88000000UL
#define RING_SIZE	0x7f00

static volatile unsigned char *ring;

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

/* ---- /dev/mem access ---------------------------------------------------- */

static int memfd = -1;

static void *map_at(unsigned long pa)
{
	void *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED,
		       memfd, pa & ~0xfffUL);

	return p == MAP_FAILED ? NULL : p;
}

static void peek(const char *what, unsigned long pa)
{
	void *p = map_at(pa);

	char b[96];

	if (!p) {
		snprintf(b, sizeof(b), "PB: PEEK %s %08lx -> map failed", what, pa);
		rlog(b);
		return;
	}
	snprintf(b, sizeof(b), "PB: PEEK %-22s %08lx = %08x", what, pa,
		 *(volatile unsigned int *)((char *)p + (pa & 0xfff)));
	rlog(b);
	munmap(p, 0x1000);
}

static int poke(unsigned long pa, unsigned int v)
{
	void *p = map_at(pa);

	if (!p)
		return -1;
	*(volatile unsigned int *)((char *)p + (pa & 0xfff)) = v;
	munmap(p, 0x1000);
	return 0;
}

/*
 * Two batches.  Batch 1 is everything except the DSI block; batch 2 is the
 * DSI1 reads, which took the whole system down in the first run (DSS/DISPC
 * read fine, DSI did not).  Doing DSI last means a repeat still leaves us the
 * USB answer.
 */
static void probe_safe(void)
{
	peek("DSS_REVISION",        0x48040000);
	peek("DISPC_REVISION",      0x48041000);
	peek("DISPC_SYSSTATUS",     0x48041014);
	peek("DISPC_CONTROL",       0x48041040);
	peek("CM_DSS_CLKSTCTRL",    0x4a009100);
	peek("CM_DSS_CLKCTRL",      0x4a009120);

	peek("CM_L3INIT_CLKSTCTRL", 0x4a009300);
	peek("L3INIT_USBOTGHS",     0x4a009358);
	peek("L3INIT_USBHOSTHS",    0x4a009360);
	peek("L3INIT_USBTLL",       0x4a009368);
	peek("DPLL_USB mode",       0x4a008180);

	peek("MUSB_DEVCTL before",  0x4a0ab060);
	peek("USBOTGHS_CONTROL",    0x4a00233c);
	if (poke(0x4a00233c, 0x15) == 0)
		rlog("PB: POKE USBOTGHS_CONTROL = 0x15 (AVALID|VBUSVALID|IDDIG)");
	usleep(20000);
	peek("USBOTGHS_CONTROL",    0x4a00233c);
	peek("MUSB_DEVCTL after",   0x4a0ab060);
	rlog("PB: PROBE1 DONE (display-safe + USB)");
}

static void probe_dsi(void)
{
	rlog("PB: PROBE2 start (DSI1)");
	peek("DSI1_REVISION",       0x48044000);
	peek("DSI1_SYSSTATUS",      0x48044014);
	peek("DSI1_COMPLEXIO_CFG1", 0x48044048);
	peek("DSI1_PHY_REVISION",   0x48044200);
	peek("DSI1_PLL_REVISION",   0x48044300);
	rlog("PB: PROBE2 DONE (DSI1 reachable)");
}

static void run_probe(void (*fn)(void))
{
	pid_t pid;
	int st;

	pid = fork();
	if (pid == 0) {
		memfd = open("/dev/mem", O_RDWR | O_SYNC);
		if (memfd < 0)
			_exit(1);
		fn();
		_exit(0);
	}
	if (pid > 0) {
		waitpid(pid, &st, 0);
		char b[80];

		if (WIFSIGNALED(st))
			snprintf(b, sizeof(b), "PB: probe child died on signal %d",
				 WTERMSIG(st));
		else
			snprintf(b, sizeof(b), "PB: probe child exit %d",
				 WEXITSTATUS(st));
		rlog(b);
	}
}

/* ---- configfs USB gadget ----------------------------------------------- */

static int wr(const char *path, const char *val)
{
	int fd = open(path, O_WRONLY);
	int n;

	if (fd < 0)
		return -1;
	n = write(fd, val, strlen(val));
	close(fd);
	return n < 0 ? -1 : 0;
}

static void gadget_setup(void)
{
	char b[128], udc[64] = "";
	DIR *d;
	struct dirent *de;

	mkdir("/config", 0755);
	if (mount("configfs", "/config", "configfs", 0, NULL))
		rlog("PB: configfs mount failed");

	/* No shell in the initramfs: configfs objects are created by mkdir. */
	mkdir("/config/usb_gadget", 0777);
	mkdir("/config/usb_gadget/g1", 0777);
	mkdir("/config/usb_gadget/g1/strings", 0777);
	mkdir("/config/usb_gadget/g1/strings/0x409", 0777);
	mkdir("/config/usb_gadget/g1/configs", 0777);
	mkdir("/config/usb_gadget/g1/configs/c.1", 0777);
	mkdir("/config/usb_gadget/g1/configs/c.1/strings", 0777);
	mkdir("/config/usb_gadget/g1/configs/c.1/strings/0x409", 0777);
	mkdir("/config/usb_gadget/g1/functions", 0777);

	wr("/config/usb_gadget/g1/idVendor", "0x0fca");
	wr("/config/usb_gadget/g1/idProduct", "0x8010");
	wr("/config/usb_gadget/g1/strings/0x409/serialnumber", "123456");
	wr("/config/usb_gadget/g1/strings/0x409/manufacturer", "Research In Motion");
	wr("/config/usb_gadget/g1/strings/0x409/product", "PlayBook (Linux bring-up)");
	wr("/config/usb_gadget/g1/configs/c.1/strings/0x409/configuration", "acm");

	mkdir("/config/usb_gadget/g1/functions/acm.usb0", 0777);
	symlink("/config/usb_gadget/g1/functions/acm.usb0",
		"/config/usb_gadget/g1/configs/c.1/acm.usb0");

	/* first (only) UDC */
	d = opendir("/sys/class/udc");
	if (d) {
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.')
				continue;
			snprintf(udc, sizeof(udc), "%s", de->d_name);
			break;
		}
		closedir(d);
	}
	if (!udc[0]) {
		rlog("PB: no UDC registered (musb did not probe?)");
		return;
	}
	snprintf(b, sizeof(b), "PB: UDC = %s, binding gadget", udc);
	rlog(b);
	if (wr("/config/usb_gadget/g1/UDC", udc)) {
		rlog("PB: UDC bind FAILED");
		return;
	}
	rlog("PB: gadget bound (idVendor 0fca idProduct 8010)");

	/* state, for the record and for the host-side match */
	snprintf(b, sizeof(b), "/sys/class/udc/%s/state", udc);
	dump(b);
	snprintf(b, sizeof(b), "/sys/class/udc/%s/function", udc);
	dump(b);
}

/* ---- interactive console on the ACM gadget ------------------------------
 * /console is a separate static binary that talks to /dev/ttyGS0 (the device
 * side of the very same gadget the host sees as /dev/ttyACM0).  It gets the
 * write end of a pipe as argv[1]: any byte on it means "reboot now", so the
 * decision to reboot always stays with PID 1.
 */

static pid_t console_start(int rb_fd)
{
	pid_t pid = fork();

	if (pid == 0) {
		char arg[16];

		snprintf(arg, sizeof(arg), "%d", rb_fd);
		execl("/console", "console", arg, (char *)NULL);
		rlog("PB: console: exec /console failed");
		_exit(127);
	}
	return pid;
}

static void shutdown_now(const char *why)
{
	char b[160];

	snprintf(b, sizeof(b), "PB: reboot now (%s)", why);
	rlog(b);
	out("<0>PLAYBOOK: rebooting now\n");
	sync();
	reboot(RB_AUTOBOOT);		/* returns only on failure */
	rlog("PB: reboot(2) FAILED");
	for (;;)
		pause();
}

/* ---- panel TCON init ----------------------------------------------------
 * The reference sends the 9-word Hitachi table as part of its own display
 * bring-up: winch_lcdctl 0x1c580, called at 0x193c4, i.e. right after the
 * TC358764 has been programmed (0x1938c) and the HS clock started (0x19398).
 * Until session 15 this existed only as the console command `spi tconbe`, so
 * the panel was programmed by hand, seconds late, or not at all.
 *
 * Framing is the reference's (report 130 §5.2): 450 kHz, mode 0, MSB first,
 * one 16-bit word per transfer, 100 ms after word 0, word 2 and the last.
 * The console's spi_do() has the same code; keep them in step.
 */
static const unsigned short tcon_hitachi[] = {
	0x0029, 0x0025, 0x0840, 0x00AD, 0x0430, 0x385F, 0x3CA4, 0x3409, 0x4041
};

static void panel_tcon_init(void)
{
	unsigned int mode = 0, bits = 8, speed = 450000;
	struct spi_ioc_transfer tr;
	unsigned char b[2];
	int fd, i, ret = 0, nw;

	fd = open("/dev/spidev3.0", O_RDWR);
	if (fd < 0) {
		rlog("PB: tcon: cannot open /dev/spidev3.0");
		return;
	}
	if (ioctl(fd, SPI_IOC_WR_MODE, &mode) ||
	    ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) ||
	    ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed))
		rlog("PB: tcon: spi setup failed");

	nw = (int)(sizeof(tcon_hitachi) / sizeof(tcon_hitachi[0]));
	for (i = 0; i < nw; i++) {
		unsigned short w = tcon_hitachi[i];

		b[0] = w >> 8;		/* the reference's byte order */
		b[1] = w & 0xff;
		memset(&tr, 0, sizeof(tr));
		tr.tx_buf = (unsigned long)b;
		tr.len = 2;
		tr.speed_hz = 450000;
		tr.bits_per_word = 8;
		ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
		if (ret < 0) {
			rlog("PB: tcon: word write failed");
			break;
		}
		if (i == 0 || i == 2 || i == nw - 1)
			usleep(100000);
	}
	close(fd);

	if (ret >= 0)
		rlog("PB: tcon: Hitachi table sent (high byte first)");
}

/* ---- region-1 Linux root (read-only loop over the eMMC) -----------------
 * The first Linux root lives in media region 1 (raw 0x28000000) at region-1
 * offset 16 MiB, i.e. raw 0x29000000, placed there by
 * tools/pre_os_ifs_flash.py over the pre-OS 0xF7 loop (region 1 is inside the
 * loader's 0x1B allow-list, so no MCT edit).  The image is ext4 with no
 * journal; it is mounted through a LO_FLAGS_READ_ONLY loop with MS_RDONLY and
 * ext4 "noload", so the mount cannot write (see the read-only-mount decision).
 * Its /init is run forked and bounded: reaching it proves the image is on the
 * eMMC, ext4 reads it and code executes from it.
 */
#define ROOTFS_RAW_OFF 0x29000000ULL

static void rootfs_try(void)
{
	struct loop_info64 li;
	char loopdev[32], b[160];
	int ctl, lfd, bfd, n, st = 0;
	pid_t pid;

	ctl = open("/dev/loop-control", O_RDWR);
	if (ctl < 0) {
		rlog("PB: rootfs: no /dev/loop-control");
		return;
	}
	n = ioctl(ctl, LOOP_CTL_GET_FREE);
	close(ctl);
	if (n < 0) {
		rlog("PB: rootfs: no free loop device");
		return;
	}
	snprintf(loopdev, sizeof(loopdev), "/dev/loop%d", n);
	bfd = open("/dev/mmcblk1", O_RDONLY);
	if (bfd < 0) {
		snprintf(b, sizeof(b), "PB: rootfs: /dev/mmcblk1: %s", strerror(errno));
		rlog(b);
		return;
	}
	lfd = open(loopdev, O_RDWR);
	if (lfd < 0) {
		rlog("PB: rootfs: loop device open failed");
		close(bfd);
		return;
	}
	if (ioctl(lfd, LOOP_SET_FD, bfd) < 0) {
		snprintf(b, sizeof(b), "PB: rootfs: LOOP_SET_FD: %s", strerror(errno));
		rlog(b);
		goto out;
	}
	memset(&li, 0, sizeof(li));
	li.lo_offset = (typeof(li.lo_offset))ROOTFS_RAW_OFF;
	li.lo_flags = LO_FLAGS_READ_ONLY;
	if (ioctl(lfd, LOOP_SET_STATUS64, &li) < 0) {
		snprintf(b, sizeof(b), "PB: rootfs: LOOP_SET_STATUS64: %s", strerror(errno));
		rlog(b);
		ioctl(lfd, LOOP_CLR_FD, 0);
		goto out;
	}
	mkdir("/rootfs", 0755);
	if (mount(loopdev, "/rootfs", "ext4", MS_RDONLY, "noload") < 0) {
		snprintf(b, sizeof(b), "PB: rootfs: mount: %s", strerror(errno));
		rlog(b);
		ioctl(lfd, LOOP_CLR_FD, 0);
		goto out;
	}
	snprintf(b, sizeof(b),
		 "PB: rootfs: ext4 at 0x%llx mounted read-only on /rootfs",
		 (unsigned long long)ROOTFS_RAW_OFF);
	rlog(b);
	out("<0>PLAYBOOK: region-1 root mounted read-only; running its /init\n");

	pid = fork();
	if (pid == 0) {
		alarm(5);
		execl("/rootfs/init", "init", (char *)NULL);
		_exit(127);
	}
	if (pid > 0) {
		waitpid(pid, &st, 0);
		snprintf(b, sizeof(b), "PB: rootfs: /rootfs/init %s %d",
			 WIFSIGNALED(st) ? "killed by signal" : "exit",
			 WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
		rlog(b);
	}
out:
	close(lfd);
	close(bfd);
}

int main(void)
{
	char b[128];
	int rb_pipe[2] = { -1, -1 };
	pid_t cpid = -1;

	mkdir("/proc", 0555);
	mkdir("/dev", 0755);
	mkdir("/sys", 0555);
	mount_it("devtmpfs", "/dev", "devtmpfs");
	mount_it("proc", "/proc", "proc");
	mount_it("sysfs", "/sys", "sysfs");

	kfd = open("/dev/kmsg", O_WRONLY);
	out("<0>PLAYBOOK: /init running as PID 1, no rootfs, no media access\n");

	dump("/proc/version");
	dump("/proc/interrupts");

	memfd = open("/dev/mem", O_RDWR | O_SYNC);
	if (memfd >= 0) {
		void *m = mmap(NULL, 0x8000, PROT_READ | PROT_WRITE, MAP_SHARED,
			       memfd, RING_PA);
		if (m != MAP_FAILED)
			ring = m;
	}
	rlog("PB: ---- userspace probe begin (ring logger, printk bypassed) ----");
	gadget_setup();

	/*
	 * The panel's own TCON is configured over MCSPI4, as the reference does
	 * at the end of its display bring-up (0x193c4).  By the time /init
	 * runs, omapdrm has already programmed the DSI and the TC358764, so
	 * this lands in the same order as winch_lcdctl.
	 */
	panel_tcon_init();

	/*
	 * The DSS debugfs dumps settle the clock question that the boot log's
	 * "clk rate mismatch: 48000000 != 128000000" raises: what the DISPC
	 * and LCD clock sources actually are and what rates the driver
	 * computed.  Cheap, read-only, and it must happen while the display is
	 * live.
	 */
	mkdir("/sys/kernel/debug", 0755);
	if (mount("debugfs", "/sys/kernel/debug", "debugfs", 0, NULL) == 0) {
		dump("/sys/kernel/debug/omapdss/clk");
		dump("/sys/kernel/debug/omapdss/dispc");
	} else {
		rlog("PB: debugfs mount failed");
	}

	/*
	 * Console first, probes second: the host polls for 0fca:8010 and can be
	 * talking before the register probes have run.  The gadget is bound by
	 * now, so /dev/ttyGS0 exists.
	 */
	if (pipe(rb_pipe) == 0) {
		cpid = console_start(rb_pipe[1]);
		close(rb_pipe[1]);
		if (cpid > 0) {
			snprintf(b, sizeof(b), "PB: console started as pid %d",
				 (int)cpid);
			rlog(b);
		} else {
			rlog("PB: console fork FAILED");
		}
	} else {
		rlog("PB: console pipe() FAILED");
	}

	/*
	 * With the gadget up (/dev/ttyGS0 exists) try the region-1 Linux root.
	 * A missing image is not an error: the probe boot still comes up and
	 * the console reports why the mount failed.
	 */
	rootfs_try();

	run_probe(probe_safe);
	run_probe(probe_dsi);

	/*
	 * No bounded lifetime.  PID 1 used to reboot the unit after 180 s
	 * because, before the ACM console existed, a warm reboot was the only
	 * way to read the log back out of the DRAM rings — and every one of
	 * those reboots spent a BOOTCTR_FCTRY unit (trap 33).  With the
	 * console working, a reboot is a deliberate act: the console's
	 * `reboot` command, or the power button if the unit is wedged.  PID 1
	 * now only reboots when the console asks it to.
	 */
	snprintf(b, sizeof(b), "PB: console up (no auto-reboot)");
	rlog(b);
	out("<0>PLAYBOOK: console on /dev/ttyGS0, no auto-reboot\n");

	for (;;) {
		struct pollfd pf = { .fd = rb_pipe[0], .events = POLLIN };

		if (rb_pipe[0] >= 0 && poll(&pf, 1, 500) > 0) {
			char c;

			if (pf.revents & POLLIN) {
				if (read(rb_pipe[0], &c, 1) == 1)
					shutdown_now("console request");
			} else if (pf.revents & (POLLHUP | POLLERR)) {
				/* the console is gone; do NOT read that as a reboot
				 * request */
				rlog("PB: console pipe closed (console gone)");
				rb_pipe[0] = -1;
			}
		}
		if (cpid > 0) {
			int st;

			if (waitpid(cpid, &st, WNOHANG) == cpid) {
				snprintf(b, sizeof(b),
					 "PB: console exited (status %d)",
					 WIFEXITED(st) ? WEXITSTATUS(st) : -1);
				rlog(b);
				cpid = -1;
			}
		}
	}
}
