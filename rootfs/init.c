/*
 * PlayBook bring-up — the first program that runs *from* the region-1
 * filesystem.
 *
 * The initramfs /init loop-mounts the ext4 image this lives in (read-only,
 * noload, at raw 0x29000000) and then forks this program.  Reaching it proves
 * three things at once: the image landed on the eMMC, the ext4 driver reads it
 * through a loop device, and code can be executed from it.  It is deliberately
 * small and stateless: it writes one line to /dev/kmsg and one to the console,
 * reads the version file, and exits.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/utsname.h>

static void kmsg(const char *s)
{
	int fd = open("/dev/kmsg", O_WRONLY);

	if (fd >= 0) {
		char b[512];
		int n = snprintf(b, sizeof(b), "<0>%s", s);

		write(fd, b, n);
		close(fd);
	}
}

static void tty(const char *s)
{
	int fd = open("/dev/ttyGS0", O_WRONLY | O_NONBLOCK);

	if (fd >= 0) {
		write(fd, s, strlen(s));
		close(fd);
	}
}

int main(void)
{
	struct utsname u;
	char ver[128] = "(no /etc/pbroot-version)";
	char kline[512], tline[600];
	int fd, n;

	if (uname(&u) == 0)
		snprintf(kline, sizeof(kline),
			 "PB-ROOTFS: /init is running from the region-1 ext4 image "
			 "(%s %s)", u.sysname, u.release);
	else
		snprintf(kline, sizeof(kline),
			 "PB-ROOTFS: /init is running from the region-1 ext4 image");
	kmsg(kline);

	fd = open("/etc/pbroot-version", O_RDONLY);
	if (fd >= 0) {
		n = read(fd, ver, sizeof(ver) - 1);
		close(fd);
		if (n > 0) {
			ver[n] = 0;
			while (n > 0 && (ver[n - 1] == '\n' || ver[n - 1] == '\r'))
				ver[--n] = 0;
		}
	}

	snprintf(tline, sizeof(tline),
		 "[ROOTFS] /init ran from the region-1 ext4 image; version: %s\r\n",
		 ver);
	tty(tline);
	return 0;
}
