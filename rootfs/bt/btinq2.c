/* btinq2 — BR/EDR inquiry, built on the skeleton that is proven to answer.
 *
 * btgate.c's command path is measured working on this port (nine consecutive
 * commands answered in one session), so the inquiry rides on exactly that:
 * same open, same termios, same single-deadline wait.  An earlier standalone
 * attempt failed to get even HCI_Reset back while btgate succeeded moments
 * before and after it, so rather than chase that difference the working path is
 * reused verbatim.
 *
 * usage: btinq2 [device] [seconds] [baud]
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

static unsigned char in[1024];
static int in_len;

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static speed_t baud_const(unsigned long b)
{
	switch (b) {
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 230400: return B230400;
	case 460800: return B460800;
	case 921600: return B921600;
	default: return B115200;
	}
}

static int take_event(unsigned char *o)
{
	int pl, need, i;

	if (in_len < 3)
		return 0;
	if (in[0] != 0x04) {
		for (i = 1; i < in_len; i++)
			if (in[i] == 0x04)
				break;
		if (i >= in_len) {
			in_len = 0;
			return 0;
		}
		memmove(in, in + i, in_len - i);
		in_len -= i;
		if (in_len < 3)
			return 0;
	}
	pl = in[2];
	need = 3 + pl;
	if (in_len < need)
		return 0;
	memcpy(o, in, need);
	memmove(in, in + need, in_len - need);
	in_len -= need;
	return need;
}

/* Send one command; return the reply length or -1.  Same shape as btgate. */
static int cmd(int fd, unsigned short op, const unsigned char *p, int plen,
	       unsigned char *ev, int ms)
{
	unsigned char pkt[300];
	long end = 0;

	pkt[0] = 0x01;
	pkt[1] = op & 0xff;
	pkt[2] = op >> 8;
	pkt[3] = plen;
	if (plen)
		memcpy(pkt + 4, p, plen);
	tcflush(fd, TCIFLUSH);
	in_len = 0;
	if (write(fd, pkt, 4 + plen) != 4 + plen)
		return -1;

	{
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		end = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L + ms;
	}
	for (;;) {
		int e = take_event(ev);

		if (e > 0) {
			/* The two event layouts are NOT the same:
			 *   Command Complete 04 0e len num  op_lo op_hi status
			 *   Command Status   04 0f len stat num  op_lo op_hi
			 * so the opcode is at offset 4 for 0x0e and 5 for 0x0f.
			 * Reading ev[4..5] on a 0x0f event drops a good reply --
			 * exactly how a working inquiry looked "silent".
			 */
			if (ev[1] == 0x0e && (ev[4] | (ev[5] << 8)) == op)
				return e;
			if (ev[1] == 0x0f && e >= 7 &&
			    (ev[5] | (ev[6] << 8)) == op)
				return e;
			continue;
		}
		{
			fd_set rf;
			struct timeval tv;

			if (now_ms() >= end)
				return -1;
			FD_ZERO(&rf);
			FD_SET(fd, &rf);
			tv.tv_sec = 0;
			tv.tv_usec = 100000;
			if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
				int r = read(fd, in + in_len,
					     sizeof(in) - in_len);

				if (r > 0)
					in_len += r;
			}
		}
	}
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS1";
	int secs = argc > 2 ? atoi(argv[2]) : 10;
	unsigned long baud = argc > 3 ? strtoul(argv[3], NULL, 0) : 115200;
	struct termios t;
	unsigned char ev[600];
	int fd, n, found = 0;
	long end;
	int units = (secs * 100) / 128;

	setvbuf(stdout, NULL, _IOLBF, 0);
	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "btinq2: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	tcgetattr(fd, &t);
	cfmakeraw(&t);
	cfsetispeed(&t, baud_const(baud));
	cfsetospeed(&t, baud_const(baud));
	t.c_cflag |= CLOCAL | CREAD;
	t.c_cflag &= ~CRTSCTS;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &t);

	printf("btinq2: %s at %lu 8N1, %d s inquiry\n", dev, baud, secs);

	n = cmd(fd, 0x0c03, NULL, 0, ev, 2000);
	if (n < 0) {
		printf("btinq2: no HCI_Reset answer\n");
		return 1;
	}
	printf("btinq2: reset ok (st 0x%02x)\n", ev[6]);

	n = cmd(fd, 0x1009, NULL, 0, ev, 2000);
	if (n >= 13)
		printf("btinq2: local bdaddr %02x:%02x:%02x:%02x:%02x:%02x\n",
		       ev[11], ev[10], ev[9], ev[8], ev[7], ev[6]);

	n = cmd(fd, 0x1001, NULL, 0, ev, 2000);
	if (n >= 15)
		printf("btinq2: hci ver %u rev 0x%04x lmp ver %u subver 0x%04x "
		       "mfr 0x%04x\n", ev[7], ev[8] | (ev[9] << 8), ev[10],
		       ev[12] | (ev[13] << 8), ev[14] | (ev[15] << 8));

	/* Inquiry: GIAC LAP little-endian, length in 1.28 s units, unlimited. */
	{
		unsigned char p[5];


		if (units < 1)
			units = 1;
		if (units > 0x30)
			units = 0x30;
		p[0] = 0x33;
		p[1] = 0x8b;
		p[2] = 0x9e;
		p[3] = units;
		p[4] = 0x00;
		n = cmd(fd, 0x0401, p, 5, ev, 3000);
		if (n < 0) {
			printf("btinq2: no answer to HCI_Inquiry\n");
			return 1;
		}
		printf("btinq2: inquiry started, st 0x%02x, %d x 1.28 s\n",
		       ev[6], units);
	}

	end = now_ms() + (long)units * 1280 + 3000;
	in_len = 0;
	while (now_ms() < end) {
		int e = take_event(ev);

		if (e <= 0) {
			fd_set rf;
			struct timeval tv;

			FD_ZERO(&rf);
			FD_SET(fd, &rf);
			tv.tv_sec = 0;
			tv.tv_usec = 200000;
			if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
				int r = read(fd, in + in_len,
					     sizeof(in) - in_len);

				if (r > 0)
					in_len += r;
			}
			continue;
		}
		if (ev[1] == 0x02) {		/* Inquiry Result */
			int num = ev[3];
			int j;

			printf("btinq2: Inquiry Result, %d device(s), plen %d\n",
			       num, ev[2]);
			for (j = 0; j < num && 4 + j * 14 + 6 <= ev[2]; j++) {
				unsigned char *q = &ev[4 + j * 14];
				unsigned int cls = q[8] | (q[9] << 8) |
						   (q[10] << 16);

				printf("btinq2: FOUND %02x:%02x:%02x:%02x:%02x:%02x"
				       "  class 0x%06x\n",
				       q[5], q[4], q[3], q[2], q[1], q[0], cls);
				found++;
			}
		} else if (ev[1] == 0x01) {
			printf("btinq2: inquiry complete\n");
			break;
		}
	}
	printf("btinq2: %d device(s) found\n", found);
	close(fd);
	return found ? 0 : 2;
}
