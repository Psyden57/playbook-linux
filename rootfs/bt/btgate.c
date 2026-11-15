/* btgate — find what gates the LE commands on the WL1283's BT core.
 *
 * Measured so far, on a freshly reset ROM:
 *   LE_Read_Buffer_Size            -> Success
 *   LE_Read_Local_Supported_Features -> Success, 8 zero bytes
 *   LE_Read_Supported_States (0x201c) -> **Command Disallowed (0x0c)**, 4 bytes
 *   Write_LE_Host_Support (0x0c6d)    -> **0x11** (unsupported parameter value)
 *
 * Command Disallowed means "no", not "don't know" — so the LE block exists but
 * is not permissible in the current controller state.  The service pack's last
 * command is 0xFDA8 00 ("enable Cortex Sleep") and the reference sends
 * 0xFDA8 01 ("disable") at the START of its script, so the first thing to rule
 * out is that the core is asleep.
 *
 * This probe walks a small state machine and prints the answer at each step, so
 * the gate is identified rather than guessed.
 *
 * usage: btgate [device] [baud]
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

static unsigned char in[512];
static int in_len;

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

/* Send one command; return the reply length or -1. */
static int cmd(int fd, unsigned short op, const unsigned char *p, int plen,
	       unsigned char *ev, int ms)
{
	unsigned char pkt[300];
	long end;

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

	end = 0;
	{
		struct timespec ts;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		end = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L + ms;
	}
	for (;;) {
		int e = take_event(ev);

		if (e > 0) {
			if ((ev[1] == 0x0e || ev[1] == 0x0f) &&
			    (ev[4] | (ev[5] << 8)) == op)
				return e;
			continue;
		}
		{
			struct timespec ts;
			long now;
			fd_set rf;
			struct timeval tv;

			clock_gettime(CLOCK_MONOTONIC, &ts);
			now = ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
			if (now >= end)
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

static void show(const char *label, int n, const unsigned char *ev)
{
	int i;

	if (n < 0) {
		printf("%-46s (no reply)\n", label);
		return;
	}
	printf("%-46s st=0x%02x", label, ev[6]);
	if (n > 7) {
		printf("  [");
		for (i = 7; i < n && i < 15; i++)
			printf("%02x", ev[i]);
		printf("]");
	}
	printf("\n");
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS1";
	unsigned long baud = argc > 2 ? strtoul(argv[2], NULL, 0) : 115200;
	struct termios t;
	unsigned char ev[300];
	int fd, n;
	unsigned char sleep_off[1] = { 0x01 };
	unsigned char sleep_on[1] = { 0x00 };
	unsigned char hostsup[2] = { 0x01, 0x00 };

	setvbuf(stdout, NULL, _IOLBF, 0);
	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "btgate: open %s: %s\n", dev, strerror(errno));
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

	printf("btgate: %s at %lu 8N1\n", dev, baud);

	n = cmd(fd, 0x0c03, NULL, 0, ev, 2000);
	show("0. HCI_Reset", n, ev);
	if (n < 0)
		return 1;

	n = cmd(fd, 0x201c, NULL, 0, ev, 2000);
	show("1. LE_Read_Supported_States (baseline)", n, ev);

	n = cmd(fd, 0xfda8, sleep_off, 1, ev, 2000);
	show("2. TI 0xFDA8 = 01 (disable Cortex Sleep)", n, ev);

	n = cmd(fd, 0x201c, NULL, 0, ev, 2000);
	show("3. LE_Read_Supported_States (sleep off)", n, ev);

	n = cmd(fd, 0x0c6d, hostsup, 2, ev, 2000);
	show("4. Write_LE_Host_Support(1,0) (sleep off)", n, ev);

	n = cmd(fd, 0x201c, NULL, 0, ev, 2000);
	show("5. LE_Read_Supported_States (after hostsup)", n, ev);

	n = cmd(fd, 0x2003, NULL, 0, ev, 2000);
	show("6. LE_Read_Local_Supported_Features", n, ev);

	n = cmd(fd, 0x2002, NULL, 0, ev, 2000);
	show("7. LE_Read_Buffer_Size", n, ev);

	n = cmd(fd, 0xfda8, sleep_on, 1, ev, 2000);
	show("8. TI 0xFDA8 = 00 (restore sleep)", n, ev);
	close(fd);
	return 0;
}
