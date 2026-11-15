/* btdump — send one HCI command and hexdump EVERYTHING that comes back.
 *
 * No event matching, no interpretation: if the chip answers, this shows it.
 * Written after an inquiry appeared "silent" through a matching parser, to
 * separate "the chip said nothing" from "my parser threw it away".
 *
 * usage: btdump <opcode-hex> [device] [ms] [param-hex]
 *   e.g. btdump 0x0401 /dev/ttyS1 4000 338b9e3000
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

int main(int argc, char **argv)
{
	unsigned short op;
	const char *dev = argc > 2 ? argv[2] : "/dev/ttyS1";
	int ms = argc > 3 ? atoi(argv[3]) : 3000;
	struct termios t;
	unsigned char pkt[300], buf[2048], in[4096];
	int plen = 0, fd, i, in_len = 0, total = 0;
	long end;
	unsigned char param[128];

	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc < 2) {
		fprintf(stderr, "usage: btdump <op-hex> [dev] [ms] [param-hex]\n");
		return 1;
	}
	op = strtoul(argv[1], NULL, 0);
	if (argc > 4) {
		const char *h = argv[4];
		int n = strlen(h);

		for (i = 0; i + 1 < n && plen < (int)sizeof(param); i += 2) {
			char b[3] = { h[i], h[i + 1], 0 };

			param[plen++] = strtoul(b, NULL, 16);
		}
	}

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "btdump: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	tcgetattr(fd, &t);
	cfmakeraw(&t);
	cfsetispeed(&t, B115200);
	cfsetospeed(&t, B115200);
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~CRTSCTS;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);
	tcflush(fd, TCIOFLUSH);

	pkt[0] = 0x01;
	pkt[1] = op & 0xff;
	pkt[2] = op >> 8;
	pkt[3] = plen;
	memcpy(pkt + 4, param, plen);
	printf("btdump: -> op 0x%04x plen %d:", op, plen);
	for (i = 0; i < 4 + plen; i++)
		printf(" %02x", pkt[i]);
	printf("\n");

	if (write(fd, pkt, 4 + plen) != 4 + plen) {
		printf("btdump: write: %s\n", strerror(errno));
		return 1;
	}
	{
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		end = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L + ms;
	}
	while (1) {
		fd_set rf;
		struct timeval tv;
		struct timespec ts;
		long now;
		int r;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		now = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
		if (now >= end)
			break;
		FD_ZERO(&rf);
		FD_SET(fd, &rf);
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0)
			continue;
		r = read(fd, in, sizeof(in));
		if (r <= 0)
			continue;
		memcpy(buf + in_len, in, r);
		in_len += r;
		printf("btdump: <- %d byte(s):", r);
		for (i = 0; i < r; i++)
			printf(" %02x", in[i]);
		printf("\n");
		total += r;
	}
	printf("btdump: %d byte(s) in %d ms\n", total, ms);
	close(fd);
	return total ? 0 : 2;
}
