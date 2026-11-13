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

/*
 * 16-bit-register variants, for a device with 16-bit register addresses and
 * 16-bit data (the WM8994 at I2C4 0x1a).  The only differences from the 8-bit
 * paths above are the register iovec length (2) and the descriptor's
 * register-size word; the header/descriptor shapes are otherwise unchanged.
 */
static int i2c_write16(int fd, int addr, unsigned reg, unsigned val)
{
	int32_t hdr[4] = { addr, 4, 2, 1 };
	unsigned char rb[2] = { (reg >> 8) & 0xff, reg & 0xff };
	unsigned char vb[2] = { (val >> 8) & 0xff, val & 0xff };
	iov_t iov[3];

	SETIOV(&iov[0], hdr, sizeof(hdr));
	SETIOV(&iov[1], rb, 2);
	SETIOV(&iov[2], vb, 2);
	return devctlv(fd, DCMD_I2C_WRITE, 3, 0, iov, NULL, NULL);
}

/* Reference-driver write ABI (deva-ctrl-omap4abe-wm8994.so `codec_write`,
 * file 0x18294): a 20-byte inline devctl struct, {addr,2,4,1,reg,val} with
 * reg/val stored big-endian.  devctlv with iovs is not what the driver does. */
static int i2c_write16r(int fd, int addr, unsigned reg, unsigned val)
{
	struct {
		int32_t addr, f1, f2, f3;
		uint16_t reg, val;
	} wr;

	wr.addr = addr;
	wr.f1 = 2;
	wr.f2 = 4;
	wr.f3 = 1;
	wr.reg = (uint16_t)(((reg >> 8) & 0xff) | ((reg & 0xff) << 8));
	wr.val = (uint16_t)(((val >> 8) & 0xff) | ((val & 0xff) << 8));
	return devctl(fd, DCMD_I2C_WRITE, &wr, sizeof(wr), 0);
}

static int i2c_read16(int fd, int addr, unsigned reg, unsigned char *out, int n)
{
	int32_t desc[5] = { addr, 2, 2, n, 1 };
	unsigned char rb[2] = { (reg >> 8) & 0xff, reg & 0xff };
	iov_t iovA[2], iovB[2];

	SETIOV(&iovA[0], desc, sizeof(desc));
	SETIOV(&iovA[1], rb, 2);
	SETIOV(&iovB[0], desc, sizeof(desc));
	SETIOV(&iovB[1], out, n);
	return devctlv(fd, DCMD_I2C_READ, 2, 2, iovA, iovB, NULL);
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
			"       %s rdw   <bus> <addr7> <reg>      (2-byte SMBus word)\n"
			"       %s rd16  <bus> <addr7> <reg16>\n"
			"       %s wr    <bus> <addr7> <reg> <val>\n"
			"       %s wr16  <bus> <addr7> <reg16> <val16>\n"
			"       %s scan  <bus>\n"
			"       %s dump  <bus> <addr7> <from> <to>\n"
			"       %s dumpw <bus> <addr7> <from> <to>  (2-byte words)\n"
			"       %s dump16 <bus> <addr7> <from16> <to16>\n",
			argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
			argv[0], argv[0], argv[0], argv[0]);
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

	if (!strcmp(argv[1], "rd") || !strcmp(argv[1], "dump") ||
	    !strcmp(argv[1], "rdw") || !strcmp(argv[1], "dumpw")) {
		int word = (!strcmp(argv[1], "rdw") || !strcmp(argv[1], "dumpw"));
		if (argv[1][0] == 'r') {
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
			rc = i2c_read(fd, addr, reg, buf, word ? 2 : 1);
			if (rc == 0) {
				if (word)
					printf("0x%02x[0x%02x] = 0x%04x (lo=0x%02x hi=0x%02x)\n",
					       addr, reg, buf[0] | (buf[1] << 8), buf[0], buf[1]);
				else
					printf("0x%02x[0x%02x] = 0x%02x\n", addr, reg, buf[0]);
			} else
				printf("0x%02x[0x%02x] ERR %s\n", addr, reg, strerror(errno));
		}
		close(fd);
		return 0;
	}

	if (!strcmp(argv[1], "rd16") || !strcmp(argv[1], "dump16")) {
		if (argv[1][0] == 'r') {
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
			rc = i2c_read16(fd, addr, reg, buf, 2);
			if (rc == 0)
				printf("0x%02x[0x%04x] = 0x%04x (hi=0x%02x lo=0x%02x)\n",
				       addr, reg, (buf[0] << 8) | buf[1], buf[0], buf[1]);
			else
				printf("0x%02x[0x%04x] ERR %s\n", addr, reg, strerror(errno));
		}
		close(fd);
		return 0;
	}

	if (!strcmp(argv[1], "wr16")) {
		if (argc < 6 || !parse_hex(argv[4], &reg) || !parse_hex(argv[5], &val)) {
			fprintf(stderr, "bad register/value\n");
			return 2;
		}
		rc = i2c_write16(fd, addr, reg, val);
		if (rc == 0)
			printf("0x%02x[0x%04x] <- 0x%04x ok\n", addr, reg, val);
		else
			printf("0x%02x[0x%04x] <- 0x%04x FAILED %s\n", addr, reg, val,
			       strerror(errno));
		close(fd);
		return rc ? 1 : 0;
	}

	if (!strcmp(argv[1], "wr16r")) {
		if (argc < 6 || !parse_hex(argv[4], &reg) || !parse_hex(argv[5], &val)) {
			fprintf(stderr, "bad register/value\n");
			return 2;
		}
		rc = i2c_write16r(fd, addr, reg, val);
		if (rc == 0)
			printf("0x%02x[0x%04x] <- 0x%04x ok (ref-abi)\n",
			       addr, reg, val);
		else
			printf("0x%02x[0x%04x] <- 0x%04x FAILED %s\n", addr, reg, val,
			       strerror(errno));
		close(fd);
		return rc ? 1 : 0;
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
