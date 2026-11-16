/* memzero.c — write zeroes over a physical range through /dev/mem.
 *
 * Used before a boot whose DEBUG_LL ring is going to be read: the ring pages
 * are `no-map` reserved, so `dd` to /dev/mem fails (EFAULT) while mmap works.
 *
 * usage: memzero <physaddr-hex> <size-hex>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    unsigned long addr, size;
    int fd;
    void *p;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <physaddr-hex> <size-hex>\n", argv[0]);
        return 1;
    }
    addr = strtoul(argv[1], NULL, 16);
    size = strtoul(argv[2], NULL, 16);
    if (size == 0 || size > 0x100000) {
        fprintf(stderr, "size 1..0x100000 please\n");
        return 1;
    }
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)addr);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    memset(p, 0, size);
    msync(p, size, MS_SYNC);
    munmap(p, size);
    close(fd);
    return 0;
}
