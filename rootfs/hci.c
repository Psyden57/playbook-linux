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
 * It writes exactly one 4-byte command and nothing else.
 *
 * usage: pbhci [device] [baud]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>

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

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS1";
	unsigned long baud = argc > 2 ? strtoul(argv[2], NULL, 0) : 115200;
	unsigned char cmd[4] = { 0x01, 0x03, 0x0c, 0x00 };	/* HCI_Reset */
	unsigned char buf[128];
	struct termios t;
	fd_set rfds;
	struct timeval tv;
	int fd, i, k, n, got = 0;

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "pbhci: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "pbhci: tcgetattr: %s\n", strerror(errno));
		return 1;
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
		return 1;
	}
	tcflush(fd, TCIOFLUSH);

	if (write(fd, cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd)) {
		fprintf(stderr, "pbhci: write: %s\n", strerror(errno));
		return 1;
	}
	printf("pbhci: %s at %lu 8N1, sent HCI_Reset (01 03 0c 00)\n", dev, baud);

	for (i = 0; i < 40 && !got; i++) {
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
		printf("pbhci: nothing back in 4 s (chip not running, or BT not enabled)\n");
	close(fd);
	return got ? 0 : 2;
}
