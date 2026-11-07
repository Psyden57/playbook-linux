/* pbiotest — can this session actually read MMCHS5 (the WL1283's SDIO host)?
 *
 * memdump3 calls ThreadCtl(_NTO_TCTL_IO, 0) and ignores the result; its own
 * comment notes some QNX 6.6 configurations refuse it for sshd sessions.  When
 * it is refused, mmap_device_memory() can still hand back a mapping and every
 * access faults with SIGBUS — which is indistinguishable from an L4 firewall
 * from the outside.
 *
 * So report the ThreadCtl result, then try the block that io-pkt maps on this
 * very boot.  Run it on TabletOS while the WiFi driver is up: the values are
 * the target state for our Linux driver.
 *
 * usage: pbiotest [physaddr-hex]
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/neutrino.h>
#include <sys/procmgr.h>

static void dump(volatile uint32_t *p)
{
	printf("  SYSCONFIG %08x  SYSSTATUS %08x  CON %08x  CAPA %08x\n",
	       p[0x110 / 4], p[0x014 / 4], p[0x12c / 4], p[0x140 / 4]);
	printf("  HCTL      %08x  SYSCTL    %08x  STAT %08x  PSTATE %08x\n",
	       p[0x128 / 4], p[0x12c / 4], p[0x130 / 4], p[0x124 / 4]);
	printf("  IE        %08x  ISE       %08x  BLK  %08x  AC12 %08x\n",
	       p[0x134 / 4], p[0x138 / 4], p[0x104 / 4], p[0x13c / 4]);
}

int main(int argc, char **argv)
{
	uint64_t addr = argc > 1 ? strtoull(argv[1], NULL, 16) : 0x480D5000ULL;
	volatile uint32_t *p;
	int rc;

	/* unbuffered: a fault must not swallow the lines before it */
	setvbuf(stdout, NULL, _IONBF, 0);

	errno = 0;
	rc = ThreadCtl(_NTO_TCTL_IO, 0);
	printf("ThreadCtl(_NTO_TCTL_IO) = %d errno=%d (%s)\n",
	       rc, errno, strerror(errno));

	if (rc != 0) {
		/* As root we may grant the ability to ourselves, which is what
		 * a non-privileged sshd session lacks (errno 22 = EINVAL). */
		errno = 0;
		rc = procmgr_ability(0,
			PROCMGR_ADN_ROOT | PROCMGR_AOP_ALLOW | PROCMGR_AID_IO,
			PROCMGR_AID_EOL);
		printf("procmgr_ability(ALLOW|AID_IO) = %d errno=%d (%s)\n",
		       rc, errno, strerror(errno));
		errno = 0;
		rc = ThreadCtl(_NTO_TCTL_IO, 0);
		printf("ThreadCtl retry = %d errno=%d (%s)\n",
		       rc, errno, strerror(errno));
	}

	errno = 0;
	p = mmap_device_memory(NULL, 0x1000, PROT_READ | PROT_NOCACHE,
			       MAP_SHARED | MAP_PHYS, addr);
	if (p == MAP_FAILED) {
		printf("mmap_device_memory(%llx): %s\n",
		       (unsigned long long)addr, strerror(errno));
		return 1;
	}
	printf("mapped %llx\n", (unsigned long long)addr);
	dump(p);
	munmap_device_memory((void *)p, 0x1000);
	return 0;
}
