# 147 — The monitor's address view is pinned: identity, and the page must be below 0xB0000000

**Session 20 (absolute), Linux-era session 14.  2026-10-31.**
Status: **the `0xFF05` refusal is solved — it was a wrong command-page address.**
The monitor reads and writes **identity physical addresses**, so the
`−0x10000000` "DRAM bias" of session 19 was an artefact of QNX's own physical
address space, not a property of the monitor.  With an identity address into a
page below `0xB0000000`, service `0x12` returns **0 and fills the nonce buffer**.
No RPMB write was issued.

Image: **`0x3116B6F8`** (`boot/chainloader/kernel.img` at the time of the run;
`drivers/misc/pbmon.c` with the `CM_MPU_STATICDEP` fix and `/dev/pboracle`).

---

## 1. What was fixed first, and what it did not fix

Session 19's driver wrote the MPU→L4SEC static dependency to **`0x4A004004`** —
an undefined offset of `INTRCONN_SOCKET_CM1`.  RpmbInit's second PRCM write is
`CM_MPU_STATICDEP[14]` = `L4SEC_STATDEP` at **`0x4A004304`** (MPU_CM1 mapped at
`0x4A004300`, off `0x004`; report 107 §4, TRM Table 3-1034).  This session's
driver writes the right register at probe and never restores it, like QNX.

It was **not** the refusal: probe 1, the unchanged session-19 call, still
answered `0xFF05` with the buffer untouched.

An incidental finding: `/dev/mem` (the console's `peek`) gets
`SIGBUS` on `0x4A009580` and `0x4A004304` — the L3 reports
`MASTER MPU TARGET GPMC (Read): Data Access in User mode`.  **User-mode** access
to those PRCM registers is firewalled; the kernel's privileged access is not
(the driver's write at probe did not fault).  Readback therefore has to come
from kernel code, not `peek`.

## 2. The oracle

`/dev/pboracle` (`pbrpmb oracle [n]`) runs one graded probe per write.  The
`0x12` nonce fill is the signal: the service writes 16 bytes into a caller
buffer, so *where the monitor wrote* identifies its address view even when the
return code is an error.  Three DTS `no-map` reserved pages make every candidate
translation hang-safe, so a stray 16-byte write can only land in our own
scratch:

| page | physical | role |
|---|---|---|
| `pbmon-low` | `0xA0200000` | the identity candidate |
| `pbmon-shadow` | `0x90200000` | low − 0x10000000 |
| `pbmon-high` | `0xB0200000` | low + 0x10000000 |

Each probe writes the command page `{2, phys(data), 16}` into the low page and
passes the address under test as `r3`; the report says which page's `+0x100`
changed.

## 3. Results

| # | service | domain | `r3` | ret | nonce landed | meaning |
|---|---|---|---:|---:|---|---|
| 1 | `0x12` | 4 | `0xaf040400` (biased, coherent page at `0xbf040000`) | 65285 `0xFF05` | none | the session-19 call, unchanged |
| 2 | `0x3A` | 4 | `0xaf040400` | 65285 | none | a zero-argument service is refused identically |
| 3 | `0x31` | 4 | `0xaf040400` | 65285 | none | QNX's form, QNX's domain |
| 4 | `0x31` | 5 | `0xaf040400` | 65285 | none | the bootloader's domain |
| 5 | `0x12` | 0 | `0xaf040400` | 65285 | none | the bootloader's domain |
| **6** | `0x12` | 4 | **`0xA0200000`** (identity, low page) | **0** | **low** | **accepted; nonce written to the page itself** |
| 7 | `0x12` | 4 | `0x90200000` (the shadow page) | **1** | none | the monitor read the `0xAA`-filled shadow page and rejected it as a command page |

Consequences, all measured:

1. **The monitor's view is identity.**  Probe 6 passed the true physical address
   of the low page and the nonce appeared in that page.  Probe 7 passed the true
   address of the shadow page and the monitor found *that* page (filled with
   `0xAA`), not the command page — so there is no translation offset.
2. **The monitor cannot reach `0xBF040000`.**  The old call with that identity
   address hung; `0xAF040000` returned a clean page-validation error, and probe
   6's page at `0xA0200000` works.  So the reachable window contains
   `0xA0200000`–`0xAF040000` and excludes `0xBF040000`; the exact upper bound
   between those was not measured.  Every monitor page must stay below the
   bound — the reserved low page at `0xA0200000` does.
3. **`0xFF05` is a page-validation error, not a caller rejection.**  It is what
   the monitor returns when it reads a page whose contents cannot be a command
   page.  A *different* malformed page (`0xAA…` count) returns **`1`**.  The
   session-19 reading of `0xFF05` as "a global refusal of our caller" is
   superseded (reports 145/146 §Q2).  Nothing about our caller — veneer, domain,
   clocks, service numbers — was ever wrong; the address was.
4. **The domain is irrelevant** (`0`, `4`, `5` all behave identically), as
   report 146 §Q3.1 predicted from the bootloader/QNX divergence.

## 4. Why the session-19 bias looked right

The live QNX page dump (`docs/reports/data/rpmb-page-0xbfb2b000-2026-10-30.txt`)
shows the 0x2F descriptor with `data = 0xafb2b700` and `out = 0xafb880e4` for a
page that `PTINFO` reports at `0xbfb2b000` and a frame buffer at `0xbfb880e4`.
Reading that as "the monitor is told 0x10000000 less" was the error: **QNX's own
physical addresses for DRAM are offset by +0x10000000 relative to the SoC's**
(QNX maps the 1 GB bank at `0x90000000`; its `paddr_t` space is not the TRM
address space).  So QNX's `0xafb2b700` *is* the identity address the monitor
wants for the page QNX calls `0xbfb2b700`, and `memdump3` at the two values
reads two different pages because QNX applies the same offset to both.

The correct rule for Linux is therefore: **pass true physical addresses, and
keep every page the monitor touches below `0xB0000000`.**

## 5. What this unblocks, and the next step

The seal/unseal path (`0x33` key load + `0x2F` transform) is now reachable: the
caller is accepted, so `pbrpmb sealtest` can be re-run with the driver's pages
moved to the reserved low page and identity addresses.  The validation order is
unchanged: reproduce block 0x43's media bytes (unseal then seal) **before** any
RPMB write, then the single verified `BOOTCTR_FCTRY = 0x32` top-up
(SAFETY.md §2/§4).

## 6. Bench state at the end of this run

The sealed IFS was restored and verified at the end of the session
(`c72b0530…`), TabletOS is up with the door, `BOOTCTR_FCTRY` reads
`32 00 00 00`, and the region-1 root is intact.  The seal work that followed
this oracle is report 148.
