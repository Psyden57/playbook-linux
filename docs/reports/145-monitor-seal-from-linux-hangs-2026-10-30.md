# 145 — Calling the monitor's `0x2F` seal from Linux: entry fixed, the monitor rejects us

**Session 19 (absolute), Linux-era session 13.  2026-10-30.**
Status: **the monitor call was implemented as transcribed from `rpmb-omap4`;
the first hang was fixed (the monitor addresses DRAM `0x10000000` below the
non-secure physical address), and the monitor now returns `0xFF05` for every
service (`0x12`, `0x31`, `0x33`) — a global rejection of our caller.**  No RPMB
write was issued.  The unit was recovered to TabletOS and re-verified.  This
report is the durable record of the attempt and the remaining unknowns; it is
**not** a green light to retry blindly.

---

## 1. What was built

`drivers/misc/pbmon.c` (built-in, `obj-y`), plus `pbrpmb sealtest`.  It
implements the ABI recovered from `rpmb-omap4` (report 144 §2):

* service `0x33`: command page `{u32 2, u32 phys(key), u32 keylen}` loads the
  16-byte key `page+0x20` (`86 78 7c 2a 63 2f be 16 48 ec ab a6 d8 84 01 09`);
* service `0x2F`: command page `{u32 1, u32 phys(desc)}`, descriptor
  `{u16 1, u16 op|0x400, u8 dir, u8 0, len at +0x0c/+0x1c/+0x24,
  phys(data) +0x18, phys(out) +0x20}`, `op = 0`, `dir = 1` seal / `0` unseal;
* six CM2 L4SEC clocks enabled around the call
  (0x4A0095A0/A8/B0/B8/C0/C8, `MODULEMODE=2`, restored after);
* one coherent page holds the input, output, descriptor, command pages and key.

The smc veneer matches `rpmb-omap4 0x21f0`: `r6 = 0xff` (the monitor call tag),
`r12 = 0`, ICIALLU, `dsb`/`isb`/`dmb`, `smc #1` (encoded `0xe1600071`), with
`r4-r12` saved because the monitor clobbers them.

`pbrpmb sealtest` reads block 0x43 from media, unseals it through
`/dev/pbunseal`, checks the plaintext is the record store
(`bgroupcrt_dat8`/`fsecsyskey`/`drmclock.cfg`), seals it back through
`/dev/pbseal`, and compares with the media bytes — a read-only validation of
the whole ABI.

## 2. What happened

* **First image (`0xFCCE507A`)** ran the selftest at driver probe.  The unit
  never reached userspace: it reset-looped (the WDT), costing several
  `BOOTCTR_FCTRY` units until the sealed IFS was restored.  **Lesson: never run
  the smc automatically.**
* **Second image (`0x65599D90`)** moved the test behind `/dev/pbseal` /
  `/dev/pbunseal` (selftest off by default; a hang then costs one reboot of the
  same image, not a flash).  The driver probes, the nodes exist, and the image
  boots normally.  `run /pbrpmb sealtest` then **hung** and reset the unit.
* **The physical-address bias was the hang.**  Live QNX evidence: the
  RpmbInit page is at true phys `0xbfb13000` (`PTINFO`) and the descriptor it
  hands the monitor names it `0xafb13000`; the frame buffer `0xbfb8e0e4` is
  passed as `0xafb8e0e4`.  Reading the first `0x20` bytes of the page (and the
  first `0x10` of the frame) at both addresses gives **different** data — this
  was **not** a full-page compare — so they are not aliases; the monitor's DRAM
  window is `0x10000000` below the non-secure physical address.  `pbmon` now
  passes `phys - 0x10000000` for DRAM (`0xAFB13000`-style) and **the call
  stopped hanging**.
* **The monitor now returns `0xFF05` for every service.**  With the bias in
  place (`0xC462AE22`, `0x771355C0`, `0x94640F82`, `0x96FC5CA9`):

  | service | command | return |
  |---|---|---:|
  | `0x12` nonce fill | `{2, phys(buf), 16}` | **65285 = 0xFF05** (the first 4 bytes of the *true* buffer were still `aa aa aa aa`; the biased address was **not** read back, so "refused before touching memory" is not proven) |
  | `0x31` KEK/state | `{1, 1}` | **0xFF05** |
  | `0x33` key load | `{2, phys(key), 16}` | **0xFF05** |
  | `0x2F` seal | — | not reached |

  Our buffer was at true `0xbf040000` → monitor `0xaf040000` — the same DRAM
  band as QNX's working buffers, so the address is not the obvious problem.

## 3. What is still unknown

1. **A global rejection.**  The veneer is byte-for-byte the QNX/bootloader one
   (`r6=0xff`, `r12=0`, ICIALLU, `dsb/isb/dmb`, `smc #1` = `0xe1600071`, with
   `r4-r12` saved).  The CM2 L4SEC setup matches `RpmbInit`/the marshaller:
   `CM_L4SEC_CLKSTCTRL (0x4A009580) |= 3` (SW_WAKEUP) and the six L4SEC module
   clocks (AES1/AES2/DES3DES/PKA/SHA2MD5 = 2, RNG = 1).  **But the second PRCM
   write was wrong:** QNX sets `CM_MPU_STATICDEP` at **`0x4A004304`**, bit 14
   (`L4SEC_STATDEP`, TRM Table 3-1034; report 107 §4), while session 19's driver
   wrote `0x4A004004` — an undefined offset of `INTRCONN_SOCKET_CM1` — so the
   MPU→L4SEC static dependency was never enabled, and `0x4A004304` was never
   read back (TRM reset 0).  Yet every service returns `0xFF05`; the monitor is
   refusing the *caller*, not (only) the arguments.
2. **A secure-side OS registration/handshake.**  The bootloader calls the same
   services from bare metal at every boot.  QNX calls them after the OS starts.
   `trustzone-omap4` was checked: its only smc is the same generic marshaller
   (0x9188/0x7c30) from its devctl handler, and its init does not call the
   monitor — so it is not the handshake either.  The remaining candidate is a
   registration the secure world expects from the OS (possibly in the bootloader
   handover `0x80017F64`, or a service we have not identified).
3. **The monitor's address window.**  The bias is proven for `0xB...` DRAM;
   whether `0xaf040000` is inside the window is inferred from the matching band,
   not proven.  A wrong window would also give an error rather than a hang.
4. **`0xFF05`'s meaning** is not in the TRM or in any binary string; it is a
   monitor-internal status.

## 4. Before any retry (all read-only unless stated)

0. **Fix and verify the CM1 write.**  Set `CM_MPU_STATICDEP` at `0x4A004304`
   bit 14 (`L4SEC_STATDEP`) **before** the `0x33`/`0x2F` calls, and **read it
   back** (session 19 wrote `0x4A004004` and never read `0x4A004304`).  QNX sets
   this once at `RpmbInit` and never restores it; both QNX components that touch
   the monitor set it.
1. **Read the biased address after a probe.**  The `0x12` test only checked the
   first 4 bytes of the *true* buffer; reading the biased page
   (`0xaf040000`-style) after the call is what proves whether the monitor refused
   before touching memory.
2. **Reverse the monitor's caller validation.**  Compare the bootloader's
   marshaller (emmc-first1mb.bin, veneer at file `0x9078`) with QNX's for any
   per-call state; look for a service QNX/procnto issues at boot that we do not.
3. **Try a `0x2F` op=1 derivation** (RpmbInit's key setup) in case the
   monitor requires it before `0x33`/`0x2F` op=0 — this is the one init call in
   the QNX sequence we have not issued.
4. **Do not guess addresses on a seal.**  The monitor writes 224 bytes wherever
   it is told; the bias is proven, but the window is not.
5. Only when the seal reproduces block 0x43's media bytes should the one `0x32`
   write be attempted.

## 5. Safety

* Each smc hang/attempt costs at least one `BOOTCTR_FCTRY` unit (a factory boot)
  and a ~7.8 s WDT cycle; the first image reset-looped until the IFS restore.
* The counter returned to `0x32` on the TabletOS boot.
* A wrong physical address on a **seal** is worse than a hang: the monitor
  writes 224 bytes wherever it is told.  Do not guess.
* The QNX `/dev/rpmb` write path remains the only proven writer.

## 6. Bench state at the end

TabletOS up, door up; sealed IFS `c72b0530…`; region-1 root `0b87c29f…`;
`BOOTCTR_FCTRY = 0x32`.  The pbmon driver is in the kernel tree (selftest off,
bias applied, `0x12`/`0x31`/`0x33` probes in the error path); images
`0xFCCE507A` (auto selftest — do not use), `0x65599D90`, `0xC462AE22`,
`0x771355C0`, `0x94640F82`, `0x96FC5CA9` (latest, all bias-era).
