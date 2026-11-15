/* btdetach — put /dev/ttyS1 back to N_TTY.
 *
 * The N_HCI line discipline is attached to the *tty*, not to the file
 * descriptor, so killing `pbhci --attach` leaves it in place and the kernel
 * Bluetooth core consumes every frame that later appears on the port.  A raw
 * probe then reads nothing and looks like a dead chip.  Reset it explicitly.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#ifndef N_TTY
#define N_TTY 0
#endif

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ttyS1";
	int fd, ldisc, cur = -1;

	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		fprintf(stderr, "btdetach: open %s: %s\n", dev, strerror(errno));
		return 1;
	}
	/* TIOCGETD reports the current discipline. */
	if (ioctl(fd, TIOCGETD, &cur) == 0)
		printf("btdetach: %s currently on ldisc %d\n", dev, cur);
	ldisc = N_TTY;
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		fprintf(stderr, "btdetach: TIOCSETD N_TTY: %s\n",
			strerror(errno));
		close(fd);
		return 1;
	}
	if (ioctl(fd, TIOCGETD, &cur) == 0)
		printf("btdetach: %s now on ldisc %d (0 = N_TTY)\n", dev, cur);
	close(fd);
	return 0;
}
