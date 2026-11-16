/*
 * segvtrap — LD_PRELOAD SIGSEGV/SIGBUS/SIGILL tracer for the device.  A
 * constructor installs a handler that reports the faulting PC and the process
 * map, so a stripped binary's crash can be attributed to a library without a
 * debugger.  Device-side tool, built as a shared library:
 *   arm-linux-gcc -shared -fPIC -O2 -Wall -o /tmp/segvtrap.so \
 *       tools/bench/segvtrap.c
 * Usage: LD_PRELOAD=/tmp/segvtrap.so <program> [args...]
 *
 * Session 59 used this to pin epitaph's crash to Mesa swrast_dri.so.
 * Note: an execve wrapper that installs the handler will NOT work — exec
 * resets signal handlers to SIG_DFL; the LD_PRELOAD constructor is required.
 */
#define _GNU_SOURCE
#include <signal.h>
#include <ucontext.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static void dump_maps(void)
{
    char buf[4096];
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0)
        return;
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(2, buf, n);
    close(fd);
}

static void handler(int sig, siginfo_t *si, void *uc)
{
    char msg[512];
    ucontext_t *c = (ucontext_t *)uc;
    int len = snprintf(msg, sizeof(msg),
                       "\n=== SIG%d at PC=%p fault_addr=%p ===\n", sig,
                       (void *)c->uc_mcontext.arm_pc, si->si_addr);
    write(2, msg, len);
    dump_maps();
    _exit(128 + sig);
}

__attribute__((constructor)) static void setup(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
}
