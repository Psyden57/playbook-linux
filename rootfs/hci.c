/* pbhci — ask the WL1283's Bluetooth core whether it is alive.
 *
 * The combo's BT core is wired to OMAP UART2 (0x4806C000, /dev/ttyS1 once the
 * board DTS enables it).  After power-up the BT core expects HCI commands at
 * 115200 8N1; a single HCI_Reset (01 03 0C 00) must be answered with
 * "04 0E 04 01 03 0C 00" (Command Complete) if the chip is powered and its
 * digital core is running.
 *
 * That is the discriminator the SDIO side cannot give us on this bench: an
 * answer here means the chip's supplies and POR are fine and the problem is in
 * the SDIO path; silence means the chip is not coming up at all.
 *
 * THE ENABLE MATTERS.  `BT_EN` is gpio 55 = GPIO2_23 (pad 0x4A10007C bits
 * 31:16); the reference drives it low -> 50 ms -> high in `btp-btmgr` before it
 * talks HCI.  The session-29 probe did NOT drive it, so its "silence" was never
 * a measurement.  This tool pulses it (the pad is already mux 3 on our board)
 * and prints the pad / GPIO2 registers as evidence.
 *
 * usage: pbhci [device] [baud] [--no-pulse] [--tries N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <dirent.h>
#include <sys/select.h>
#include <sys/mman.h>

#define PAD_BT_EN	0x4A10007C
#define PAD_UART2_RX	0x4A10011C
#define PAD_UART2_TX	0x4A10011E
#define UART2_PA	0x4806C000
#define UART2_LSR	0x14
#define GPIO2_PA	0x48055000
#define GPIO2_OE	0x134
#define GPIO2_DATAIN	0x138
#define GPIO2_DATAOUT	0x13C
#define BT_EN_LINE	23

static unsigned char *gpio2, *padreg, *uart2, *pad2;

static int map_regs(void)
{
	long page = sysconf(_SC_PAGESIZE);
	void *m;

	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "pbhci: /dev/mem: %s\n", strerror(errno));
		return -1;
	}
	m = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 GPIO2_PA & ~(page - 1));
	if (m == MAP_FAILED) {
		fprintf(stderr, "pbhci: mmap GPIO2: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	gpio2 = (unsigned char *)m + (GPIO2_PA & (page - 1));
	m = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 PAD_BT_EN & ~(page - 1));
	if (m == MAP_FAILED) {
		fprintf(stderr, "pbhci: mmap pad: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	padreg = (unsigned char *)m + (PAD_BT_EN & (page - 1));
	/* UART2 and the uart2 pads share the 0x4A100000 / 0x4806C000 pages
	 * with the mappings above on this SoC, so only map what is missing. */
	if ((PAD_UART2_RX & ~(page - 1)) == (PAD_BT_EN & ~(page - 1)))
		pad2 = padreg + (PAD_UART2_RX - PAD_BT_EN);
	m = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		 UART2_PA & ~(page - 1));
	if (m == MAP_FAILED) {
		fprintf(stderr, "pbhci: mmap UART2: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	uart2 = (unsigned char *)m + (UART2_PA & (page - 1));
	close(fd);
	return 0;
}

#define R32(p)		(*(volatile unsigned int *)(p))
#define GPIO2_R(off)	R32(gpio2 + (off))

/* The GPIO2 controller's sysfs base moves with probe order (the boards are
 * registered dynamically), so resolve it from the device path instead of
 * assuming 512. */
static int gpio2_base(void)
{
	DIR *d = opendir("/sys/class/gpio");
	struct dirent *de;
	int base = -1;
	char path[320], link[320];

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		ssize_t n;
		if (strncmp(de->d_name, "gpiochip", 8))
			continue;
		snprintf(path, sizeof(path), "/sys/class/gpio/%s/device", de->d_name);
		n = readlink(path, link, sizeof(link) - 1);
		if (n <= 0)
			continue;
		link[n] = 0;
		if (strstr(link, "48055000.gpio")) {
			FILE *f;
			snprintf(path, sizeof(path), "/sys/class/gpio/%s/base", de->d_name);
			f = fopen(path, "r");
			if (f) {
				if (fscanf(f, "%d", &base) != 1)
					base = -1;
				fclose(f);
			}
			break;
		}
	}
	closedir(d);
	return base;
}

static void sysfs_write(int gpio, const char *attr, const char *val)
{
	char path[128];
	int fd;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/%s", gpio, attr);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return;
	(void)write(fd, val, strlen(val));
	close(fd);
}

static int gpio_export(int gpio)
{
	char path[128];
	int fd;
	snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d", gpio);
	if (access(path, F_OK) == 0)
		return 0;
	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (fd < 0)
		return -1;
	{
		char buf[16];
		int n = snprintf(buf, sizeof(buf), "%d", gpio);
		(void)write(fd, buf, n);
	}
	close(fd);
	return access(path, F_OK);
}

static void pulse_bt_en(void)
{
	int gpio, base;

	if (gpio2) {
		unsigned int pad = R32(padreg);
		printf("pbhci: pad 0x%08x = 0x%08x (BT_EN mux %u), "
		       "GPIO2_OE=0x%08x DATAOUT=0x%08x DATAIN=0x%08x\n",
		       PAD_BT_EN, pad, (pad >> 16) & 7,
		       GPIO2_R(GPIO2_OE), GPIO2_R(GPIO2_DATAOUT),
		       GPIO2_R(GPIO2_DATAIN));
		if (((pad >> 16) & 7) != 3)
			fprintf(stderr, "pbhci: WARNING pad high half is not mux 3 "
				"(GPIO); BT_EN cannot be driven\n");
	}

	base = gpio2_base();
	if (base < 0) {
		fprintf(stderr, "pbhci: cannot resolve the GPIO2 sysfs base\n");
		return;
	}
	gpio = base + BT_EN_LINE;
	if (gpio_export(gpio) < 0) {
		fprintf(stderr, "pbhci: export gpio %d failed\n", gpio);
		return;
	}
	sysfs_write(gpio, "direction", "out");
	sysfs_write(gpio, "value", "0");
	printf("pbhci: BT_EN (gpio %d) low, 50 ms\n", gpio);
	usleep(50000);
	sysfs_write(gpio, "value", "1");
	printf("pbhci: BT_EN high, 100 ms settle\n");
	usleep(100000);

	if (gpio2)
		printf("pbhci: after pulse GPIO2_OE=0x%08x DATAOUT=0x%08x "
		       "DATAIN=0x%08x\n",
		       GPIO2_R(GPIO2_OE), GPIO2_R(GPIO2_DATAOUT),
		       GPIO2_R(GPIO2_DATAIN));
}

static speed_t baud_const(unsigned long b)
{
	switch (b) {
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
	case 230400: return B230400;
	case 460800: return B460800;
	case 921600: return B921600;
	default: return B115200;
	}
}

static int hci_reset(const char *dev, unsigned long baud)
{
	unsigned char cmd[4] = { 0x01, 0x03, 0x0c, 0x00 };	/* HCI_Reset */
	unsigned char buf[128];
	struct termios t;
	fd_set rfds;
	struct timeval tv;
	int fd, i, k, n, got = 0;

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "pbhci: open %s: %s\n", dev, strerror(errno));
		return -1;
	}
	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "pbhci: tcgetattr: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	cfmakeraw(&t);
	cfsetispeed(&t, baud_const(baud));
	cfsetospeed(&t, baud_const(baud));
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~CRTSCTS;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSANOW, &t) < 0) {
		fprintf(stderr, "pbhci: tcsetattr: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	tcflush(fd, TCIOFLUSH);

	if (write(fd, cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd)) {
		fprintf(stderr, "pbhci: write: %s\n", strerror(errno));
		close(fd);
		return -1;
	}
	printf("pbhci: %s at %lu 8N1, sent HCI_Reset (01 03 0c 00)\n", dev, baud);
	if (uart2) {
		usleep(10000);
		printf("pbhci: UART2 LSR=0x%02x (THRE=%d TEMT=%d DR=%d)",
		       R32(uart2 + UART2_LSR),
		       !!(R32(uart2 + UART2_LSR) & 0x20),
		       !!(R32(uart2 + UART2_LSR) & 0x40),
		       !!(R32(uart2 + UART2_LSR) & 0x01));
		if (pad2)
			printf(", pads rx=0x%08x tx=0x%08x",
			       R32(pad2), R32(pad2 + (PAD_UART2_TX - PAD_UART2_RX)));
		printf("\n");
	}

	for (i = 0; i < 20 && !got; i++) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
			continue;
		n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			continue;
		printf("pbhci: %d byte(s):", n);
		for (k = 0; k < n && k < 32; k++)
			printf(" %02x", buf[k]);
		printf("\n");
		got = 1;
	}
	if (!got)
		printf("pbhci: nothing back in 2 s\n");
	close(fd);
	return got ? 0 : 2;
}

int main(int argc, char **argv)
{
	const char *dev = "/dev/ttyS1";
	unsigned long baud = 115200;
	int pulse = 1, tries = 3, i, rc = 2, argi, pos = 0;

	for (argi = 1; argi < argc; argi++) {
		if (!strcmp(argv[argi], "--no-pulse")) {
			pulse = 0;
		} else if (!strcmp(argv[argi], "--tries") && argi + 1 < argc) {
			tries = atoi(argv[++argi]);
		} else if (pos == 0) {
			dev = argv[argi];
			pos++;
		} else {
			baud = strtoul(argv[argi], NULL, 0);
			pos++;
		}
	}
	if (tries < 1)
		tries = 1;

	if (map_regs() < 0)
		pulse = 0;

	for (i = 0; i < tries; i++) {
		if (pulse) {
			printf("pbhci: --- attempt %d/%d ---\n", i + 1, tries);
			pulse_bt_en();
		}
		rc = hci_reset(dev, baud);
		if (rc == 0)
			break;
		if (i + 1 < tries && pulse)
			usleep(200000);
	}
	if (rc == 0)
		printf("pbhci: REPLY - the BT core is powered and running\n");
	else
		printf("pbhci: silent after %d attempt(s)\n", tries);
	return rc;
}
