/* btsload — replay a TI .bts service pack over H4 on the WL1283's BT core.
 *
 * The reference OS does this before its stack starts: `btp-btmgr` +
 * `bts_LoadScript` push the file's HCI commands at the raw UART, and only then
 * does the transport come up.  We have to do the same, and the reason is
 * measured (btsurvey): the ROM answers every BR/EDR command, but
 * HCI_LE_Read_Supported_States (0x201c) comes back **Command Disallowed**
 * (0x0c) with a short payload, which makes mainline's `hci_dev_init_sync()`
 * fail and `hci0` never register with mgmt (`num_controllers = 0`).
 *
 * .bts format (measured on this unit's WL1283.bts, 76 587 B):
 *   0x00  "BTSB", u32 version, then padding to 0x20
 *   0x20  records: u16 type, u16 len, payload[len]
 *           type 6 = comment (skip)
 *           type 1 = one or more H4 command packets, sent verbatim
 *           type 2 = the expected event: u32 timeout_ms, u32 len, event bytes
 *
 * usage: btsload [file.bts] [device] [baud] [--no-verify]
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

#define MAXPKT 600

static unsigned char in[MAXPKT * 2];
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

static long now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* Pull one whole H4 event out of the buffer.  Returns its length or 0. */
static int take_event(unsigned char *out)
{
	int plen, need, i;

	if (in_len < 2)
		return 0;
	if (in[0] != 0x04) {
		/* Resynchronise on the next event marker. */
		for (i = 1; i < in_len; i++)
			if (in[i] == 0x04)
				break;
		if (i >= in_len) {
			in_len = 0;
			return 0;
		}
		memmove(in, in + i, in_len - i);
		in_len -= i;
		if (in_len < 2)
			return 0;
	}
	plen = in[2];
	need = 3 + plen;
	if (in_len < need)
		return 0;
	memcpy(out, in, need);
	memmove(in, in + need, in_len - need);
	in_len -= need;
	return need;
}

/* Does this event satisfy the script's expectation?
 *
 * The .bts expectation is a whole H4 event, but its byte 3 is
 * `num_hci_command_packets`, which the chip is free to report as 0 or 1, so an
 * exact compare rejects correct replies.  Match the event code, the opcode and
 * (for Command Complete) the status instead.
 */
static int ev_matches(const unsigned char *got, int glen,
		      const unsigned char *want, int wlen)
{
	if (wlen < 7 || glen < 7)
		return 0;
	if (want[0] != 0x04 || got[0] != 0x04)
		return 0;
	if (got[1] != want[1])
		return 0;
	if (got[1] == 0x0e)			/* Command Complete */
		return got[4] == want[4] && got[5] == want[5] &&
		       got[6] == want[6];
	if (got[1] == 0x0f)			/* Command Status */
		return got[4] == want[4] && got[5] == want[5] &&
		       got[6] == want[6];
	/* Other events: compare the payload verbatim. */
	return glen == wlen && !memcmp(got, want, wlen);
}

/* Wait for an event.  If `want` is given, keep reading until it matches. */
static int wait_event(int fd, long timeout_ms, const unsigned char *want,
		      int want_len, unsigned char *got)
{
	long end = now_ms() + timeout_ms;

	for (;;) {
		int n = take_event(got);
		long left;

		if (n > 0) {
			if (!want)
				return n;
			if (ev_matches(got, n, want, want_len))
				return n;
			printf("      (unexpected:");
			{
				int k;

				for (k = 0; k < n && k < 12; k++)
					printf(" %02x", got[k]);
			}
			printf(" want:");
			{
				int k;

				for (k = 0; k < want_len && k < 12; k++)
					printf(" %02x", want[k]);
			}
			printf(")\n");
		}
		left = end - now_ms();
		if (left <= 0) {
			if (in_len == 0)
				return -1;
			/* Half a packet on the wire: fall through and keep reading. */
			if (left < -200)
				return -1;
		} else {
			fd_set rf;
			struct timeval tv;

			FD_ZERO(&rf);
			FD_SET(fd, &rf);
			tv.tv_sec = left / 1000;
			tv.tv_usec = (left % 1000) * 1000;
			if (select(fd + 1, &rf, NULL, NULL, &tv) > 0) {
				int r = read(fd, in + in_len,
					     sizeof(in) - in_len);

				if (r <= 0)
					return -1;
				in_len += r;
			}
		}
	}
}

int main(int argc, char **argv)
{
	const char *file = "WL1283.bts", *dev = "/dev/ttyS1";
	unsigned long baud = 115200;
	struct termios t;
	unsigned char *d, *p, *end, ev[MAXPKT];
	struct {
		unsigned char pkt[300];
		int len;
	} last;
	long size;
	int fd, i, n, ok = 0, bad = 0, sent = 0, verify = 1, pos = 0;
	int stay_awake = 0, post_reset_probe = 0;

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--no-verify")) {
			verify = 0;
		} else if (!strcmp(argv[i], "--stay-awake")) {
			stay_awake = 1;
		} else if (!strcmp(argv[i], "--probe-reset")) {
			post_reset_probe = 1;
		} else if (pos == 0) {
			file = argv[i];
			pos++;
		} else if (pos == 1) {
			dev = argv[i];
			pos++;
		} else {
			baud = strtoul(argv[i], NULL, 0);
			pos++;
		}
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "btsload: open %s: %s\n", file, strerror(errno));
		return 1;
	}
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);
	d = malloc(size);
	if (!d || read(fd, d, size) != size) {
		fprintf(stderr, "btsload: read %s failed\n", file);
		return 1;
	}
	close(fd);
	if (size < 0x24 || memcmp(d, "BTSB", 4)) {
		fprintf(stderr, "btsload: %s is not a .bts container\n", file);
		return 1;
	}
	printf("btsload: %s, %ld B, version %u\n", file, size,
	       *(unsigned int *)(d + 4));

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "btsload: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	if (tcgetattr(fd, &t) < 0) {
		fprintf(stderr, "btsload: tcgetattr: %s\n", strerror(errno));
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
		fprintf(stderr, "btsload: tcsetattr: %s\n", strerror(errno));
		return 1;
	}
	tcflush(fd, TCIOFLUSH);

	/* HCI_Reset first: the script assumes a fresh ROM. */
	{
		unsigned char rst[4] = { 0x01, 0x03, 0x0c, 0x00 };
		unsigned char want[7] = { 0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };

		if (write(fd, rst, sizeof(rst)) != sizeof(rst)) {
			fprintf(stderr, "btsload: reset write: %s\n",
				strerror(errno));
			return 1;
		}
		n = wait_event(fd, 2000, want, sizeof(want), ev);
		if (n < 0) {
			fprintf(stderr, "btsload: no HCI_Reset answer — is "
				"BT_EN high and /dev/ttyS1 free?\n");
			return 1;
		}
		printf("btsload: HCI_Reset answered\n");
	}

	last.len = 0;
	p = d + 0x20;
	end = d + size;
	while (p + 4 <= end) {
		unsigned int type = p[0] | (p[1] << 8);
		unsigned int len = p[2] | (p[3] << 8);

		if (p + 4 + len > end) {
			fprintf(stderr, "btsload: truncated record at 0x%lx\n",
				(long)(p - d));
			break;
		}
		if (type == 6) {
			/* comment */
		} else if (type == 1) {
			if (len > sizeof(last.pkt)) {
				fprintf(stderr, "btsload: command too big (%u)\n",
					len);
				break;
			}
			memcpy(last.pkt, p + 4, len);
			last.len = len;
			if (write(fd, p + 4, len) != (ssize_t)len) {
				fprintf(stderr, "btsload: write: %s\n",
					strerror(errno));
				break;
			}
			sent++;
			in_len = 0;
		} else if (type == 2) {
			unsigned int timeout, elen;
			int r;

			if (len < 8) {
				p += 4 + len;
				continue;
			}
			memcpy(&timeout, p + 4, 4);
			memcpy(&elen, p + 8, 4);
			if (8 + elen > len)
				elen = len - 8;
			r = wait_event(fd, timeout ? timeout : 2000,
				       p + 12, elen, ev);
			if (r < 0) {
				bad++;
				printf("btsload: no event for cmd %d (%s)\n",
				       sent, strerror(errno));
				if (bad > 8) {
					printf("btsload: giving up after %d "
					       "silent commands\n", bad);
					break;
				}
			} else {
				ok++;
			}
		}
		p += 4 + len;
	}

	printf("btsload: %d command(s) sent, %d answered, %d unanswered\n",
	       sent, ok, bad);

	if (stay_awake) {
		/* The script's last action is 0xFDA8 00 = *enable* Cortex Sleep.
		 * Re-disable it so the core keeps answering while we measure. */
		unsigned char c[5] = { 0x01, 0xa8, 0xfd, 0x01, 0x01 };

		in_len = 0;
		if (write(fd, c, sizeof(c)) == sizeof(c)) {
			int r = wait_event(fd, 2000, NULL, 0, ev);

			printf("btsload: 0xFDA8 01 (disable Cortex Sleep) -> %s\n",
			       r > 0 ? "answered" : "no reply");
		}
		usleep(50000);
	}

	if (post_reset_probe) {
		/* Does HCI_Reset undo the pack?  Read the states reply length,
		 * reset, and read it again -- all inside one session so the core
		 * never idles.  12 bytes = pack present, 11 = bare ROM. */
		unsigned char c[4] = { 0x01, 0x1c, 0x20, 0x00 };
		int r, j;

		in_len = 0;
		if (write(fd, c, 4) == 4) {
			r = wait_event(fd, 2000, NULL, 0, ev);
			printf("btload: states BEFORE reset: %d B:", r);
			for (j = 0; j < r && j < 14; j++)
				printf(" %02x", ev[j]);
			printf("\n");
		}
		c[1] = 0x03; c[2] = 0x0c; c[3] = 0x00;
		in_len = 0;
		if (write(fd, c, 4) == 4)
			printf("btload: HCI_Reset -> %d B\n",
			       wait_event(fd, 2000, NULL, 0, ev));
		c[1] = 0x1c; c[2] = 0x20; c[3] = 0x00;
		in_len = 0;
		if (write(fd, c, 4) == 4) {
			r = wait_event(fd, 2000, NULL, 0, ev);
			printf("btload: states AFTER reset:  %d B:", r);
			for (j = 0; j < r && j < 14; j++)
				printf(" %02x", ev[j]);
			printf("\n");
		}
	}

	if (verify) {
		/* Did the service pack make LE work?  This is the whole point. */
		static const struct {
			const char *name;
			unsigned short op;
		} q[] = {
			{ "Read_Local_Version_Information", 0x1001 },
			{ "LE_Read_Supported_States", 0x201c },
			{ "LE_Read_Local_Supported_Features", 0x2003 },
			{ "LE_Read_Buffer_Size", 0x2002 },
		};
		unsigned int k;

		printf("\nbtsload: post-load capability check\n");
		for (k = 0; k < sizeof(q) / sizeof(q[0]); k++) {
			unsigned char c[4];
			int r, j;

			c[0] = 0x01;
			c[1] = q[k].op & 0xff;
			c[2] = q[k].op >> 8;
			c[3] = 0x00;
			in_len = 0;
			if (write(fd, c, 4) != 4)
				break;
			r = wait_event(fd, 3000, NULL, 0, ev);
			if (r < 0) {
				printf("  %-34s -> (no reply)\n", q[k].name);
				continue;
			}
			printf("  %-34s ->", q[k].name);
			for (j = 0; j < r && j < 24; j++)
				printf(" %02x", ev[j]);
			printf("\n");
		}
	}
	close(fd);
	return bad ? 2 : 0;
}
