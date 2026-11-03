/*
 * rfs_validator_shim.c -- replacement for /proc/boot/rfs_validator.
 *
 * WHY
 *   The stock rfs_validator verifies a signed hash of the RCFS (/base) and
 *   mounts it as a side effect.  The signing key is RIM's, so any modified
 *   /base fails verification; skipping the validator is the only route for a
 *   customised /base (report 158).  The .script line that runs it is
 *   `service_start rfsvalidator / rfs_validator -c / service_stop ...`, so the
 *   replacement must do the *mount* the validator would have done and exit 0.
 *
 * WHAT IT DOES
 *   Probes the four devices the stock binary probes (its own strings), mounts
 *   the first that works at /base exactly as the live system has it:
 *     qnx6, read-only, noatime, crypto=enable,sync=optional
 *   (df -g on the reference: Flags 00000021 [noatime, rdonly],
 *    Option crypto=enable,sync=optional; report 158 sections 3-4).
 *
 *   EBUSY means /base is already mounted (the stock unit running normally);
 *   that is success.  No hash is computed, nothing is written: every mount is
 *   read-only and the only opens are O_RDONLY probes.
 *
 * USAGE
 *   rfs_validator_shim [-c] [-v]
 *     -c  compare mode in the stock tool, which also means "mount".  Accepted
 *         for the .script; it takes the same path.  This is the invocation to
 *         test on a running unit: /tmp/shim -c  -> EBUSY -> exit 0.
 *     -v  verbose (repeatable).
 *
 * BUILD (QNX 6.6 SDP, from the repository root)
 *   . tools/env.sh
 *   ntoarmv7-gcc -O2 -Wall -o rfs_validator_shim tools/rfs_validator_shim.c
 *
 * NOT DONE HERE (deliberately): the signed-hash verification, the NVRAM
 * device lookup, the blacklist/radio checks and the -f generator mode.  The
 * mount call is the part the boot needs; milestone 4 tests it on hardware.
 */

#include <sys/mount.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *devices[] = {
    "/dev/hd1t179",             /* region-0 RCFS, the reference unit */
    "/dev/hd2t179",
    "/dev/hd7t179",
    "/dev/hd7t178",
};

static const char *mount_opts = "noatime,crypto=enable,sync=optional";

int main(int argc, char **argv)
{
    int check = 0;
    int verbose = 0;
    int i;
    size_t n;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            const char *p = argv[i] + 1;
            for (; *p; p++) {
                if (*p == 'c') {
                    check = 1;
                } else if (*p == 'v') {
                    verbose++;
                } else {
                    fprintf(stderr,
                            "rfs_validator: usage: rfs_validator [-c] [-v]\n");
                    return 2;
                }
            }
        } else {
            fprintf(stderr,
                    "rfs_validator: usage: rfs_validator [-c] [-v]\n");
            return 2;
        }
    }

    if (verbose) {
        printf("rfs_validator: shim%s, mounting /base read-only\n",
               check ? " (compare mode)" : "");
    }

    for (n = 0; n < sizeof(devices) / sizeof(devices[0]); n++) {
        const char *dev = devices[n];
        int fd;

        fd = open(dev, O_RDONLY);
        if (fd < 0) {
            if (verbose) {
                fprintf(stderr, "rfs_validator: %s: %s\n", dev,
                        strerror(errno));
            }
            continue;
        }
        close(fd);

        if (verbose) {
            printf("rfs_validator: mounting %s at /base\n", dev);
        }
        if (mount(dev, "/base", _MOUNT_READONLY | _MOUNT_NOATIME, "qnx6",
                  mount_opts, -1) == 0) {
            if (verbose) {
                printf("rfs_validator: mounted %s\n", dev);
            }
            return 0;
        }

        if (errno == EBUSY) {
            /* /base is already mounted (the normal running-unit case). */
            if (verbose) {
                printf("rfs_validator: /base already mounted (%s)\n", dev);
            }
            return 0;
        }
        if (verbose) {
            fprintf(stderr, "rfs_validator: mounting %s at /base failed (%d)\n",
                    dev, errno);
        }
    }

    fprintf(stderr, "rfs_validator: no usable /base device\n");
    return 1;
}
