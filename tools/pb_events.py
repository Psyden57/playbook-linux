#!/usr/bin/env python3
"""Decode the PlayBook printk-free event log at ring1 PA 0x88000000.

Layout:
  +0x00  heartbeat (0x3700 << 16) | (jiffies & 0xffff), one per TWD tick
  +0x08  magic 0x50424556 ("PBEV")
  +0x0c  event counter
  +0x10  28 x u32, slot = counter % 28, word = (code16 << 16) | (jiffies & 0xffff)

Usage: pb_events.py [--raw FILE]    (default: read the device over SSH)
"""
import argparse
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KEY = os.environ.get('PB_KEY', str(ROOT / 'local' / 'secrets' / 'rsa'))
SYSTEMMAP = os.environ.get('PB_SYSTEMMAP',
                           str(ROOT / 'local' / 'kernel-tree' / 'System.map'))

SSH = [
    'ssh', '-i', KEY,
    '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
    '-o', 'IdentitiesOnly=yes', '-o', 'HostKeyAlgorithms=+ssh-rsa',
    '-o', 'PubkeyAcceptedKeyTypes=+ssh-rsa', '-o', 'PubkeyAcceptedAlgorithms=+ssh-rsa',
    '-o', 'KexAlgorithms=+diffie-hellman-group1-sha1',
    '-o', 'MACs=+hmac-sha1,hmac-sha1-96',
    '-o', 'Ciphers=+aes128-cbc,3des-cbc,aes256-cbc',
    '-o', 'LogLevel=ERROR', '-o', 'ConnectTimeout=8',
    'root@169.254.0.1',
]

BASE = 0x88000000
R3 = 0x94000000          # ring3 PA: SMC liveness log + GIC census
SLOTS = 28

NAMES = {
    0x1400: 'set_clock: after CEN',
    0x1500: 'set_ios: after set_clock',
    0x1600: 'set_ios: before init_stream',
    0x1700: 'set_ios: after init_stream',
    0x1800: 'set_ios: done',
    0x2100: 'init_stream: entry',
    0x2200: 'init_stream: after IE write',
    0x2300: 'init_stream: after CON write',
    0x2400: 'init_stream: after CMD write',
    0x4100: 'mmc_irq: entry',
    0x5000: 'mmc probe: entry',
    0xa001: 'mmc_rescan: entry',
    0xa002: 'mmc_rescan: after freq loop',
    0xa003: 'mmc_rescan: after release_host',
    0xa004: 'mmc_rescan: out',
    0xa300: 'mmc_attach_mmc: entry',
    0xa301: 'mmc_attach_mmc: after send_op_cond',
    0xa400: 'mmc_power_off: entry',
    0xb001: 'hsmmc rpm_suspend: entry',
    0xb002: 'hsmmc rpm_suspend: after context_save',
    0xb003: 'hsmmc rpm_suspend: before spin_lock',
    0xb004: 'hsmmc rpm_suspend: lock acquired',
    0xb005: 'hsmmc rpm_suspend: before unlock',
    0xb006: 'hsmmc rpm_suspend: unlocked',
    0xb100: 'rpm_callback: before cb',
    0xb101: 'rpm_callback: after cb',
    0xb102: 'rpm_callback: before lock_irq',
    0xb103: 'rpm_callback: lock_irq done',
    0xb110: 'rpm_suspend: callback returned',
    0xb111: 'rpm_suspend: before suspend_suppliers',
    0xb112: 'rpm_suspend: after suspend_suppliers',
    0xb113: 'rpm_suspend: out',
    0xb114: 'rpm_suspend: before rpm_idle(parent)',
    0xb115: 'rpm_suspend: after rpm_idle(parent)',
    0xb011: 'hsmmc rpm_resume: entry',
    0xb012: 'hsmmc rpm_resume: after context_restore',
    0xb021: 'sysc rpm_suspend: entry',
    0xb022: 'sysc rpm_suspend: after disable_module',
    0xb023: 'sysc rpm_suspend: after clocks',
    0xb031: 'sysc rpm_resume: entry',
    0xb032: 'sysc rpm_resume: after clocks',
    0x6001: 'prepare_namespace: entry',
    0x6002: 'prepare_namespace: after wait_for_device_probe',
    0x6003: 'prepare_namespace: after mount_root',
    0x6004: 'prepare_namespace: out',
    0x7001: 'wait_for_device_probe: entry',
    0x7002: 'wait_for_device_probe: after deferred flush',
    0x7003: 'wait_for_device_probe: after probe_count wait',
    0x7004: 'wait_for_device_probe: after async sync',
    0x7101: 'async_synchronize_full: entry',
    0x7102: 'async_synchronize_full: exit',
}


def read_words():
    w = {}
    for addr, ln in ((BASE, 0xec), (R3, 0x80)):
        out = subprocess.run(SSH + ['on -C 0 /accounts/devuser/memdump3 %08x 0x%x' % (addr, ln)],
                             capture_output=True, check=True).stdout.decode()
        for line in out.splitlines():
            m = re.match(r'\s*([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})', line)
            if m:
                w[int(m.group(1), 16)] = int(m.group(2), 16)
    return w


def read_file(path):
    w = {}
    for line in open(path):
        m = re.match(r'\s*([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})', line)
        if m:
            w[int(m.group(1), 16)] = int(m.group(2), 16)
    return w


def resolve(ptr):
    if not ptr:
        return ''
    smap = SYSTEMMAP
    best = None
    try:
        for line in open(smap):
            parts = line.split()
            if len(parts) < 3:
                continue
            try:
                a = int(parts[0], 16)
            except ValueError:
                continue
            if a <= ptr:
                best = (a, parts[2])
            else:
                break
    except OSError:
        return ''
    return ('%s+0x%x' % (best[1], ptr - best[0])) if best else ''


def decode_word(v):
    code, jf = (v >> 16) & 0xffff, v & 0xffff
    if code in NAMES:
        name = NAMES[code]
    elif (code & 0xff00) == 0x1100:
        name = 'set_ios: entry pm=%d' % (code & 0xff)
    elif (code & 0xff00) == 0x3100:
        name = 'request: CMD%d' % (code & 0xff)
    elif (code & 0xff00) == 0x4200:
        name = 'mmc_irq: after STAT read (stat>>16)=0x%02x' % (code & 0xff)
    elif (code & 0xff00) == 0x2500:
        name = 'init_stream: first STAT read, CC=%d' % (code & 0xff)
    elif (code & 0xff00) == 0x2600:
        name = 'init_stream: poll done, reg=%d' % (code & 0xff)
    elif code == 0x8000:
        name = 'initcall'
    elif (code & 0xff00) == 0x8100:
        name = 'initcall level %d start' % (code & 0xff)
    elif (code & 0xff00) == 0x8200:
        name = 'initcall level %d end' % (code & 0xff)
    elif (code & 0xff00) == 0x7200:
        name = 'async pending count=%d' % (code & 0xff)
    elif (code & 0xf000) == 0xe000:
        name = 'GIC enter IRQ %d' % (code & 0x3ff)
    elif (code & 0xf000) == 0xd000:
        name = 'GIC done  IRQ %d' % (code & 0x3ff)
    else:
        name = 'code 0x%04x' % code
    return name, ((jf - 0x8ad0) & 0xffff) * 10


def ticks_ms(t):
    return t * 1000 // 32768


def decode_epoch(w):
    """Chainloader epoch-test block at +0x88 (loader-owned; the kernel never
    writes this gap, see include/linux/pb_event.h and debug/omap4bc.S)."""
    magic = w.get(BASE + 0x88, 0)
    st = w.get(BASE + 0x8c, 0)
    start = w.get(BASE + 0x90, 0)
    last = w.get(BASE + 0x94, 0)
    el = w.get(BASE + 0x98, 0)
    it = w.get(BASE + 0x9c, 0)
    tgt = w.get(BASE + 0xa0, 0)
    ldr = w.get(BASE + 0xa4, 0)
    cl = w.get(BASE + 0xa8, 0)
    s32 = w.get(BASE + 0xb0, 0)
    names = ('CM_MPU_CLKSTCTRL', 'CM_MPU_MPU_CLKCTRL', 'PM_MPU_PWRSTST',
             'CM_IDLEST_DPLL_MPU')

    if magic != 0x48435045:
        print('epoch block: absent (magic %08x)' % magic)
        return
    print('epoch block: EPCH status=0x%03x%s%s%s%s%s target=%d ticks (%d ms)' % (
        st,
        ' [timebase]' if st & 0x1 else ' [NO-TIMEBASE]',
        ' [ANOMALY]' if st & 0x2 else '',
        ' [done]' if st & 0x4 else ' [INCOMPLETE]',
        ' [sampled]' if st & 0x8 else ' [no-sample]',
        ' [delay-build]' if st & 0x100 else ' [no-delay]',
        tgt, ticks_ms(tgt)))
    if st & 0x10:
        print('epoch block: 32k=%08x (%d ms after power-on epoch)' % (s32, ticks_ms(s32)))
    decode_mpu_state(w)
    if not st & 0x8:
        return
    print('epoch block: LDR=%08x period=%d ms  cl-entry=%08x  handover=%d ms' % (
        ldr, ticks_ms(0x100000000 - ldr), cl, ticks_ms((cl - ldr) & 0xffffffff)))
    print('epoch block: CRR-at-entry=%08x  pre-kernel=%d ms  (this loader=%d ms)' % (
        start, ticks_ms((start - ldr) & 0xffffffff),
        ticks_ms((start - cl) & 0xffffffff)))
    if not st & 0x100:
        return
    if not tgt:
        return
    print('epoch block: loop start=%08x last=%08x elapsed=%d ticks (%d ms) iters=%d' % (
        start, last, el, ticks_ms(el), it))
    if st & 0x4:
        print('epoch block: delay completed; kernel entered %d ms after loop start'
              % ticks_ms(el))
    else:
        print('epoch block: DELAY DID NOT COMPLETE -> CPU stopped %d ms into it'
              % ticks_ms(el))


def decode_mpu_state(w):
    """Kernel-side MPU domain sampler (irq-gic.c), ring +0xb4..+0xe8."""
    names = ('CM_MPU_CLKSTCTRL', 'CM_MPU_MPU_CLKCTRL', 'PM_MPU_PWRSTST',
             'CM_IDLEST_DPLL_MPU')
    off = 0xb4
    if w.get(BASE + off + 0x2c, 0) != 0x50425354:
        print('mpu state: absent')
        return
    last = [w.get(BASE + off + i * 4, 0) for i in range(4)]
    first = [w.get(BASE + off + 0x10 + i * 4, 0) for i in range(4)]
    n = w.get(BASE + off + 0x20, 0)
    chg = w.get(BASE + off + 0x24, 0)
    chj = w.get(BASE + off + 0x28, 0)
    k0 = w.get(BASE + off + 0x30, 0)
    k1 = w.get(BASE + off + 0x34, 0)
    print('mpu state: %d samples, %d changed, last change jiffies 0x%04x' % (n, chg, chj))
    for i, nm in enumerate(names):
        flag = '' if first[i] == last[i] else '   <-- CHANGED'
        print('mpu state: %-18s first=%08x last=%08x%s' % (nm, first[i], last[i], flag))
    if k0 and k1:
        print('mpu state: 32k first=%s ms last=%s ms  -> last heartbeat at %d ms '
              'after the boot epoch' % (ticks_ms(k0), ticks_ms(k1), ticks_ms(k1)))


def decode_wdt_diag(w):
    """Loader WDT2 write diagnostic at ring3 +0xb0."""
    B = R3 + 0xb0
    if w.get(B + 0x00, 0) != 0x54445750:
        print('wdt diag: absent')
        return
    clk0, clk1 = w.get(B + 0x04, 0), w.get(B + 0x08, 0)
    ldr0, ldr1 = w.get(B + 0x0c, 0), w.get(B + 0x10, 0)
    tgr0, tgr1 = w.get(B + 0x14, 0), w.get(B + 0x18, 0)
    wps1, wps2 = w.get(B + 0x1c, 0), w.get(B + 0x2c, 0)
    crr0, crr1, crr2 = (w.get(B + 0x20, 0), w.get(B + 0x24, 0),
                        w.get(B + 0x28, 0))
    print('wdt diag: CM_WKUP_WDT2_CLKCTRL %08x -> %08x  (MODULEMODE=%d, IDLEST=%d)'
          % (clk0, clk1, clk1 & 3, (clk1 >> 16) & 3))
    print('wdt diag: LDR %08x -> %08x %s' %
          (ldr0, ldr1, 'WRITE LANDED' if ldr0 != ldr1 else 'WRITE DROPPED'))
    print('wdt diag: TGR %08x -> %08x %s' %
          (tgr0, tgr1, 'WRITE LANDED' if tgr0 != tgr1 else 'WRITE DROPPED'))
    print('wdt diag: WPS after LDR=%08x after kick=%08x (0 = posted)' % (wps1, wps2))
    print('wdt diag: CRR %08x -> %08x -> %08x  (kick %s)' %
          (crr0, crr1, crr2,
           'RELOADED' if (crr1 - crr0) & 0x80000000 else 'did not reload'))


def decode_probe(w):
    """ring3 +0x00..: SMC liveness log (irq-gic.c) and GIC census."""
    if w.get(R3 + 0x00, 0) != 0x434d5350:
        print('smc probe: absent (ring3 +0x00 = %08x)' % w.get(R3 + 0x00, 0))
        return
    mask = w.get(R3 + 0x04, 0)
    print('smc probe: magic PSMC, completed mask=0x%x' % mask)
    names = ('300ms', '800ms', '1300ms', '2000ms')
    for i in range(4):
        a = w.get(R3 + 0x08 + i * 12, 0)
        ret = w.get(R3 + 0x08 + i * 12 + 4, 0)
        rj = w.get(R3 + 0x08 + i * 12 + 8, 0)
        if not a:
            print('smc probe: %-7s not attempted' % names[i])
            continue
        tj = ((a >> 16) - 0x8ad0) & 0xffff
        if not (mask & (1 << i)):
            print('smc probe: %-7s fired at %d ms -> NEVER RETURNED (CPU died in the SMC)'
                  % (names[i], tj * 10))
        else:
            print('smc probe: %-7s fired at %d ms, select_kek ret=%d (0x%08x), '
                  'returned at %d ms' % (names[i], tj * 10, ret, ret,
                                         (((rj - 0x8ad0) & 0xffff) * 10) if rj else 0))

    if w.get(R3 + 0x38, 0) != 0x43494750:
        print('gic census: absent')
        return
    ctlr = w.get(R3 + 0x3c, 0); ctr = w.get(R3 + 0x40, 0)
    grp = [w.get(R3 + 0x44 + i * 4, 0) for i in range(4)]
    en = [w.get(R3 + 0x54 + i * 4, 0) for i in range(4)]
    act = w.get(R3 + 0x64, 0); pend = w.get(R3 + 0x68, 0)
    g0 = sum(bin(x).count('1') for x in grp)
    print('gic census: CTLR=%08x TYPER=%08x (max IRQ %d)' %
          (ctlr, ctr, (ctr & 0x1f) * 32 + 32))
    print('gic census: IGROUPR=%08x %08x %08x %08x  (%d bits set; 1=Group0/secure)' %
          (grp[0], grp[1], grp[2], grp[3], g0))
    print('gic census: ISENABLER=%08x %08x %08x %08x' % tuple(en))
    print('gic census: ISACTIVER0=%08x ISPENDR0=%08x' % (act, pend))
    print('l4sec prep: CLKSTCTRL=%08x  CM_MPU_STATICDEP=%08x' %
          (w.get(R3 + 0x6c, 0), w.get(R3 + 0x70, 0)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--raw')
    a = ap.parse_args()
    w = read_file(a.raw) if a.raw else read_words()

    magic = w.get(BASE + 0x08, 0)
    hb = w.get(BASE + 0x00, 0)
    cnt = w.get(BASE + 0x0c, 0)
    print('magic=%08x (%s)  count=%d' %
          (magic, 'PBEV' if magic == 0x50424556 else 'NOT INITIALISED', cnt))
    if (hb >> 16) == 0x3700:
        print('heartbeat: last tick at %d ms (jiffies low16 0x%04x, INITIAL_JIFFIES 0x8ad0)' % ((((hb & 0xffff) - 0x8ad0) & 0xffff) * 10, hb & 0xffff))
    else:
        print('heartbeat: no tick recorded (%08x)' % hb)

    ptr = w.get(BASE + 0x04, 0)
    print('ptr(+0x04, last initcall / async func) = 0x%08x %s' % (ptr, resolve(ptr)))

    decode_epoch(w)
    decode_probe(w)
    decode_wdt_diag(w)

    slots = [w.get(BASE + 0x10 + i * 4, 0) for i in range(SLOTS)]
    first = max(0, cnt - SLOTS)
    print('last %d events:' % (cnt - first))
    prev = None
    for i in range(first, cnt):
        name, ms = decode_word(slots[i % SLOTS])
        gap = '' if prev is None else '  (+%d ms)' % (ms - prev)
        print('  %6d ms  %s%s' % (ms, name, gap))
        prev = ms


if __name__ == '__main__':
    main()
