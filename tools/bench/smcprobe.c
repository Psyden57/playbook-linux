/* smcprobe.c — is the OMAP4 ROM "public API" reachable from this OS?
 *
 * The Linux SMP bring-up (arch/arm/mach-omap2/omap-smp.c) releases CPU1 with
 * SMC services 0x103/0x104/0x105 (omap_read_auxcoreboot0 /
 * omap_modify_auxcoreboot0 / omap_auxcoreboot_addr) and enables the SCU through
 * the same monitor.  If those services do not exist on this unit's ROM, an SMP
 * kernel can die in smp_prepare_cpus() long before the display comes up.
 *
 * This probe calls each service from a forked child (so an undefined
 * instruction or a fault kills the child, not the caller) and reports what
 * came back.  Read-only: the services tried are the ones Linux *reads*.
 *
 * Build (QNX 6.6, armle):
 *   arm-unknown-nto-qnx6.6.0eabi-gcc -O2 -Wall -o smcprobe smcprobe.c
 * Run:
 *   ./smcprobe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>

static unsigned long smc0(unsigned svc, unsigned long arg0)
{
    register unsigned long r0 __asm__("r0") = arg0;
    register unsigned long r12 __asm__("r12") = svc;

    /* SMC #0 as a raw encoding: binutils for this target rejects the mnemonic
     * without an explicit ARMv7 arch, and 0xE1600070 is what the kernel's own
     * omap-smc.S assembles to. */
    __asm__ __volatile__("dsb\n\t.word 0xe1600070"
                         : "+r"(r0) : "r"(r12) : "memory");
    return r0;
}

static void probe(unsigned svc, unsigned long arg0)
{
    int fd[2];
    pid_t pid;
    ssize_t n;
    unsigned long res = 0;

    if (pipe(fd) != 0) {
        perror("pipe");
        return;
    }
    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd[0]); close(fd[1]);
        return;
    }
    if (pid == 0) {
        close(fd[0]);
        res = smc0(svc, arg0);
        n = write(fd[1], &res, sizeof(res));
        (void)n;
        _exit(0);
    }
    close(fd[1]);
    {
        fd_set rfds;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(fd[0], &rfds);
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        if (select(fd[0] + 1, &rfds, NULL, NULL, &tv) <= 0) {
            printf("svc 0x%03x arg 0x%08lx -> no return within 3s "
                   "(service absent or blocking)\n", svc, arg0);
            fflush(stdout);
            close(fd[0]);
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return;
        }
    }
    n = read(fd[0], &res, sizeof(res));
    close(fd[0]);
    if (n == (ssize_t)sizeof(res))
        printf("svc 0x%03x arg 0x%08lx -> r0 0x%08lx\n",
               svc, arg0, res);
    else
        printf("svc 0x%03x arg 0x%08lx -> child died (%s), r0 unknown\n",
               svc, arg0, strerror(errno));
    waitpid(pid, NULL, 0);
    fflush(stdout);
}

int main(void)
{
    printf("smcprobe: OMAP4 ROM public API reachability\n");
    /* 0x103 read AUX_CORE_BOOT_0, 0x104 modify AUX_CORE_BOOT_0 (no-op mask),
     * 0x105 set AUX_CORE_BOOT_1 (address 0 is harmless), 0x102 is the L2
     * cache write path Linux already relies on (so it is the positive
     * control: our non-SMP kernel programs the PL310 through it). */
    probe(0x102, 0);
    probe(0x103, 0);
    probe(0x104, 0);
    probe(0x105, 0);
    printf("smcprobe: done\n");
    return 0;
}
