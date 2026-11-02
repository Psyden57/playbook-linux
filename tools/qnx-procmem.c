/*
 * pbmem — QNX process mapping metadata (no address-space reads).
 *
 * Why: the RPMB frame key lives in rpmb-omap4's non-cached anonymous page at
 * page+0x460 (report 141 §3.2/§6.3).  QNX ASLR moves that page between boots,
 * so it must be located in the live process first.  DCMD_PROC_MAPINFO gives
 * the virtual range and the flags; the page can then be read with a physical
 * reader (memdump3) once its physical address is known.
 *
 * DO NOT read the address space through /proc/<pid>/as on this unit.  A
 * lseek()+read() at a virtual address ("the documented interface") faulted
 * the procfs server and took TabletOS down (session 19).  Every mode here is
 * a devctl() metadata query, the same class of call `pidin` makes.
 *
 * usage:
 *   pbmem <pid> map                 segment list (DCMD_PROC_MAPINFO)
 *   pbmem <pid> page                per-page list (DCMD_PROC_PAGEDATA)
 *   pbmem <pid> pt <hexvaddr> [n]   page table from vaddr (DCMD_PROC_PTINFO)
 *
 * build (QNX 6.6 toolchain, see docs/project/SETUP.md §2):
 *   $Q/host/linux/x86/usr/bin/arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall \
 *       -o pbmem qnx-procmem.c
 */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/procfs.h>

static void show(const char *tag, procfs_mapinfo *mi, int n)
{
	int i;

	printf("%s: %d entr%s, sizeof(procfs_mapinfo)=%u\n", tag, n,
	       n == 1 ? "y" : "ies", (unsigned)sizeof(procfs_mapinfo));
	for (i = 0; i < n; i++)
		printf("  vaddr %08llx size %08llx flags %08x paddr %08llx\n",
		       (unsigned long long)mi[i].vaddr,
		       (unsigned long long)mi[i].size, mi[i].flags,
		       (unsigned long long)mi[i].paddr);
}

int main(int argc, char **argv)
{
	char path[64];
	procfs_mapinfo *mi;
	pid_t pid;
	int fd, n = 0, dummy = 0;

	if (argc < 3) {
		fprintf(stderr,
			"usage: %s <pid> map|page\n"
			"       %s <pid> pt <hexvaddr> [count]\n",
			argv[0], argv[0]);
		return 1;
	}
	pid = atoi(argv[1]);
	snprintf(path, sizeof path, "/proc/%d/as", pid);
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror(path);
		return 1;
	}

	if (!strcmp(argv[2], "map") || !strcmp(argv[2], "page")) {
		int dcmd = !strcmp(argv[2], "map") ? DCMD_PROC_MAPINFO
						   : DCMD_PROC_PAGEDATA;

		if (devctl(fd, dcmd, NULL, 0, &n) != EOK) {
			perror("devctl(count)");
			return 1;
		}
		if (n <= 0) {
			printf("no mappings\n");
			return 0;
		}
		mi = calloc((size_t)n, sizeof(*mi));
		if (!mi)
			return 1;
		if (devctl(fd, dcmd, mi, sizeof(*mi) * (size_t)n,
			   &dummy) != EOK) {
			perror("devctl(data)");
			return 1;
		}
		show(argv[2], mi, n);
		free(mi);
		return 0;
	}

	if (!strcmp(argv[2], "pt")) {
		uint64_t vaddr;
		int want = argc > 4 ? atoi(argv[4]) : 16;

		if (argc < 4 || want <= 0)
			return 1;
		vaddr = strtoull(argv[3], NULL, 16);
		mi = calloc((size_t)want, sizeof(*mi));
		if (!mi)
			return 1;
		mi[0].vaddr = vaddr;
		if (devctl(fd, DCMD_PROC_PTINFO, mi,
			   sizeof(*mi) * (size_t)want, &n) != EOK) {
			perror("devctl(ptinfo)");
			return 1;
		}
		show("pt", mi, n < want ? n : want);
		free(mi);
		return 0;
	}

	fprintf(stderr, "unknown mode %s\n", argv[2]);
	return 1;
}
