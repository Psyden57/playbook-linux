# 157 — `trustzone-omap4`'s raw SMC pass-through is not usable on this unit

**Session 21 (absolute), Linux-era session 15.  2026-10-31.**
Status: **read-only.  No RPMB, NVRAM or media writes were performed.**  The
proposal was to use `trustzone-omap4`'s root-only raw SMC pass-through to run
`0x12`/`0x2E`/`0x2F` on the unarmed reference, removing the arming gate.  The
interface is real, but the resmgr **segfaults inside libc on the first
`0xC0280501` devctl**, on a clean boot and with every copy we tried.  The
arming gate therefore remains.

---

## 0. Headline

* The pass-through exists: devctl **`0xC0280501`** (handler `0x89dc`; `movw
  0x501` / `movt 0xC028` at `0x8a5c`/`0x8a60`).  **Report 107 §5.1's
  `0xC0285001` is a typo**; the sibling code is `0xC0280502` (ECDH).
* Payload is `msg+0x14 = {funcid, a1, a2, nwords, w[4]}`, `w0..w3` at
  `msg+0x24..0x30`, result at `msg+0x10`, reply 40 bytes, root-only
  (`iofunc_check_access(..., 0x800, ...)`).
* On a clean boot `trustzone-omap4 -U99:102` is **up** (pid 8197) and
  `/dev/trustzone` exists.  A **standard** devctl (`DCMD_ALL_GETFLAGS`) works
  (rc 0) and the resmgr survives.
* A **`0xC0280501` devctl crashes the resmgr**: devctl returns **3**
  (`ESRCH`), `retval` stays `0x7fffffff`, and slog records
  `run fault ... signal 11 code 1 ip <libc base + 0x91f8>`.
* Reproduced with the stock binary, a 40-byte and a 106-byte payload, a copy
  patched to bypass the `iofunc_client_info`/`iofunc_check_access` calls, and
  a copy with `msg_max_size = 0x1000`.  The standard devctl still works in the
  patched copies; the `0x5001` path still dies.
* The **boot-time** resmgr also crashes on the `random` driver's devctl:
  `entropy_source_start_thread: rng_hwRNGen() failed` and `Failed to open
  TrustZone resource manager` in slog.  This is a stock-OS bug on this
  restored 10.0.4.197 root, not our tool.

## 1. The interface, verified statically

From report 107, report 146 §Q1.1a and the binary/disassembly:

* `smc_call(cmd, a1, a2, nwords, w…)` copies `{nwords, w…}` into the proxy's
  own 4 KiB NC phys-contiguous page and passes its physical address in `r3`;
  `r0=cmd`, `r1=a1`, `r2=a2`, `smc #1`, with the L4SEC clock bracket.  It
  forwards **any** funcid; no policy.
* The 0x5001 handler reads the client's 4 descriptor words from
  `msg+0x24..0x30`, loads `r0..r3` from `msg+0x14..0x20`, calls `smc_call`,
  writes the monitor's `r0` to `msg+0x10`, and returns a 40-byte reply.
* The rpmb-omap4 builders pin the command-page shapes:
  * `0x12`/`0x33`: `{2, phys(ptr), len}` (builders `0x24a8`/`0x24ec`);
  * `0x2E`: `{2, phys(desc), keyflag}` with the 8-word descriptor
    `{mode, 0, data, len, out, outlen, ptr, lens}` (builder `0x26c4`);
  * `0x2F`: `{1, phys(desc)}` with the descriptor at page `+0x480`
    (builder `0x2530`; the live seal/unseal descriptors are in
    `docs/reports/data/session21-capture/`).
* `mem_offset64()` returns the **QNX paddr** (equal to `PTINFO.paddr` on this
  unit); the monitor writes back its internal `−0x10000000` view.  The caller
  passes the QNX paddr verbatim.

## 2. Live test on the reference

Bench: reference `0x50175127`, TabletOS, door up.

| step | result |
|---|---|
| clean boot | `trustzone-omap4` pid 8197, `/dev/trustzone` present |
| `DCMD_ALL_GETFLAGS` | rc 0, resmgr alive |
| `/tmp/tzsmc nonce` (`0x12`, 40-byte payload) | devctl **3**, `retval 0x7fffffff`, `data changed: no`; resmgr dies |
| same, 106-byte payload | same |
| copy patched: `iofunc_client_info`/`iofunc_check_access` calls → `mov r0,#0` | same |
| copy patched: `msg_max_size 0x24 → 0x1000` | same |
| map/fault | main text `0x78520000`, libc text `0x7863a000`; fault `0x786431f8` = **libc+0x91f8** |

The fault offset is the same in every run (`...1f8`): the crash is a fixed
libc path, not a bad function pointer from our payload.  The standard devctl's
small reply works, so the dispatch/reply machinery is otherwise functional;
the `0x5001` path (or its reply) is what dies.  A QNX gdb attach/spawn via
qconn was attempted (`target qnx 169.254.0.1:8000`) and failed
(`Failed to attach` / `spawn error`), so no backtrace was obtained.

The boot-time resmgr is also affected: the `random` driver opens
`/dev/trustzone` and the resmgr faults, leaving the RNG source failed.  This is
independent of our tool.

## 3. What this means for the plan

* **The previous agent's "removes the ordering constraint" is false on this
  unit.**  The `0x2E`/`0x2F` validation cannot be done through this resmgr.
* The arm gate for the monitor work **remains**: either the operator arms the
  reference for the `pbmon` path, or the trustzone resmgr is debugged/fixed
  first (a core/gdb backtrace would be the next step).
* The probe tool is kept at `tools/qnx-tzsmc.c` so a fixed resmgr can be
  exercised without rewriting the interface.  It implements `addr`, `flags`,
  `nonce`, `kek`, `key`, `hmac`, `unseal` and `seal`, with the live descriptor
  templates and the 106-byte transaction length.
* **The reference remains on the product path; no media was touched.**  The
  patched resmgr copies live only in `/accounts/devuser`
  (`trustzone-omap4-patched`, `-bigmsg`); the stock `/proc/boot` binary is
  unchanged.

## 4. Residual unknowns

* The exact libc function at `0x91f8` and its caller — needs a working
  debugger/core.
* Whether a different `trustzone-omap4` build exists that works on this root
  (the corpus has only the one 61208-byte binary; the UFS tar does not carry
  it).
* Whether the monitor would accept the pass-through even with a fixed resmgr.
* Why the restored 10.0.4.197 root's resmgr crashes where the factory 4869
  boot image presumably did not (a libc/resmgr ABI or dispatch-buffer issue is
  the leading hypothesis).

## 5. Bench state at the end

* TabletOS up, door up; `/dev/trustzone` is **absent** (the last resmgr
  crashed).  The stock resmgr will start on the next boot and crash again on
  the `random` driver's devctl.
* The reference is otherwise fine; no RPMB/NVRAM/media writes.
* The read-only block-`0x3B` pair from report 156 remains the validation input
  for the armed `pbmon` route.

---

## Addendum — re-tested on 2.1.0.1917 with root (session 27, 2026-11-06)

The operator enabled root ssh on the 1917 set, so the probe was re-run on that
build (the earlier test was the 2.0.0.4869 restored root).

* `/dev/trustzone` exists and `trustzone-omap4` is **alive** (pid 16390) — it
  does not crash at boot here, unlike the 4869 root.
* `tools/qnx-tzsmc.c` builds and runs; `tzsmc addr` prints its four pages with
  `mem_offset64() == PTINFO_paddr` (`0x...`), so the tool side is fine.
* `tzsmc nonce` (`0x12`) → **`devctl` returns 25 (`ENOTTY`)**,
  `retval` stays `0x7fffffff`, the buffer is untouched, and **the resmgr
  survives**.  No segfault, no `libc+0x91f8` fault.
* **Static reason:** the 1917 `trustzone-omap4` (65 304 B) does **not** contain
  the devctl constants at all.  The 4869 binary (61 208 B) has
  `e3003501`/`e34c3028` (`movw/movt r3,#0xC0280501`) at `0x8A5C`/`0x8A60` and
  `movw r3,#0x502` at `0x8A6C`; the 1917 build has **none** of `0xC0280501`,
  `0xC0280502` or `0xC0285001` in any encoding (scanned as little-endian words
  and as ARM/Thumb immediates).

So on 1917 the raw SMC pass-through is **not present**, rather than present and
faulting: `ENOTTY` is the whole story.  The route stays closed for this unit's
current OS, and the negative is now explained rather than unexplained.  The
transform work of report 175 does not need it.
