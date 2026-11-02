/*
 * pbi2c — generic I2C register access on QNX/TabletOS for the PlayBook bench.
 *
 * Built for the bring-up sessions to use the *reference OS* as an oracle:
 * probe the LM3532 at 0x38, dump TWL6030 registers, and test causality by
 * toggling rails with pmic-twl6030-ctl and re-probing.
 *
 * ABI reconstructed from pmic-winchester-twl6030 / omap4430-wdtkick
 * (reports 108 and 119): the QNX OMAP I2C driver takes a 4-word header
 * {addr7, len, nbufs, flags} followed by the data iovecs.
 *
 *   write: devctlv(fd, 0x80100505, 3, 0, iov, NULL, NULL)
 *          iov = { {hdr,16}, {&reg,1}, {val,n} }
 *   read : devctlv(fd, 0xC0140507, 2, 2, iovA, iovB, NULL)
 *          desc[5] = {addr7, 2, 1, n, 1}
 *          iovA = { {desc,20}, {&reg,1} }, iovB = { {desc,20}, {out,n} }
 *
 * READ-ONLY BY DEFAULT except for 'wr', which is only ever pointed at the
 * TWL6030 (0x48/0x49/0x4A) and the LM3532 (0x38).  Never point it at the
 * eMMC, RPMB or BOOTCTR.
 *
 * usage:
 *   pbi2c probe <bus> <addr7>
 *   pbi2c rd    <bus> <addr7> <reg>
 *   pbi2c wr    <bus> <addr7> <reg> <val>
 *   pbi2c scan  <bus>
 *   pbi2c dump  <bus> <addr7> <from> <to>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <devctl.h>
#include <sys/types.h>
#include <stdint.h>

#define DCMD_I2C_WRITE 0x80100505
#define DCMD_I2C_READ  0xC0140507

static int i2c_open(int bus)
{
	char path[32];
	int fd;

	snprintf(path, sizeof(path), "/dev/i2c%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0)
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
	return fd;
}

static int i2c_write(int fd, int addr, unsigned reg, const unsigned char *val, int n)
{
	int32_t hdr[4] = { addr, 2, n + 1, 1 };
	iov_t iov[3];

	SETIOV(&iov[0], hdr, sizeof(hdr));
	SETIOV(&iov[1], &reg, 1);
	SETIOV(&iov[2], val, n);
	return devctlv(fd, DCMD_I2C_WRITE, 3, 0, iov, NULL, NULL);
}

static int i2c_read(int fd, int addr, unsigned reg, unsigned char *out, int n)
{
	int32_t desc[5] = { addr, 2, 1, n, 1 };
	iov_t iovA[2], iovB[2];

	SETIOV(&iovA[0], desc, sizeof(desc));
	SETIOV(&iovA[1], &reg, 1);
	SETIOV(&iovB[0], desc, sizeof(desc));
	SETIOV(&iovB[1], out, n);
	return devctlv(fd, DCMD_I2C_READ, 2, 2, iovA, iovB, NULL);
}

static int parse_hex(const char *s, unsigned *v)
{
	char *end;

	*v = strtoul(s, &end, 16);
	return (*end == '\0');
}

int main(int argc, char **argv)
{
	int fd, rc, bus, n;
	unsigned addr, reg, val, from, to;
	unsigned char buf[16];

	if (argc < 3) {
		fprintf(stderr,
			"usage: %s probe <bus> <addr7>\n"
			"       %s rd    <bus> <addr7> <reg>\n"
			"       %s wr    <bus> <addr7> <reg> <val>\n"
			"       %s scan  <bus>\n"
			"       %s dump  <bus> <addr7> <from> <to>\n",
			argv[0], argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}

	bus = atoi(argv[2]);
	fd = i2c_open(bus);
	if (fd < 0)
		return 1;

	if (!strcmp(argv[1], "scan")) {
		int found = 0;

		for (addr = 0x03; addr <= 0x77; addr++) {
			unsigned char b;

			if (i2c_read(fd, addr, 0x00, &b, 1) == 0) {
				printf("ACK  0x%02x (reg00=0x%02x)\n", addr, b);
				found++;
			}
		}
		printf("scan /dev/i2c%d: %d device(s)\n", bus, found);
		close(fd);
		return 0;
	}

	if (!parse_hex(argv[3], &addr)) {
		fprintf(stderr, "bad address\n");
		return 2;
	}

	if (!strcmp(argv[1], "probe")) {
		rc = i2c_read(fd, addr, 0x00, buf, 1);
		if (rc == 0)
			printf("0x%02x ACK  (reg00=0x%02x)\n", addr, buf[0]);
		else
			printf("0x%02x NAK  (%s)\n", addr, strerror(errno));
		close(fd);
		return rc ? 1 : 0;
	}

	if (!strcmp(argv[1], "rd") || !strcmp(argv[1], "dump")) {
		if (!strcmp(argv[1], "rd")) {
			if (argc < 5 || !parse_hex(argv[4], &reg)) {
				fprintf(stderr, "bad register\n");
				return 2;
			}
			from = to = reg;
		} else {
			if (argc < 6 || !parse_hex(argv[4], &from) || !parse_hex(argv[5], &to)) {
				fprintf(stderr, "bad range\n");
				return 2;
			}
		}
		for (reg = from; reg <= to; reg++) {
			rc = i2c_read(fd, addr, reg, buf, 1);
			if (rc == 0)
				printf("0x%02x[0x%02x] = 0x%02x\n", addr, reg, buf[0]);
			else
				printf("0x%02x[0x%02x] ERR %s\n", addr, reg, strerror(errno));
		}
		close(fd);
		return 0;
	}

	if (!strcmp(argv[1], "wr")) {
		if (argc < 6 || !parse_hex(argv[4], &reg) || !parse_hex(argv[5], &val)) {
			fprintf(stderr, "bad register/value\n");
			return 2;
		}
		buf[0] = (unsigned char)val;
		n = 1;
		rc = i2c_write(fd, addr, reg, buf, n);
		if (rc == 0)
			printf("0x%02x[0x%02x] <- 0x%02x ok\n", addr, reg, val);
		else
			printf("0x%02x[0x%02x] <- 0x%02x FAILED %s\n", addr, reg, val,
			       strerror(errno));
		close(fd);
		return rc ? 1 : 0;
	}

	fprintf(stderr, "unknown command %s\n", argv[1]);
	close(fd);
	return 2;
}
