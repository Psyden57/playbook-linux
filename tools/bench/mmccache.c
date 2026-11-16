/*
 * mmccache — read the eMMC EXT_CSD cache fields and set CACHE_CTRL (index 33)
 * via the MMC_IOC_CMD ioctl.  A tiny, self-contained stand-in for mmc-utils on
 * the Nura root (which has no mmc binary).  Device-side tool.
 *
 * Build (from the repo root, with the ARM toolchain sourced):
 *   arm-linux-gcc -static -O2 -Wall -o /tmp/mmccache tools/bench/mmccache.c
 * Push to /tmp on the unit paced (8 KiB / 5 ms; trap 302).
 *
 * Usage:
 *   mmccache read     — print EXT_CSD CACHE_SIZE/BCTRL/CACHE_CTRL and card ID
 *   mmccache off      — CMD6 write byte CACHE_CTRL=0 (cache off)
 *   mmccache on       — CMD6 write byte CACHE_CTRL=1 (restore)
 *
 * These are standard, reversible EXT_CSD writes.  CACHE_CTRL is volatile: the
 * MMC core re-enables the cache at init whenever CACHE_SIZE != 0, so "off"
 * lasts only until the next card re-init; the permanent fix is a kernel quirk.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/mmc/ioctl.h>

/* from include/linux/mmc/core.h */
#define MMC_RSP_PRESENT (1 << 0)
#define MMC_RSP_136     (1 << 1)
#define MMC_RSP_CRC     (1 << 2)
#define MMC_RSP_BUSY    (1 << 3)
#define MMC_RSP_OPCODE  (1 << 4)
#define MMC_CMD_AC      (0 << 5)
#define MMC_CMD_ADTC    (1 << 5)
#define MMC_RSP_R1      (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE)
#define MMC_RSP_R1B     (MMC_RSP_PRESENT|MMC_RSP_CRC|MMC_RSP_OPCODE|MMC_RSP_BUSY)
#define MMC_RSP_SPI_S1  (1 << 7)
#define MMC_RSP_SPI_BUSY (1 << 10)
#define MMC_RSP_SPI_R1  (MMC_RSP_SPI_S1)
#define MMC_RSP_SPI_R1B (MMC_RSP_SPI_S1|MMC_RSP_SPI_BUSY)

#define MMC_SEND_EXT_CSD 8
#define MMC_SWITCH       6
#define EXT_CSD_CACHE_CTRL 33
#define EXT_CSD_CACHE_SIZE 249
#define MMC_SWITCH_MODE_WRITE_BYTE 3

static const char *dev = "/dev/mmcblk1";

static int read_extcsd(int fd, unsigned char ext[512])
{
	struct mmc_ioc_cmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_SEND_EXT_CSD;
	cmd.arg = 0;
	cmd.flags = MMC_CMD_ADTC | MMC_RSP_SPI_R1 | MMC_RSP_R1;
	cmd.blksz = 512;
	cmd.blocks = 1;
	mmc_ioc_cmd_set_data(cmd, ext);
	return ioctl(fd, MMC_IOC_CMD, &cmd);
}

static int write_byte(int fd, int index, int value)
{
	struct mmc_ioc_cmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.opcode = MMC_SWITCH;
	cmd.arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) |
	          (index << 16) | (value << 8);
	cmd.flags = MMC_CMD_AC | MMC_RSP_SPI_R1B | MMC_RSP_R1B;
	return ioctl(fd, MMC_IOC_CMD, &cmd);
}

int main(int argc, char **argv)
{
	unsigned char ext[512] __attribute__((aligned(8)));
	unsigned int cache_size;
	int fd, rc = 1;

	if (argc != 2 || (strcmp(argv[1], "read") && strcmp(argv[1], "off") &&
	                  strcmp(argv[1], "on") && strcmp(argv[1], "dump"))) {
		fprintf(stderr, "usage: %s read|off|on|dump\n", argv[0]);
		return 2;
	}

	fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	if (read_extcsd(fd, ext) < 0) { perror("MMC_IOC_CMD CMD8 read"); goto out; }

	cache_size = ext[EXT_CSD_CACHE_SIZE] |
	             (ext[EXT_CSD_CACHE_SIZE+1] << 8) |
	             (ext[EXT_CSD_CACHE_SIZE+2] << 16) |
	             ((unsigned)ext[EXT_CSD_CACHE_SIZE+3] << 24);

	printf("EXT_CSD CACHE_SIZE   = %u (bytes 249..252)\n", cache_size);
	printf("EXT_CSD CACHE_CTRL   = 0x%02x (byte 33, volatile)\n", ext[EXT_CSD_CACHE_CTRL]);
	printf("EXT_CSD CACHE_FLUSH  = 0x%02x (byte 170)\n", ext[170]);
	printf("EXT_CSD REV          = 0x%02x (byte 192)\n", ext[192]);
	if (!strcmp(argv[1], "dump")) {
		int i;
		printf("EXT_CSD bytes 0..511:\n");
		for (i = 0; i < 512; i++) {
			if (i % 16 == 0) printf("\n%03x:", i);
			printf(" %02x", ext[i]);
		}
		printf("\n");
	}

	if (!strcmp(argv[1], "off") || !strcmp(argv[1], "on")) {
		int val = !strcmp(argv[1], "on") ? 1 : 0;
		if (write_byte(fd, EXT_CSD_CACHE_CTRL, val) < 0) {
			perror("MMC_IOC_CMD CMD6 write CACHE_CTRL");
			goto out;
		}
		if (read_extcsd(fd, ext) < 0) { perror("re-read"); goto out; }
		printf("after write: CACHE_CTRL = 0x%02x (%s)\n",
		       ext[EXT_CSD_CACHE_CTRL], val ? "on" : "off");
	}
	rc = 0;
out:
	close(fd);
	return rc;
}
