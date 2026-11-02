# 108 — RE: `omap4430-wdtkick`, the TWL6030 watchdog, `winchester-bootreason`, `.script`

> **Session 4 correction (`109-epoch-test-2026-10-27.md`).**  Two things below
> are superseded:
> 1. The §6.3 **epoch test has been run**.  Result: the wall is
>    **kernel-entry relative**, not a hardware/power epoch — a calibrated
>    1000 ms pre-kernel delay moved kernel entry by exactly +1000 ms and left
>    the halt time in kernel time unchanged (2240 → 2240 ms, jitter ≤ 10 ms).
>    So the PMIC/AON-counter family is out for this wall, and report 106's
>    PRCM/VC/VP state is the line to follow.
> 2. On the **chainloader/Linux** path WDT2 is armed by the bootloader with
>    `LDR = 0xfffc1800` → **7.812 s** (the `0xffe2b400`/58.6 s quoted below is
>    the *live QNX* value).  It is re-armed 7 ms before the chainloader starts,
>    so a test image on media reset-loops every ~7.8 s.  The topology argument
>    in §6.1 still holds (the rails stay up and the WDT2 reset does happen);
>    only the period changes.

Static analysis only (no execution, no device access). All binaries are
QNX/ARM ELF32 (ET_DYN, `/usr/lib/ldqnx.so.2`, stripped) from
`/home/psyden/rea-work/playbook-dev/dumped4869ifs/proc/boot/`.
Method: `rizin 0.8.2` (`asm.bits=32`, `aa`), cross-checked with
`arm-linux-objdump -D -b binary -m arm --adjust-vma=0` on the RX LOAD
segment; string/rodata decoding by hand against the GOT base (`0x4000`).

---

## 0. TL;DR

| Goal | Answer |
|---|---|
| 1. `omap4430-wdtkick` | Fully recovered. **One** WDT register is ever written: `base+0x30` (TGR), read→`MVN`→write back. `base+0x48` is read once as an ID ("watchdog started") check against `0x4444`. It never enables, disables, presets or reloads WDT2. Kick period = `-t` ms, re-armed from a one-shot `CLOCK_SOFTTIME` timer, pulse code **126**. `-a`/`-l` default to the QNX syspage HWInfo device record `wdt` (i.e. WDT2 @ `0x4A314000`). `-e` is **only** an "on ID mismatch, don't keep running" switch. The TWL kick is a single 2-byte write `{0x2C, seconds}` to slave `0x48`, no read-back, no verify, no retry, **no action/control register, no `DEV_ON`, no `DEV_GROUP`, no `PWR_CTRL`**. |
| 2. TWL `0x2C` | `0x2C` on slave `0x48` = **`TWL6030_BASEADD_PM_MASTER` (`0x1F`) + `0x0D`** → PM-Master/PHOENIX register **`0x0D`** = **`PRIMARY_WATCHDOG_CFG`**: a 7-bit **timeout in seconds** (0…127, `0x7F` = 127 s = max). Writing it **restarts/reloads the counter** — that *is* the kick. It is **enabled by default in TWL6030B1 hardware** and its expiry drives the PMIC into the **WAIT‑ON/OFF** power state (rails down), not an IRQ. Nothing else in the QNX PMIC stack touches `0x2C`/`0x2D`/`0x2E`. |
| 3. `winchester-bootreason` | Reads TWL `0x48/0x1F` = **`PHOENIX_START_CONDITION`** (PM_MASTER `+0x00`) and `0x48/0x10` (bit 2 = "restart to charge" latch, cleared by write-back `&0xFB`). 9 reason codes / exit codes. Holds the boot on plug-detect and re-checks `0x10` bit 2 after. **No boot counter in the PMIC** — the counter (`BOOTCTR_FCTRY`) lives in NVRAM. |
| 4. `.script` | Record format decoded; **50** records (1 loader/`libc`, 11 display, 13 `waitfor`, 1 wait-target, 24 process records). Only `setup-core-inactive full` precedes the FIRST banner; the watchdog + PMIC work happens *after* `i2c-omap35xx-omap4 ×3` are up. Nothing in the QNX script happens before ~1 s except `setup-core-inactive`, `resource_seed`, `devc-seromap`, `slogger`, `pipe`, `trustzone-omap4`. |
| 5. Is the PMIC the 1.4 s killer? | **Almost certainly no** — two independent quantitative arguments (§6.1). But it is a *real, latent* Linux bug: on TWL6030B1 the primary watchdog runs by default from `NRESPWRON` release, so Linux must re-program `0x48/0x2C` at least once per 127 s or the PMIC will drop the rails at an unpredictable moment. §6.2 gives the exact neutralisation sequence, §6.3 the two decisive experiments. |

---

## 1. `omap4430-wdtkick` — complete recovery

Entry `0x1050`, `main` @ `0x1220` (2060 B). GOT base = `0x4000`.
`getopt` string @ `0x29f4` = **`a:d:l:p:t:T:PevU:`**

### 1.1 Options (exact)

| opt | goes to | stack slot | default | meaning (from `QNX_usage` @ `0x32cf`) |
|---|---|---|---|---|
| `-a addr` | `var_e0h` | `sp+0x10` | HWInfo | WDT register physical base |
| `-l len` | register `sl` | — | HWInfo | WDOG register window size |
| `-p prio` | `var_d0h` | `sp+0x20` | **10** (`0x12d8`) | priority of the watchdog event |
| `-t msec` | `var_d4h` | `sp+0x1c` | HWInfo | kick period, **milliseconds** |
| `-T sec` | register `sb` | — | **127** (`0x12b8: mov r9,#0x7f`) | TWL watchdog timeout |
| `-P` | `var_c8h` | `sp+0x28` | 0 | also kick the TWL6030 primary watchdog |
| `-e` | `var_cch` | `sp+0x24` | 0 | "Terminate the Watchdog Time Module" |
| `-v` | `var_dch++` | `sp+0x14` | 0 | verbose (slogf dump) |
| `-U user` | `strdup` → `fp` | — | NULL | drop to uid[:gid,...] after setup |
| `-d dev` | `var_c4h` | `sp+0x2c` | `0x29e8` = **`/dev/i2c0`** | TWL I²C device |

The switch table is a 39-entry ARM branch table at `0x1300` (base
`0x12f8`, `addls pc,pc,r0,lsl#2`, entries are `b` instructions relative to
`0x1300`), so only `P T U a d e l p t v` differ from `default → 0x148c`.
Verified with `objdump` on the raw bytes:

```
123c  ldr  r0,[pc,#2028]   @0x1a30   ; GOT-relative "/dev/i2c0"
125c  add  r1, sp, #48                ; &hwinfo_out  (6 words, zeroed)
1260  bl   0x1ae4                     ; hwinfo_fill(handle, &out)
126c  ldr  sl, [sp,#52]               ; out[1] = WDT window size
1278  ldr  r3, [sp,#56]               ; out[2] = WDT physical base
1280  orrs r2, r3, r2                 ; out[2]|out[3] must be non-zero
128c  mov  r0, r6 ; 1294 bl 0x1c6c    ; default kick ms = inputclk[1]/inputclk[2]
12b8  mov  r9, #127                   ; -T default
```

### 1.2 Defaults come from the QNX syspage HWInfo, not from constants

`0x1a8c` matches the syspage `Device` name against `"wdt"`; `0x1ae4`
walks the HWInfo attribute list (`0x1df4` = find-by-name with an occurrence
counter, `0x24a8`/`0x2418` = nth-item / atoi) and fills:

| out[] | HWInfo key (rodata) | used as |
|---|---|---|
| `+0x00…+0x14` | `"location"` (`0x2e6c`) | raw copy; `+0x04` → **size**, `+0x08` → **base** |
| `+0x18` | `"drvr_dll"` (`0x2e78`) | atoi (unused) |
| `+0x1c` | `"optstr"` (`0x2e84`) | atoi (unused) |
| `+0x20` | `"irq"` (`0x2ea0`) | atoi (unused) |
| `+0x24` | `"errata"` (`0x2e8c`) | atoi (unused) |
| `+0x28` | `"inputclk"` (`0x2e94`) | `inputclk[1] / inputclk[2]` → **default `-t`** |

If the lookup fails, `base = 0`, `size = -1`, `kick = -1`, and the tool
aborts on the first of the three validity checks:

```
0x14a4  ldr r2,[sp,#16] ; cmp r2,0   → "Invalid WDT register physics based address..."
0x14d0  cmp sl,0                     → "Invalid  WDT registers size..."
0x14f8  ldr r1,[sp,#28]; cmn r1,1    → "Invalid default time for watchdog timer kick..."
```

Because the boot script passes no `-a`/`-l`, the runtime base on the
PlayBook is the HWInfo value, and report 105's live read
(`0x4a314010 SYS_CONFIG=0x20 … 0x4a314030 TGR=0xffffd47b`) confirms it is
**OMAP4430 WDT2, physical base `0x4A314000`, size `0x1000`**.

`-T` is clamped: `0x1524  ldr r8,[sp,#0x28] ; cmp sb,#0x7f ; andhi r3,r8,1`
→ if `-T > 127` **and** `-P` was given, print *"Invalid time for pmic
watchdog timeout - using max timeout instead"* and force `0x7F`.

### 1.3 Start-up sequence (addresses are vaddrs)

```
1220  push {r4-r8,sb,sl,fp,lr} ; sub sp,#0xcc
124c  … hwinfo lookup (see 1.2) …
12b0  fp=0 ; [sp+0x28]=0 (-P) ; r9=127 (-T) ; [sp+0x1c]=-1 ; [sp+0x30]=10 ; …
12ec  getopt loop
1558  ThreadCtl(_NTO_TCTL_IO /*1*/, 0)                    -> "ThreadCtl failed"
158c  ChannelCreate(0)                                   -> "ChannelCreate() failed"
15d0  ConnectAttach(0,0,chid,_NTO_SIDE_CHANNEL /*0x40000000*/,0)
1600  sigevent {type=SIGEV_PULSE(4), coid, 0, code=126, priority=-p}
162c  TimerCreate(CLOCK_SOFTTIME /*2*/, &sev)             -> "TimerCreate() failed"
1690  if (-P) {                                   /* r8 = -P */
16a0      fd = open(path /*-d*/, O_RDWR)                  -> "PMIC I2C device open failed"
16d8      v = 100000;  devctl(fd, 0x80040502, &v, 4, 0)   /* i2c set bus speed = 100 kHz */
                                                          -> "PMIC I2C speed devctl failed. Continuing anyways."
      }
1720  if (verbose) slogf("Paddr = 0x%x, size = 0x%x, kick = %d, Priority = %d, exit = %d", …)
1764  if (getprio(0) != -p) setprio(0,-p)                 -> "can't change priority"
17a0  r7 = mmap_device_io(sl /*size*/, base)              -> "Failed to map WDT registers"
17dc  if ((*(u16*)(r7+0x48)) != 0x4444) {                 /* uxth, movw r3,#0x4444 */
17f4      slog("WDT is disabled now")
180c      if (-e) { slog("Terminate WDT Module."); TimerDestroy(t); goto unmap; }
      }
183c  procmgr_daemon(0, 6)       /* PROCMGR_DAEMON_NOCHDIR|PROCMGR_DAEMON_NOREDIRECT */
1870  if (-U) drop_privs(fp)                              -> "unable to run as specified user"
18a8  itime = { .nsec = t_ms * 1000000, .interval = 0 }   /* smull by 0xF4240 */
18d8  TimerSettime(timerid, 0, &itime, NULL)              /* one-shot */
```

### 1.4 The steady-state loop, and the exact per-kick sequence

```
1910  rcvid = MsgReceivev(chid, &pulse16, 1, NULL)   /* 16-byte QNX _pulse */
192c  if (pulse.code /* offset +4, ldrsb */ == 126) {
1938      r3 = r7 + 0x30
193c      r2 = *(u32*)(r7+0x30)        ; TGR
1940      r2 = ~r2                    ; MVN
1944      *(u32*)(r7+0x30) = r2       ; write back inverted
1948      if (-P) {
1950          buf[0] = 0x2C
1958          buf[1] = (u8)sb         /* -T, default 127, clamped to 127 */
195c          hdr = { addr=0x48, len=2, nbufs=2, flags=1 }   /* 16 bytes @ sp+0x78 */
1978          iov[0] = { &hdr, 0x10 }
1988          iov[1] = { &buf[0], 1 }
1994          iov[2] = { &buf[1], 1 }
19a4          fd = *(int*)(GOT+0x134)     /* the /dev/i2c0 fd saved at 0x16ac */
19c4          devctlv(fd, 0x80100505, 3, 0, iov, NULL, NULL)
19c8          if (rc != 0) slog("I2C write failed")
      }
19e0      TimerSettime(timerid, 0, &itime, NULL)   /* re-arm, same -t */
1910      loop
  } else if (rcvid) MsgReplyv(rcvid, 1, &msg, 0x30)
```

**That is the whole program.** Consequences worth stating explicitly:

- The OMAP WDT2 is only ever *kicked*, never configured. `base+0x10`
  (SYSCONFIG), `+0x14` (SYSSTATUS), `+0x24` (CNTRL), `+0x28` (CRR),
  `+0x2C` (LDR) and `+0x34` (WPS) are **never read and never written**.
  The reload value (live `0xffe2b400` ⇒ `(0xFFFFFFFF-0xFFE2B400)/32768 =
  58.59 s`) and the "running" state come from the RIM bootloader.
- The TGR kick alternates the register between `TGR` and `~TGR`
  (live `0xffffd47b` → the tool writes `0x00002b84`, next kick writes
  `0xffffd47b`, …), which is the documented "write a *different* value to
  reload" behaviour.
- `WPS` (`+0x34`) is **not** polled before or after the TGR write, so a
  silently-dropped kick is architecturally possible. Irrelevant at the
  configured 15 s interval.
- `base+0x48` is read as a 16-bit value and compared with `0x4444`. That
  is the OMAP WDT **Start/Stop (SPR)** register; `0x4444` in the low half
  means "watchdog started". The tool treats anything else as "WDT is
  disabled now" and (with `-e`) exits instead of kicking a dead timer.
  It never *writes* SPR, so it cannot start a stopped watchdog.
- `-e` has exactly one effect: the early-exit branch above. It does **not**
  stop the periodic kicking, does not disable the WDT, and does not change
  the TWL behaviour.
- The I²C request header is 16 bytes `{addr, len, nbufs, flags}` =
  `{0x48, 2, 2, 1}`; the two payload bytes (register, value) are passed as
  two 1-byte iovecs after the header. The same header shape is used by
  `pmic_twl6030_cfg`'s write helper (`0xa30`), so the field interpretation
  is cross-validated by two independent binaries.
- Bus speed for the TWL kick is set to **100 kHz** once, at start-up.

---

## 2. What TWL6030 register `0x2C` is

### 2.1 Address arithmetic (three independent anchors)

The QNX binaries never name TWL registers, so the mapping must be derived.
It pins down exactly:

1. `winchester-bootreason` reads abs `0x48/0x1F` and calls it
   **`PHOENIX_START_CONDITION`** in its own string table
   (`"Power on due to *UNKNOWN*, PHOENIX_START_CONDITION = 0x%02x"` @ `0x3760`).
2. `include/linux/mfd/twl.h` + `drivers/mfd/twl-core.c` (in this workspace's
   `kernel-clean`):
   ```
   #define TWL6030_BASEADD_PM_MASTER   0x001F    /* subchip/slave 0  0x48 */
   #define TWL6030_PHOENIX_DEV_ON      0x06      /* → absolute 0x25 */
   ```
   `twl6030_power_off()` writes `PHOENIX_DEV_ON` with `APP|CON|MOD_DEVOFF`
   set — i.e. absolute `0x25` is the "turn the rails off" register. The
   well-known mainline value for `TWL6030_PHOENIX_DEV_ON` is indeed `0x25`,
   which confirms the `0x1F` base.
3. TI E2E (see Sources) names the watchdog register and its semantics.

Therefore:

```
absolute 0x1F  = PM_MASTER + 0x00   PHOENIX_START_CONDITION   (bootreason)
absolute 0x25  = PM_MASTER + 0x06   PHOENIX_DEV_ON            (kernel)
absolute 0x2C  = PM_MASTER + 0x0D   PRIMARY_WATCHDOG_CFG      (wdtkick -P)
```

### 2.2 Semantics of `PRIMARY_WATCHDOG_CFG`

From the binary: the value is passed straight through from `-T` (seconds),
default **127**, and anything `>127` is clamped to `0x7F` with the message
*"Invalid time for pmic watchdog timeout - using max timeout instead"*. A
7-bit, seconds-granular timeout with a hard ceiling of 127 s. From TI:

- *"I am programming the PRIMARY_WATCHDOG_CFG register … to set the timeout
  at 127 seconds. The documentation … says that programming this register
  will automatically restart the WDT."* → **the write IS the kick**; there
  is no separate "reload" register and no separate "enable" write.
- *"In TWL6030B1AACMR the Primary watchdog timer is **enabled by default in
  hardware**. Hence, software must program PRIMARY_WATCHDOG_CFG register
  once within every 128 seconds to prevent TWL6030 from entering
  **WAIT-ON/OFF state**."* → the expiry action is a PMIC power-group
  transition (rails down), **not** an interrupt and **not** a clean SoC
  reset.
- *"The primary watchdog counter is initialized with the
  PRIMARY_WATCHDOG_CFG register when NRESPWRON = 0, then the counter starts
  as soon as NRESPWRON is released…"* → the period is **latched at PMIC
  power-on**; the PMIC epoch is the release of `NRESPWRON`, which is *not*
  the SoC reset and *not* kernel entry.

### 2.3 What is *not* touched

Verified by exhaustive immediate scan of all four PMIC binaries plus
`omap4430-wdtkick` (all `mov r1/r2, 0x1X` / `0x2X` immediates, plus every
`bl` to the I²C read/write helpers). Caveat: `pmic-winchester-twl6030`
resolves regulator register addresses from its `regulator_info` tables, so
the scan cannot see those as immediates — but those tables only populate
the regulator banks (`0x50-0x7F`, `0x80-0xAF`, `0xB0-0xCF`) and
`PM_RECEIVER` (`0x30+`), never `0x1F-0x2F`:

- no write to `0x2D`, `0x2E`, `0x2F`;
- no write to `PHOENIX_DEV_ON` (`0x25`), `PWR_CTRL`, `DEV_GROUP`,
  `PHOENIX_DEV_OFF`, `SECONDARY_WATCHDOG`;
- no read-back / verify / retry of the `0x2C` write;
- `pmic-winchester-twl6030`, `pmic-twl6030-ctl` and `pmic_twl6030_cfg`
  **never** touch `0x2C` at all. Only `omap4430-wdtkick -P` does.
- no "disable watchdog" path exists anywhere — nothing in the boot set
  clears the watchdog, it is only fed.

`pmic-twl6030.cfg` has no watchdog entry (report 105 §3.3 is correct).

---

## 3. The rest of the PMIC stack (for completeness)

### 3.1 `pmic_twl6030_cfg` — "Configure power management chip"

Straight-line `main` @ `0xae0`. Write helper `0xa30(addr, reg, val)`:
`hdr={addr,2,2,1}`, `iov={ {&hdr,16}, {&reg,1}, {&val,1} }`,
`devctlv(fd, 0x80100505, 3, 0, iov, 0, 0)`, prints `twl6030_i2c_write fail`
on error. Bus speed set to **100 kHz** (`0x80040502`).

1. Version probe: `DCMD 0xC0140507` (20-byte descriptor, slave **`0x4A`**,
   register `0x87`, 1 byte) → `r5`; `0/1/2` ⇒ print `TWL6030 ES1.0/ES2.0/ES2.1`,
   otherwise `TWL6030 Unknown ver: 0x%02x`.
2. Unconditional writes (`0x0c1c`–`0x0c78`):

| addr | reg | val |
|---|---|---|
| 0x48 | 0xBD | 0x05 |
| 0x48 | 0xBE | 0xE1 |
| 0x48 | 0x98 | 0x01 |
| 0x48 | 0x99 | 0x03 |
| 0x48 | 0x9A | 0x21 |
| 0x48 | 0x9B | 0x15 |

3. Only if `strstr(confstr(_CS_PATH…), "Winchester")` matches
   (`0x0ca8`–`0x0e14`) — i.e. always on this board:

| addr | reg | val | | addr | reg | val |
|---|---|---|---|---|---|---|
| 0x48 | 0x84 | 0x01 | | 0x48 | 0xAD | 0x01 |
| 0x48 | 0x85 | 0x03 | | 0x48 | 0xAE | 0x03 |
| 0x48 | 0x86 | 0x21 | | 0x48 | 0xAF | 0x21 |
| 0x48 | 0x87 | 0x13 | | 0x48 | 0xB0 | 0x01 |
| 0x48 | 0x88 | 0x01 | | 0x48 | 0xB1 | 0x03 |
| 0x48 | 0x89 | 0x03 | | 0x48 | 0xB2 | 0x21 |
| 0x48 | 0x8A | 0x21 | | **0x38** | 0x10 | 0x00 |
| 0x48 | 0x8B | 0x13 | | **0x38** | 0x16 | 0x03 |
| 0x48 | 0x8C | 0x01 | | **0x38** | 0x17 | 0xFF |
| 0x48 | 0x8D | 0x03 | | **0x38** | 0x1D | 0x01 |
| 0x48 | 0x8E | 0x21 | | **0x38** | 0x70 | 0x00 |
| 0x48 | 0x8F | 0x13 | | | | |

   (`0x38` is a third I²C slave, not named by the binary.)
4. Always, finally (`0x0e18`–`0x0ea4`):

| addr | reg | val |
|---|---|---|
| 0x48 | 0xF5 | 0x01 |
| 0x48 | 0xE5 | 0x10 |
| 0x48 | 0xA3 | 0x18 |
| 0x48 | 0xA1 | 0x07 |
| 0x48 | 0xA2 | 0xE1 |
| 0x48 | 0xFA | 0x01 |
| 0x49 | 0x07 | 0xFF |
| 0x49 | 0x06 | 0x05 |
| 0x49 | 0x04 | 0x05 |

Then `close(fd); exit(0)`. Note: 60+ individual register writes at 100 kHz
— a few tens of ms of I²C traffic, one of the reasons QNX's 1.5 s point
arrives after a lot of work.

### 3.2 `pmic-winchester-twl6030` — the regulator resmgr

`twl6030_devctl` @ `0x15d8` (95 blocks), `main` @ `0x1ea0`,
`twl6030_read` @ `0x2988`. Implements `DCMD 0x80045001/2/3`,
`0xC0085004/5`, `0xC0105006`, `0xC0145007`, `0x80095008` on
`/dev/pmic/regulator`. Works entirely inside the regulator banks
(`0x50-0x7F`, `0x80-0xAF`, `0xB0-0xCF`, `PM_RECEIVER` `0x30+`) and the
config registers `CFG_LDO_PD1/PD2`, `CFG_SMPS_PD`, `SMPS_OFFSET`,
`SMPS_MULT`, `PREQ_MASK`. Regulator name table @ `0x76bc`:
`vana vaux1 vaux2 vaux3 vcxio vdac vmmc vrtc vusb vusim regen1 regen2
v1v29 v1v8 v2v1 vmem vcore1 vcore2 vcore3`.
**No access to `0x1F`–`0x2F`.** DVT3+ remaps `regen1`/`regen2` onto
GPIO 2/15 (`Detected MBID=5+, regen1 and regen2 GPIO controlled`).

### 3.3 `pmic-twl6030-ctl` + the `.cfg` files

A script interpreter over `/dev/pmic/regulator` (commands
`get_/set_groups|trans|state|vout|pd`, `(un)mask_preq`, `lock_offset`, …)
plus a powerman client (`-p list`, PPS `system/power/mgr`, `ldo_%s`
objects). `pmic-twl6030*.cfg` (dumped and read in full) do:

```
set_pd vpp off
set_state vpp|vmmc|vusim off app con mod
set_trans vaux1|vaux2|vaux3|vusb sleep=ams act=ams
set_groups vmmc|vpp|vusim            (detach from all groups)
set_groups regen1|regen2 app         (DVT2 only: MACHINE Winchester:3-)
set_state regen1|regen2 on app
unmask_preq 1
```

`unmask_preq 1` is the notable one: it *enables* PREQ1-driven
ACTIVE↔SLEEP group transitions. That is a real "the PMIC can change the
rails on its own" mechanism — but it is event-driven (PREQ1 from the OMAP
PRCM), not a fixed timer, and report 105 §2.3 killed the idle/PREQ theory
(`idle=poll`, `cpuidle.off=1`, `nohz=off` still die at ~1.5 s).

---

## 4. `winchester-bootreason`

`main` @ `0x1ec0`. Options `fb:i:chs` (`0x3440`), all parsed with
`strtoul`; defaults: I²C speed **400 kHz**, PMIC IRQ **39 (0x27)**.
Opens `/dev/i2c0` and `/dev/i2c3` (`0x344c`, `0x3458`), `InterruptAttachEvent`
on the PHOENIX IRQ, `mmap_device_io` for the LED controller.

### 4.1 Registers read/written

| addr | reg | access | name |
|---|---|---|---|
| 0x48 | **0x1F** | read (`0x15ac(fd,0x48,0x1F,&b)`) | `PHOENIX_START_CONDITION` (PM_MASTER+0x00) |
| 0x48 | **0x10** | read | power-on/restart latch (block unidentified; bit 2 = "restart to charge") |
| 0x48 | 0x1F | write back the same byte (only with `-c`) | acknowledge |
| 0x48 | 0x10 | write `value & 0xFB` (only with `-c`) | clear bit 2 |

Write helper `0x16b0(fd, addr, reg, val)` — same 2-byte `{reg,val}`
transaction as `omap4430-wdtkick`, i.e. **the same mechanism, no watchdog
register involved**.

### 4.2 Reason decode (`PHOENIX_START_CONDITION`, `0x48/0x1F`)

| bit | message | exit code |
|---|---|---|
| 0x01 | Power on due to **button** | 57 (`0x39`) |
| 0x02 | Power on due to **RPWRON** | 58 (`0x3A`) |
| 0x04 | Power on due to **USB ID** | 59 (`0x3B`) |
| 0x08 | Power on due to **plug detect** | 60 (`0x3C`) |
| 0x10 | Power on due to **RTC** | 61 (`0x3D`) |
| 0x20 | Power on due to **battery insertion** | 62 (`0x3E`) |
| 0x40 | Power on due to **battery bounce** | 63 (`0x3F`) |
| ==0 | Power on condition **previously cleared** | 56 (`0x38`) |
| else | `*UNKNOWN*, PHOENIX_START_CONDITION = 0x%02x` | 56 (`0x38`) |

Pre-checked *before* the chain:

| test | message | exit code |
|---|---|---|
| `0x10` bit 2 (`0x04`) | Power on due to **reboot to charge** | 55 (`0x37`) |
| `0x1F` bit 3 or `0x10` bit 2 | Power on due to **charger – holding boot** | (holds, then falls through) |

### 4.3 Hold-boot logic

If `0x1F` bit 3 or `0x10` bit 2: print *"Power on due to charger - holding
boot"*, `InterruptAttachEvent` on the PMIC IRQ, then loop on
`InterruptWait` + I²C reads of `0x48/0x10` and the PPS objects
`/pps/services/power/usbconnector` (`UsbConnector/IdPin/Vbus`) and
`/pps/services/power/charger` (`ChargerInfo/ChargeCurrent`) to classify
`None / TypeA / TypeBConnectedCharger / TypeBConnectedHost`, printing
*"Detected 12V charger, holding boot"*, *"Detected USB charger, holding
boot"*, *"Connected to host, continue boot"*, *"Charger not present,
powering off"*, *"Detected power button release, resume boot"* or
*"Power on due to reboot to charge"*.

`-f` forces the hold-boot condition (*"Faking holdboot condition"*),
`-h` enables hold boot, `-c` clears the latched reason, `-s` skips LED
init, `-b`/`-i` set speed/IRQ.

### 4.4 Boot-reason / counter / timeout notions

- **Power-on reason: YES** — `PHOENIX_START_CONDITION` (`0x48/0x1F`), 7
  documented bits + one latch in `0x10`.
- **Timeout: YES** — the PMIC primary watchdog (`0x48/0x2C`).
- **Boot counter: NO** — nothing in the PMIC set counts boots. The counter
  used by the restore flow (`BOOTCTR_FCTRY`) is **not** in the PMIC, and it is
  **not** in NVRAM either: it is an **RPMB record**, read through
  `rpmb-omap4`'s `/dev/rpmb/<name>` namespace (which is why every read of it
  has always been `/dev/rpmb/BOOTCTR_FCTRY`). NVRAM
  (`nvram_resource_manager` → `/dev/nvram`, a region of `hd0`) is a *different*
  store — boot flags and MAC/GUID — and has nothing to do with RPMB. See
  report `116-rpmb-bootctr-plan-2026-10-27.md` for what maintaining it from
  Linux requires.

`splash_script` consumes the exit code:
`rc == 60 || rc == 55` → `winch_lcdctl -b power_off` (charge-only mode);
otherwise → `winch_lcdctl -s splash`.

---

## 5. `.script` — decoded

3984 bytes, tag-length-value records: `u16 total_size | u16 flags`, then
for process records `u8 argc`, the command name twice, NUL-separated args,
then an optional empty string + `KEY=VALUE` environment block.
`flags`: `0x0004` loader/`libc`, `0x0003` display text, `0x0002`
wait-target (secondary), `0x0001` **waitfor path**, `0x0000` start process.

| # | flags | content |
|---|---|---|
| 1 | 0x0004 | `../../proc/boot/libc.so.3`, `/usr/lib/ldqnx.so.2` (loader) |
| 2 | 0x0003 | display `Welcome to QNX Neutrino Trunk on the TI OMAP4430 … Winchester Board` |
| 3 | 0x0003 | display `SETUP-CORE-INACTIVE` |
| 4 | 0x0000 | **`setup-core-inactive full`** (6 words of HWInfo out, no waitfor) |
| 5 | 0x0000 | `resource_seed dma=0,31` |
| 6 | 0x0000 | `devc-seromap -o devperm=0600 -e -F -b115200 -c48000000/16 0x48020000^2,106` |
| 7 | 0x0001 | waitfor **`/dev/ser1`** |
| 8 | 0x0002 | wait-target `/dev/ser1` |
| 9 | 0x0000 | `slogger -U 99:99 -s2048k` |
| 10 | 0x0000 | `pipe -U 99:99` |
| 11 | 0x0003 | display `starting trustzone-omap4` |
| 12 | 0x0000 | `trustzone-omap4 -U99:102` |
| 13 | 0x0003 | display `starting I2C driver...` |
| 14 | 0x0000 | `i2c-omap35xx-omap4 -p 0x48070000 -i 88 -l 420 -f -u 0 -U 99:99` |
| 15 | 0x0001 | waitfor **`/dev/i2c0`** |
| 16 | 0x0000 | `i2c-omap35xx-omap4 -p 0x48072000 -i 89 -l 420 -f -u 1 -U 99:99` |
| 17 | 0x0001 | waitfor **`/dev/i2c1`** |
| 18 | 0x0000 | `i2c-omap35xx-omap4 -p 0x48350000 -i 94 -l 420 -f -u 3 -U 99:99` |
| 19 | 0x0001 | waitfor **`/dev/i2c3`** |
| 20 | 0x0003 | display `starting Watchdog` |
| 21 | 0x0000 | **`omap4430-wdtkick -t 15000 -P -U 99:99`** (6 args, no waitfor) |
| 22 | 0x0003 | display `Configure power management chip` |
| 23 | 0x0000 | `pmic_twl6030_cfg` |
| 24 | 0x0003 | display `Take touch pad out of reset` |
| 25 | 0x0003 | display `Start PMIC driver...` |
| 26 | 0x0000 | `pmic-winchester-twl6030 -U 99:99` |
| 27 | 0x0001 | waitfor **`/dev/pmic/regulator`** |
| 28 | 0x0003 | display `Initialize regulators` |
| 29 | 0x0000 | `sh /proc/boot/pmic-twl6030-ctl_script` |
| 30 | 0x0001 | waitfor **`/dev/trustzone`** |
| 31 | 0x0000 | `random -p -l entropy.so:10:300000:256` |
| 32 | 0x0001 | waitfor **`/dev/random`** |
| 33 | 0x0003 | display `Starting MMC/SD driver...` |
| 34 | 0x0000 | `devb-mmcsd-winchester blk cache=10M,noatime,hookdll=vfs-hooks-rim.so cam quiet,cache qnx6 crypto=enable` |
| 35 | 0x0003 | display `starting SPI driver` |
| 36 | 0x0000 | `spi-master -P 0600 -u 3 -U 99:99 -d omap4430 base=0x480ba100,irq=80,sdma=0` |
| 37 | 0x0001 | waitfor `/dev/spi3` |
| 38 | 0x0000 | `sh /proc/boot/splash_script` |
| 39 | 0x0001 | waitfor `/dev/hd3` |
| 40 | 0x0000 | `backlight_win -U 99:99` + env (`BOOT_LOADER=RIMBOOT`) |
| 41 | 0x0001 | waitfor `/dev/hd6` |
| 42 | 0x0000 | `rpmb-omap4 -U99:102 -f0:102` + env |
| 43 | 0x0001 | waitfor `/dev/rpmb` |
| 44 | 0x0000 | `nvram_resource_manager -U 99:0 /dev/hd0 0x48000000 0x483FFFFF` + env |
| 45 | 0x0001 | waitfor `/dev/nvram` |
| 46 | 0x0000 | `rfs_validator -c` + env |
| 47 | 0x0000 | `sh` + env (interactive shell) |
| 48 | 0x0000 | `sanityChecking test /dev/hd1 /dev/hd2 256 4 /proc/boot/sanity 99 /proc/boot/sanity.cfg` + env |
| 49 | 0x0001 | waitfor `/base/scripts/startup.sh` |
| 50 | 0x0000 | `ksh /base/scripts/startup.sh` + env |

**Semantics.** Every process record is started **backgrounded**; ordering
is enforced only by the `0x0001 waitfor` records. There is no `on -u`;
privilege dropping is each daemon's own `-U uid[:gid]`
(`procmgr_daemon` user switch, `-U99:102` / `-U 99:0` / `-U 99:99`), and
`SYSNAME/TERM/HOME/PATH/LD_LIBRARY_PATH/BOOT_LOADER` are per-record
environment blocks. `trustzone-omap4` is started *without* a waitfor; the
`/dev/trustzone` waitfor only appears after the ctl script, i.e. QNX does
not block on the secure world early.

**Ordering constraints that matter for the 1.5 s question:**

1. `setup-core-inactive full` is the *first* program: full PRCM/VC/VP/
   clockdomain/voltage-controller bring-up (`prcm_vc_global_init`,
   `prcm_configure_volcon`, `prcm_vc_bypass_write 0x3a/0x40`, core voltage
   1225/1250 mV — report 105 §3.1).
2. All three I²C controllers must exist before `omap4430-wdtkick -P` and
   before `pmic_twl6030_cfg`.
3. The **first** watchdog kick and the **first** `0x2C` write happen only
   after ~1 s of QNX work (setup + trustzone + 3× I²C + 60 register writes).
   So QNX itself relies on the PMIC's hardware default for the first
   second.
4. Nothing before `omap4430-wdtkick` touches a watchdog.

---

## 6. Conclusions

### 6.1 Could the TWL6030 watchdog (or another PMIC action) halt the CPU at ~1.4 s?

**No — and the case is quantitative, not hand-waving.**

1. **Timeout arithmetic.** `PRIMARY_WATCHDOG_CFG` is a ≤127 s counter. Any
   halt it causes is anchored to the *last write* (or, if never written, to
   the release of `NRESPWRON`), and cannot be "1.4 s after kernel entry"
   unless the phase happens to land there. QNX re-arms it every 15 s.
   Linux never arms it — so under Linux the counter runs from power-on with
   whatever period was latched at `NRESPWRON`. That makes a PMIC-watchdog
   event a **power-epoch** event, not a boot-epoch event. The observed
   spread (1.38…1.53 s over 8 runs) is ±5 %; a 127 s power-epoch counter
   plus a variable flash/restore/reboot cycle can produce *any* offset,
   including 1.4 s — so it is not ruled out by phase, but it is also not
   *predicted* by anything in the kernel.
2. **Topology (the strong argument).** The expiry action is a transition
   into the PMIC's **WAIT-ON/OFF** power state, i.e. the rails go away.
   If VCORE/VMMC had been removed at 1.4 s, the OMAP's always-on domain
   and the WDT2 reset path would die with it and there would be **no reset
   at 55 s**. The 55 s reset *does* happen, every cycle, and it matches
   `WDT2_LDR = 0xffe2b400` → `(0xFFFFFFFF-0xFFE2B400)/32768 = 58.6 s`
   (observed ~55 s from restore timestamps). Therefore the SoC rails were
   still up: the event at 1.4 s stops **the CPU**, not the board.
3. **The 55 s reset is fully explained by OMAP WDT2** and nothing in the
   QNX set disables or reprograms it (`omap4430-wdtkick` only kicks
   `base+0x30`, §1.4). A Linux kernel that never kicks WDT2 is reset after
   ~58.6 s: exactly what is seen.

So: **the PMIC watchdog is exonerated as the 1.4 s killer.** It remains a
genuine latent bug for Linux (it will drop the rails ~2 min after
`NRESPWRON` unless kicked), and report 105's single failed `0x2C = 127`
attempt is expected to be inconclusive: it was a *kick*, not a period
change, and it was gated on a read.

### 6.2 Kernel-side neutralisation sequence (exact)

Do this **first**, before any other TWL traffic — in particular before the
first `twl_i2c_read_u8(TWL_MODULE_PM_MASTER, …, TWL6030_PHOENIX_DEV_ON)`
(which today happens only in `twl6030_power_off()`, but the MFD regmap
probe also starts issuing reads immediately):

```c
/* raw i2c, no regmap, no page select, no read-modify-write */
static const u8 twl_pet_wdt[] = { 0x2C, 0x7F };      /* reg 0x2C = 127 s (max) */

static int twl6030_pet_primary_wdt(struct i2c_client *twl)
{
        struct i2c_msg m = { .addr = 0x48, .flags = 0,
                             .len = 2, .buf = (u8 *)twl_pet_wdt };
        return i2c_transfer(twl->adapter, &m, 1);
}
```

| step | I²C addr | register | value | notes |
|---|---|---|---|---|
| 1 | 0x48 | **0x2C** | **0x7F** | 2-byte write `2C 7F`. This *is* the kick (the write reloads the counter). Do it unconditionally — never gate it on a read. |
| 2 | 0x48 | 0x2C | read back | log; on TWL6030B1 expect `0x7F` (bits[6:0] = seconds). |
| 3 | 0x48 | 0x1F | read only | `PHOENIX_START_CONDITION` — free boot-reason telemetry, and proves the bus works. **Never write** it (write clears latches). |
| 4 | 0x48 | 0x10 | read only | bit 2 = "restart to charge". **Never write** unless you intend to clear latches (`&0xFB`). |
| 5 | 0x48 | 0x25 | read only | `PHOENIX_DEV_ON`. **Never set `APP|CON|MOD_DEVOFF`** — that powers the board off. |
| 6 | repeat step 1 | | | from a `delayed_work`/`timer_list` every **30 s** (any interval < 127 s), started at probe, cancelled at shutdown. |

Also required, and independent: **kick OMAP WDT2**, or the 58.6 s reset
stays.

```c
/* WDT2 @ 0x4A314000, TGR = +0x30. Exactly what QNX does. */
void __iomem *wdt = ioremap(0x4A314000, 0x1000);
u32 tgr = readl_relaxed(wdt + 0x30);
writel_relaxed(~tgr, wdt + 0x30);           /* alternate value reloads */
/* poll WPS at +0x34 (bit 0 = pending) if you want strict correctness;
 * QNX does not bother.  Call every 15 s, well inside the 58.6 s LDR. */
```

Order matters: **pet the PMIC first**, then WDT2, then do anything slow.
And log both into the `pb_event` breadcrumb page (`+0x04`) so the two
kick results are visible after a wedge.

Reference: no page register, no `MAP` write, no `SECURED_REG` unlock is
needed for `0x2C` — the QNX tool does the plain 2-byte write and the
hardware accepts it.

### 6.3 The two decisive experiments

**A. Epoch test (zero PMIC writes — isolates `power-epoch` vs `boot-epoch`).**

```
run 1: boot the kernel normally                     → record t_halt(0)
run 2: insert a fixed delay D = 3.000 s in the loader before the kernel's
       first instruction (bootloader `sleep`, or an mdelay in the
       decompressor head, i.e. before any kernel timer exists)
       → record t_halt(D)
```

* `t_halt(D) ≈ t_halt(0) − D` ⇒ the wall is anchored to a **hardware
  epoch** (PMIC `NRESPWRON` / power-on) ⇒ PMIC watchdog or another
  PMIC/AON counter is live.
* `t_halt(D) ≈ t_halt(0)` ⇒ the wall is anchored to **kernel entry** ⇒ the
  PMIC is definitively out and the cause is kernel-relative (secure-world
  timer, PRCM/VC state, CPU1 park).

Repeat with D = 0 twice to measure the intrinsic jitter first. This is the
single highest-value measurement left, and it costs one boot cycle pair.

**B. Direct PMIC test (fixes the flaws of the 105 attempt).**

1. In `twl6030_probe`, *before* the existing `PHOENIX_DEV_ON` read:
   unconditional raw `{0x2C,0x7F}` write (no read gate), then a read-back;
   log `read`, `write rc`, `read-back` into `pb_event +0x04` as three
   nibbles.
2. Add the 30 s `delayed_work` kicker.
3. Re-run the 30 s busy-spin test and record the halt offset.
   * Wall moves later / disappears ⇒ PMIC watchdog confirmed.
   * Wall unchanged ⇒ PMIC watchdog exonerated under Linux *and* the
     neutralisation is proven in place for later bring-up.
4. Optional smoking gun: at the very first I²C access, read `0x48/0x2C`
   *before* writing. `0x7F` ⇒ something (bootloader/QNX) already latched
   127 s. `0x00`/small ⇒ never programmed, hardware default period applies
   and the phase is unpredictable — which is exactly the failure mode TI
   warns about.

**C. Cheap corroboration while you are there:** dump `0x48/0x1F` at probe.
If the PlayBook came up on plug-detect you should see bit 3 set, matching
`winchester-bootreason`'s "plug detect" (exit 60). If it reads `0x00`
after every `force_reset_loader.py` warm reset, the PMIC is *not* being
power-cycled between runs — which is itself important for experiment A.

### 6.4 Where the 1.4 s wall most likely lives (unchanged from 105, now sharper)

RE of this binary set removes two candidates and adds one datum:

- **Removed:** `omap4430-wdtkick` cannot be arming anything harmful — it
  only kicks an already-running WDT2 and pokes `0x2C`. The PMIC stack
  cannot halt the CPU (§6.1).
- **Removed:** `pmic-twl6030.cfg` has no watchdog and no `DEV_ON` write;
  `unmask_preq 1` only enables PREQ1-driven transitions, and the
  `idle=poll`/`cpuidle.off` runs already killed the idle→PREQ theory.
- **Sharpened:** the remaining QNX-only, pre-watchdog work is exactly
  `setup-core-inactive full` (PRCM/VC/VP/CLKDM/voltage) and
  `trustzone-omap4` (secure world), in that order, and QNX spends its
  first ~1 s there. Those are the two RE targets, in that order.

---

## 7. Open items

1. `0x48/0x10` could not be named from the TWL6030 material available
   here (it sits below `PM_MASTER` base `0x1F`, between the RTC block
   `0x00-0x16` and `PM_MASTER`). Bits: `0x04` = "restart to charge",
   cleared by write-back `&0xFB`. Needs the TWL6030 TRM register map.
2. I²C slave **`0x38`** (written by `pmic_twl6030_cfg` on Winchester
   boards: `0x10=0x00, 0x16=0x03, 0x17=0xFF, 0x1D=0x01, 0x70=0x00`) is not
   identified. Same bus, not the TWL6030.
3. The version probe reads `0x4A/0x87` via `DCMD 0xC0140507` with a 20-byte
   descriptor whose field layout is only partly resolved; the *effect*
   (1 byte from slave `0x4A` reg `0x87`, values 0/1/2 = ES1.0/ES2.0/ES2.1)
   is certain.
4. `PRIMARY_WATCHDOG_CFG` bit 7 is never set by any of these tools; whether
   it is an enable or reserved is not determinable statically.

---

## Sources

- [Resetting Primary WDT in TWL6030 — TI E2E](https://e2e.ti.com/support/processors-group/processors/f/processors-forum/206371/resetting-primary-wdt-in-twl6030)
- [TWL6030 primary watchdog timer expires on suspend — TI E2E](https://e2e.ti.com/support/processors-group/processors/f/processors-forum/219035/twl6030-primary-watchdog-timer-expires-on-suspend)
- Local: `kernel-clean/drivers/mfd/twl-core.c` (`TWL6030_BASEADD_PM_MASTER 0x1F`,
  `twl6030_power_off`), `kernel-clean/include/linux/mfd/twl.h`
  (`TWL6030_PHOENIX_DEV_ON 0x06`).
- Local: `bootdumps-2026-09-11/analysis/105-global-1.5s-stall-2026-10-26.md`
  (live WDT2 register dump, MSK/spin evidence).
