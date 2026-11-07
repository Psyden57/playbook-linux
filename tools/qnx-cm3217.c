/* cm3217probe — speak the CM3217's real protocol from QNX.
 *
 * This part has no register file: every transfer is a bare one-byte send or
 * receive (report 181 / ls-cm3217.so).  pbi2c cannot express that (it always
 * sends a register byte), which is why an earlier "enable then read" attempt was
 * invalid.
 *
 * usage: cm3217probe <bus> [msb_addr] [lsb_addr]
 *   default bus 3, 0x10, 0x11
 *
 * Sequence, exactly as the vendor driver does it:
 *   write 0x60 -> 0x11 (cmd2, once)
 *   write 0x26 -> 0x10 (ALS on), wait 200 ms
 *   read  0x11 (the two bytes are at the two addresses)
 *   read  0x10
 *   write 0x27 -> 0x10 (ALS off)
 * and it repeats the pair so a bit-settling first sample is visible.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <devctl.h>
#include <sys/types.h>
#include <hw/i2c.h>

#define DCMD_I2C_WRITE 0x80100505
#define DCMD_I2C_READ  0xC0140507

/* A bare one-byte write, and a bare one-byte read: no register phase at all.
 * The header is the QNX DCMD_I2C_WRITE/RECV descriptor followed by the payload
 * in the same devctlv vector (this is how pbi2c builds it). */
static int wr_byte(int fd, unsigned addr, unsigned char v)
{
	int32_t hdr[4] = { addr, I2C_ADDRFMT_7BIT, 1, 1 };
	iov_t iov[2];

	SETIOV(&iov[0], hdr, sizeof(hdr));
	SETIOV(&iov[1], &v, 1);
	return devctlv(fd, DCMD_I2C_WRITE, 2, 0, iov, NULL, NULL);
}

/* Register-form read: send one register byte, then receive one byte.  This is
 * what pbi2c does, and it is a different transaction from a bare read. */
static int rd_reg(int fd, unsigned addr, unsigned char reg, unsigned char *out)
{
	int32_t desc[5] = { addr, I2C_ADDRFMT_7BIT, 1, 1, 1 };
	iov_t iovA[2], iovB[2];

	SETIOV(&iovA[0], desc, sizeof(desc));
	SETIOV(&iovA[1], &reg, 1);
	SETIOV(&iovB[0], desc, sizeof(desc));
	SETIOV(&iovB[1], out, 1);
	return devctlv(fd, DCMD_I2C_READ, 2, 2, iovA, iovB, NULL);
}

static int rd_byte(int fd, unsigned addr, unsigned char *out)
{
	int32_t desc[5] = { addr, I2C_ADDRFMT_7BIT, 0, 1, 1 };
	iov_t iovA[1], iovB[2];

	SETIOV(&iovA[0], desc, sizeof(desc));
	SETIOV(&iovB[0], desc, sizeof(desc));
	SETIOV(&iovB[1], out, 1);
	return devctlv(fd, DCMD_I2C_READ, 1, 2, iovA, iovB, NULL);
}

int main(int argc, char **argv)
{
	int bus = argc > 1 ? atoi(argv[1]) : 3;
	unsigned msb = argc > 2 ? strtoul(argv[2], NULL, 0) : 0x10;
	unsigned lsb = argc > 3 ? strtoul(argv[3], NULL, 0) : 0x11;
	char path[32];
	unsigned char b;
	int fd, i, rc;

	snprintf(path, sizeof(path), "/dev/i2c%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open %s: %s\n", path, strerror(errno));
		return 1;
	}

	/* Restore the vendor's idle state: cmd2 to 0x11, then ALS off on 0x10,
	 * and leave it there.  This is where ls-cm3217 leaves the part. */
	b = 0x60;
	wr_byte(fd, lsb, b);
	usleep(20000);
	b = 0x27;
	wr_byte(fd, msb, b);
	usleep(50000);
	printf("restored: cmd2=0x60 -> 0x%02x, cmd1=0x27 (ALS off) -> 0x%02x\n",
	       lsb, msb);

	close(fd);
	return 0;
}
