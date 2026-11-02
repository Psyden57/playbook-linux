/*
 * tzsmc — drive the monitor through trustzone-omap4's root-only raw SMC
 * pass-through on the PlayBook's QNX TabletOS.
 *
 * WHY THIS EXISTS
 *   trustzone-omap4 exposes devctl 0xC0280501 (root only) which forwards
 *   {funcid, a1, a2, nwords, w[0..3]} to the secure monitor via its own
 *   non-cached, physically-contiguous 4 KiB page.  That lets us run 0x12,
 *   0x31, 0x33, 0x2E and 0x2F on the *unarmed* reference, with no Linux
 *   image and no RPMB write.  See docs/reports/107, 146, 156 and the plan
 *   ~/.commandcode/plans/trustzone-smc-probe.md.
 *
 * SAFETY
 *   Only 0x12/0x31/0x33/0x2E/0x2F are ever issued.  0x2F writes into the
 *   caller's output buffer, never to RPMB.  0x32 (the RPMB write frame) is
 *   not implemented here.
 *
 * BUILD
 *   . tools/env.sh
 *   arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall -o /tmp/tzsmc tools/qnx-tzsmc.c
 *
 * USAGE
 *   tzsmc [-m] addr
 *   tzsmc [-m] nonce
 *   tzsmc [-m] kek
 *   tzsmc [-m] key <16-byte-file>
 *   tzsmc [-m] hmac <infile> <outfile> [keyflag]
 *   tzsmc [-m] unseal <cfile> <outfile>
 *   tzsmc [-m] seal <pfile> <outfile>
 *
 *   -m  subtract 0x10000000 from mem_offset64() before passing it to the
 *       monitor.  Default is to pass mem_offset64() verbatim (the trustzone
 *       binary's own convention).
 */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/procfs.h>
#include <sys/dcmd_all.h>

/* Declared in <sys/mman.h> only under __EXT_QNX/__EXT_LF64SRC. */
extern int mem_offset64(const void *__addr, int __fd, size_t __len,
                        off64_t *__off, size_t *__contig_len);

#define TZ_DEVCTL   0xC0280501
#define NOFD        (-1)
#define PG          0x1000

/* The resmgr's own 0x5002 path uses a 106-byte payload; the message-size
 * check in the 0x5001 handler rejects short transactions.  Send 106. */
#define TZ_PLEN 106
typedef struct {
    uint32_t funcid, a1, a2, nwords;
    uint32_t w[4];
    uint8_t  pad[TZ_PLEN - 32];
} tzreq;

static int tzfd = -1;
static void *pg[4];
static uint64_t phys[4];
static int minus_flag;

static uint64_t pass(int i)
{
    return minus_flag ? (phys[i] - 0x10000000ULL) : phys[i];
}

static void *alloc_page(int i)
{
    void *p = mmap(0, PG, PROT_READ | PROT_WRITE | PROT_NOCACHE,
                   MAP_ANON | MAP_PHYS | MAP_PRIVATE, NOFD, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    off64_t off = 0;
    size_t contig = 0;
    if (mem_offset64(p, NOFD, PG, &off, &contig) != 0) {
        perror("mem_offset64");
        exit(1);
    }
    pg[i] = p;
    phys[i] = (uint64_t)off;
    return p;
}

static void show_pt(int i, const char *tag)
{
    char path[64];
    procfs_mapinfo mi;
    int fd, n = 0;

    snprintf(path, sizeof path, "/proc/%d/as", (int)getpid());
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("%s page%d PTINFO: open %s: %s\n", tag, i, path,
               strerror(errno));
        return;
    }
    memset(&mi, 0, sizeof mi);
    mi.vaddr = (uint64_t)(uintptr_t)pg[i];
    if (devctl(fd, DCMD_PROC_PTINFO, &mi, sizeof mi, &n) != EOK) {
        printf("%s page%d PTINFO: devctl failed: %s\n", tag, i,
               strerror(errno));
        close(fd);
        return;
    }
    printf("%s page%d vaddr=0x%llx mem_offset64=0x%llx "
           "PTINFO_paddr=0x%llx pass=0x%llx\n",
           tag, i,
           (unsigned long long)(uintptr_t)pg[i],
           (unsigned long long)phys[i],
           (unsigned long long)mi.paddr,
           (unsigned long long)pass(i));
    close(fd);
}

static int smc(uint32_t funcid, uint32_t a1, uint32_t a2, uint32_t nwords,
               uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    tzreq r;
    int retval = 0x7fffffff;
    int rc;

    memset(&r, 0, sizeof r);
    r.funcid = funcid;
    r.a1 = a1;
    r.a2 = a2;
    r.nwords = nwords;
    r.w[0] = w0; r.w[1] = w1; r.w[2] = w2; r.w[3] = w3;

    rc = devctl(tzfd, (int)TZ_DEVCTL, &r, sizeof r, &retval);
    printf("smc funcid=0x%x a1=0x%x a2=0x%x nwords=%u "
           "w=[%08x %08x %08x %08x] -> devctl=%d retval=0x%x (%d)\n",
           funcid, a1, a2, nwords, w0, w1, w2, w3, rc,
           (unsigned)retval, retval);
    return retval;
}

static void hexdump(const char *tag, const unsigned char *b, int n)
{
    int i;
    printf("%s", tag);
    for (i = 0; i < n; i++)
        printf("%02x", b[i]);
    printf("\n");
}

static int read_file(const char *path, unsigned char *b, int max)
{
    int fd = open(path, O_RDONLY);
    int n;
    if (fd < 0) { perror(path); return -1; }
    n = read(fd, b, max);
    close(fd);
    if (n < 0) perror(path);
    return n;
}

static int write_file(const char *path, const unsigned char *b, int n)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int r;
    if (fd < 0) { perror(path); return -1; }
    r = write(fd, b, n);
    close(fd);
    if (r != n) { perror(path); return -1; }
    return 0;
}

static void alloc_all(void)
{
    int i;
    for (i = 0; i < 4; i++)
        alloc_page(i);
}

int main(int argc, char **argv)
{
    const char *mode;
    unsigned char *data, *out, *desc, *key;
    int ai = 1;

    if (argc < 2) {
        fprintf(stderr, "usage: %s [-m] addr|nonce|kek|key <k>|"
                "hmac <in> <out> [keyflag]|unseal <c> <out>|seal <p> <out>\n",
                argv[0]);
        return 1;
    }
    if (!strcmp(argv[ai], "-m")) { minus_flag = 1; ai++; }
    if (argc <= ai) { fprintf(stderr, "missing mode\n"); return 1; }
    mode = argv[ai++];

    tzfd = open("/dev/trustzone", O_RDWR);
    if (tzfd < 0) {
        perror("/dev/trustzone");
        return 1;
    }
    ThreadCtl(_NTO_TCTL_IO, 0);
    alloc_all();
    data = pg[0]; out = pg[1]; desc = pg[2]; key = pg[3];
    printf("pid=%d\n", (int)getpid());
    show_pt(0, "addr");
    show_pt(1, "addr");
    show_pt(2, "addr");
    show_pt(3, "addr");

    if (!strcmp(mode, "addr")) {
        return 0;
    }

    if (!strcmp(mode, "flags")) {
        int v = 0, rc;
        rc = devctl(tzfd, DCMD_ALL_GETFLAGS, &v, sizeof v, &v);
        printf("DCMD_ALL_GETFLAGS=0x%x rc=%d v=0x%x\n",
               (unsigned)DCMD_ALL_GETFLAGS, rc, v);
        return rc ? 2 : 0;
    }

    if (!strcmp(mode, "nonce")) {
        int i, ret;
        memset(data, 0xAA, PG);
        ret = smc(0x12, 0, 4, 2, (uint32_t)pass(0), 16, 0, 0);
        hexdump("data[0:32]=", data, 32);
        printf("data changed: ");
        for (i = 0; i < 16; i++)
            if (((unsigned char *)data)[i] != 0xAA) { printf("yes\n"); break; }
        if (i == 16) printf("no\n");
        write_file("/tmp/tz-nonce.bin", data, 16);
        return ret ? 2 : 0;
    }

    if (!strcmp(mode, "kek")) {
        int ret = smc(0x31, 0, 4, 1, 0, 0, 0, 0);
        return ret ? 2 : 0;
    }

    if (!strcmp(mode, "key")) {
        int n, ret;
        if (argc <= ai) { fprintf(stderr, "key: need file\n"); return 1; }
        n = read_file(argv[ai], key, 16);
        if (n != 16) { fprintf(stderr, "key: need 16 bytes, got %d\n", n); return 1; }
        hexdump("key=", key, 16);
        ret = smc(0x33, 0, 4, 2, (uint32_t)pass(3), 16, 0, 0);
        return ret ? 2 : 0;
    }

    if (!strcmp(mode, "hmac")) {
        int n, ret, keyflag = 0, olen = 32;
        uint16_t mode16 = 3;
        uint32_t *d = (uint32_t *)desc;
        if (argc <= ai + 1) { fprintf(stderr, "hmac: need in/out\n"); return 1; }
        n = read_file(argv[ai], data, 4096);
        if (n < 0) return 1;
        if (argc > ai + 2) keyflag = atoi(argv[ai + 2]);
        memset(desc, 0, PG);
        memcpy(&d[0], &mode16, 2);            /* u16 mode, u16 zero */
        d[1] = (uint32_t)pass(0);             /* data */
        d[2] = (uint32_t)n;                   /* len */
        d[3] = (uint32_t)pass(1);             /* out */
        d[4] = (uint32_t)olen;                /* outlen */
        d[5] = 0;                             /* ptr */
        d[6] = 0;                             /* lens */
        ret = smc(0x2E, 0, 4, 2, (uint32_t)pass(2), (uint32_t)keyflag, 0, 0);
        hexdump("out[0:32]=", out, 32);
        write_file(argv[ai + 1], out, olen);
        return ret ? 2 : 0;
    }

    if (!strcmp(mode, "unseal") || !strcmp(mode, "seal")) {
        int n, ret, outlen;
        uint32_t *d = (uint32_t *)desc;
        if (argc <= ai + 1) { fprintf(stderr, "%s: need in/out\n", mode); return 1; }
        n = read_file(argv[ai], data, 4096);
        if (n < 0) return 1;
        n = read_file(!strcmp(mode, "unseal") ? "/tmp/desc_unseal.bin"
                                              : "/tmp/desc_seal.bin",
                      desc, 0x60);
        if (n != 0x60) { fprintf(stderr, "descriptor template: got %d\n", n); return 1; }
        /* descriptor base is +0x00 of the template; +0x18 = input, +0x20 = output */
        d[0x18 / 4] = (uint32_t)pass(0);
        d[0x20 / 4] = (uint32_t)pass(1);
        ret = smc(0x2F, 0, 4, 1, (uint32_t)pass(2), 0, 0, 0);
        outlen = !strcmp(mode, "unseal") ? 224 : 256;
        hexdump("out[0:32]=", out, 32);
        write_file(argv[ai + 1], out, outlen);
        return ret ? 2 : 0;
    }

    fprintf(stderr, "unknown mode %s\n", mode);
    return 1;
}
