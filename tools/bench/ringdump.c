/* ringdump.c — read a physical range through /dev/mem and write it raw.
 *
 * `dd if=/dev/mem` fails (EFAULT) on the PlayBook's ring pages because they are
 * carved out with `no-map` in the DTS, so pfn_valid() is false and the /dev/mem
 * read path rejects them.  mmap() of /dev/mem still works (busybox devmem uses
 * it), so this does the same and copies the whole range out at once.
 *
 * usage: ringdump <physaddr-hex> <size-hex> [outfile]
 *        (32 KiB max per call, like memdump3)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
    unsigned long addr, size;
    int fd, out = 1;
    void *p;
    ssize_t n, off = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <physaddr-hex> <size-hex> [outfile]\n", argv[0]);
        return 1;
    }
    addr = strtoul(argv[1], NULL, 16);
    size = strtoul(argv[2], NULL, 16);
    if (size == 0 || size > 0x10000) {
        fprintf(stderr, "size 1..0x10000 please\n");
        return 1;
    }
    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }
    p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, (off_t)addr);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (argc > 3) {
        out = open(argv[3], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out < 0) {
            perror("open out");
            return 1;
        }
    }
    while (off < (ssize_t)size) {
        n = write(out, (char *)p + off, size - off);
        if (n <= 0) {
            perror("write");
            return 1;
        }
        off += n;
    }
    munmap(p, size);
    if (out != 1)
        close(out);
    return 0;
}
