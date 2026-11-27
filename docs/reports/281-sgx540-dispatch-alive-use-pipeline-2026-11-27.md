# Report 281 — the open microkernel takes and completes jobs: USE_PIPELINE was the one-bit blocker

Date: 2026-11-27 (session 78, Linux-era 72).  Bench session on the reference
unit.  Follows report 280 (the dispatch was dead after the launch) and the
session's first nine boots (the capture has all sixteen in order).

**Headline: the open `powervr_sgx` SGX540 microkernel now dispatches
continuously and completes jobs — `null job signalled in 946 µs` and `600 null
jobs in 91 ms` with ZERO GPU resets, the CCB ring draining
(`issued 615 consumed 615`), and host interrupts delivered.  The fix was ONE
BIT in `EVENT_PDS_INFO`: `USE_PIPELINE` was TOGGLE (round-robin between the
two USE pipes); the vendor's derivation is PIPE_0.**

## 1. How it was found

Session 78's campaign (13 boots before the fix, all in
`docs/captures/sgx540-kick-entry-counter-2026-11-27.txt`):

* **The instruments were made trustworthy first** (DECISIONS §116's rules):
  the tick counter needed a **GPU-side seed** (the GPU's initial view of the
  control-block tail is not the CPU's zeros — the old counter read 0xffffffff,
  +1 wrapped to 0, and looked dead; seeded 0x100 it reads 0x101 after exactly
  one dispatch pass — trap 409); the "4 fetches per life" was **double
  counting** (the old ENTRYLOG counter still emitted beside ENTRYFIRST — trap
  410); the STATUS snapshot's limm was the byte/word bug (trap 406, fixed).
* **The negatives were re-run correctly**: DMS_AGE = ENABLE|0x2c (sticks, no
  effect); the task-end emit with the vendor's exact operand derivation
  (session 76's "does not re-fetch" was measured with garbage operands —
  withdrawn; retried correctly, same result); `UK_NO_PHAS` (the vendor's
  working SGX540 image has no `phas` at all; ours was not where anything
  died); the DOUTU task-control words fixed to the vendor's
  `MODE_PERINSTANCE` shape (kept; no effect alone).
* **The launch-script diff was completed**: the entire remaining vendor delta
  was six registers; **`CACHE_CTRL = 0x0000ffff` — the vendor's own value —
  breaks event dispatch entirely** (bisected over three boots; trap 411), the
  other five are compatible and programmed.
* **The timing test** (`UK_TIMERDIV=100`) proved the second fetch waits for
  the first timer edge: event dispatch worked exactly once per µkernel life,
  with fresh b29 edges proven to occur afterwards.
* **The pipeline registers** (`EUR_CR_LOOPBACK`, `USE0/1_SERV_EVENT`, newly in
  `status`) read idle — and `USE0_SERV_EVENT` AND `USE1_SERV_EVENT` both
  showed count 1: under **TOGGLE**, the event tasks alternated between the
  two USE pipes.  A stuck pipe blocks the toggle forever — the
  one-dispatch-then-stuck signature.

The fix: `EVENT_PDS_INFO` (0x0a70) `0x00013d01 → 0x00013c01` — clear bit 8,
`USE_PIPELINE_0` per the vendor's derivation (sgxinit.c:3640-3646).  One bit.

## 2. The live pipeline

```
entry(0x07)  0x9d4 -> 0xd2a -> ... 0x15b2     climbing (x2 per fetch)
tick(0x2c)   0x5e9 -> 0x794 -> ... 0x971      climbing (seeded counter)
USE0_SERV_EVENT  count = the fetch count      tasks admitted AND retired
USE1_SERV_EVENT  idle (PIPE_0)
```

The uAPI suite: **30/50**, with the entire null-job family passing on real
timings (946/427 µs per job; 600 jobs in 91 ms) and **no timeout lines in
dmesg**.  `resets: 0` for the whole suite — before the fix, every job cost a
GPU reset (trap 388's signature since session 74).

## 3. What still fails, and what it is

The 20 remaining failures are **SGX545-shaped test expectations**, not
dispatch failures: `dev-query` expects core_id 0x0115 / va_bits 32 / heap
base 0x3e000000 (ours: 0x0114 / 28 bits / the 28-bit `sgx540_heaps`), and
every `vm_map` failure maps at 0x3e000000-0x3e400000 — 32-bit addresses a
28-bit VM correctly refuses with -22.  The suite needs its addresses
parametrized by `va_bits` (a test-side fix).  No render or transfer job has
been submitted yet — the TA/3D register tables remain the next mountain.

## 4. The diff that matters, for the record

| register | was | now | effect |
|---|---|---|---|
| `EVENT_PDS_INFO` 0x0a70 | `0x00013d01` (USE_PIPELINE **TOGGLE**) | `0x00013c01` (**PIPE_0**) | **dispatch alive** |
| PDS0 stub DOUTU word 1 | `0x04000000` (reserved bit, MODE=PARALLEL) | `0x00000002` (`MODE_PERINSTANCE`) | kept (matches the vendor's derivation) |
| PDS0 stub DOUTU word 2 | `0x00000001` | `0` | kept |
| `DMS_AGE` 0x0a78 | 0 | `ENABLE\|0x2c` | no effect, kept |
| `USE_MOE`/`ISP_DEPTHSORT`/`USE_G0`/`USE_G1` | unwritten | vendor values | no effect, kept |
| `CACHE_CTRL` 0x0804 | (reset) | `0xffff` — **breaks dispatch** | **not written** |
| `USE_CTRL` 0x0a00 | (reset) | `31<<14` — removed in the bisect | not written |

## 5. Bench at the close of this report

Region 0 `0x02C1DDC2` (no flash all session), region 1 `20a5edd3…`, hd3 the
session-71 rebake.  **The working pair is preserved on the unit**:
`sgx540_ukernel.bin.working` = `3fbbd11b…` (kick10),
`powervr_sgx.ko.working` = `10c6f435…` — the live files ARE this pair.
`BOOTCTR_FCTRY 0x28` (the top-up fired during the session's heavy
rebooting — self-maintaining).  `wlan0` down (the earlier GPU resets);
`usb0` up.
