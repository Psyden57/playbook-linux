# BB10 on the PlayBook — working notes

**Goal (operator, 2026-11-03).**  Boot **BB10** on the PlayBook.  The target
BB10 image/version is **not decided yet**.  This file records what the operator
has stated and what must be established from the images; it deliberately
contains **no guesses** about the BB10 OS.

> Every fact in §1 is **operator-supplied**, not measured by this project.  The
> next session must verify anything it relies on, and ask before assuming
> anything else about BB10.

## 1. Operator-supplied facts

* **BB10 is not QNX 6.6 or 7.0 — it reports as QNX 8.**
* **The Z10 is OMAP-based**; its IFS should run on the PlayBook, given the
  **OMAP4460** code is compatible with the **OMAP4430** (or because RIM left
  TabletOS-era code in the BB10 image).
* **Early BB10 versions already run on the PlayBook**: **10.0.9.1103** is
  reported to run fine.  It is too early to be useful, but it proves BB10 can
  run on this hardware, with its newer **RCFS filesystem and IFS**.
* The **BB10 `/base` is an RCFS image**, not a plain QNX6 filesystem:
  `bb10mt qnx6 mount` does **not** work on it.  The expected route is to
  **dump it, edit it, and repack it**.
* A **QNX 8 SDP / toolchain** will be needed to work with BB10.  (The two
  toolchains in `local/toolchains/` are the ARM Buildroot gcc and the QNX 6.6
  SDP; neither is a QNX 8 SDP.)

**SoC check (session 25, web sources — not measured on any BB10 device).**  The
bullets above say "the Z10 is OMAP-based" and name OMAP4460.  The published
model tables do not support "Z10 + OMAP4460": the only OMAP Z10 SKU is
**STL100-1**, and its SoC is **OMAP4470**; the other Z10 SKUs
(STL100-2/-3/-4) are Qualcomm **MSM8960**, and every documented Q10 SKU is
MSM8960 too.  OMAP4460 is the **BB10 Dev Alpha (A)** and the PlayBook 4G LTE;
OMAP4470 is the **Dev Alpha B**.  So "Z10" without a SKU is ambiguous, and
**OMAP4460 describes a Dev Alpha A, not a retail Z10**.  What that means here:

* the OMAP target is either a **Z10 STL100-1 (OMAP4470)** or a **Dev Alpha A
  (OMAP4460)** — two different devices with two different images;
* OMAP4470 is *closer* to the PlayBook's OMAP4430 than a Snapdragon is, but it
  is still a different part (PowerVR SGX544 vs SGX540, different clock tree),
  so "code compatible" remains an operator claim, not a measured fact;
* the target decision must therefore name **device + BB10 version + image
  path**, which is what session 25 asked for and did not get beyond "a Z10
  OMAP build".

Sources: Wikipedia's Z10/Q10/OMAP/PlayBook tables and the Dev Alpha A/B spec
comparison; URLs in the session-25 note.  **No BB10 image has been inspected by
this project yet.**

## 2. What the PlayBook side already has

The PlayBook image chain is proven end to end (reports 169/170,
`IMAGE-REBUILD-GUIDE.md`):

* IFS: our uncompressed 2.1.0.1917 rebuild with the 8,759-byte
  `rfs_validator` shim; `os_type` 6, CRC, `LoadEnd` patched; `dumpifs`-clean.
* RCFS (`/base`): a plain QNX6 filesystem at `0xC00000`, edited with
  `bb10mt qnx6 mount`; the shim mounts it without the signature gate.
* UFS: `PlayBook_2.1.0.1917.5.ufs` **with the operator's `autorun::0` change** at the UFS delta; it skips setup.
* Container/flash: `bbusb.py pack`/`parse`/`flash` (V1+V2, signature, gate);
  hardware-proven; the reference boots the set and `devuser` SSH works.
* The reference is armed and sealed; `BOOTCTR_FCTRY` is topped up by the
  operator's `/accounts/devuser/startup.sh`.

These are the tools to reuse, but **the BB10 image's own layout may differ** —
do not assume the PlayBook deltas, QNX version, signing or filesystem type.

## 3. Open questions (ask the operator / read the image)

1. **Target image** — **partly answered (session 25): "a Z10 OMAP build", with
   no image supplied yet.**  Still needed: the Z10 **SKU** (STL100-1 =
   OMAP4470; the others are MSM8960), the **BB10 version**, and the **path**.
   See the SoC check in §1.
2. **The BB10 RCFS format** — what is it, and which tool dumps/repacks it?
   Session 25 found `mkrcfs` in the **QNX 6.6 SDP** (byte-identical to the copy
   bb10mt ships, sha256 `410a04a5…`) and confirmed it builds an RCFS image from
   a directory — but the **operator's instruction is not to trust the 6.6 one
   or base anything on it**, so the format question is **open until the QNX 8
   `mkrcfs` is available**.  `bb10mt rcfs extract` exists but only for a
   "rcfs image"; whether it accepts a BB10 `/base` is untested.
3. **The BB10 IFS** — its startup/imagefs format vs the PlayBook's QNX 6.6
   `imagefs`; does `dumpifs`/our emitter apply, or is it a QNX 8 format?
4. **QNX 8 SDP/toolchain** — is one available, where, and which host binaries
   (`dumpifs`, `mkifs`, `ntoarmv7-gcc`, `mkrcfs`) does it carry?  **The operator
   is looking for the QNX 8.0.0 one (session 25).**
5. **The PlayBook loader/factory path** — can it accept a BB10 image
   (`os_type`, CRC, `LoadEnd`, length, MCT extents, container deltas)?  The
   early 10.0.9.1103 run is the evidence that at least one BB10 image fits.
6. **Signing/secure boot** — what does the BB10 image require, and does the
   reference's factory/CRC path bypass it?  (The PlayBook shim removes the
   `/base` signature check for 1917; BB10 may differ.)
7. **Hardware gaps** — display (DSI/TC358764/panel), touch, sensors, WiFi,
   camera, audio: what works with a Z10/OMAP4460 image and what RIM left in
   for the TabletOS-era hardware.

## 4. Starting procedure for the next session

1. **Ask** which image/version and path; do not pick one silently.
2. Copy the image to `local/work/`; never edit the vendor file.
3. Identify the container and components **read-only** (`bbusb.py parse`,
   `split`, `unpack`; `bb10mt`), and record sizes, hashes, deltas and any
   signature.
4. Identify the IFS and RCFS formats; try the existing readers on **copies**,
   and record exactly which tool understands them.
5. Only then propose the first variable.  The PlayBook chain gives the way back
   (the working 2.1.0.1917 set); no flash until the operator approves.

## 4a. Session 25 (2026-11-03) — read-only preparation

**Decision.** Target = **a Z10 OMAP build** (operator).  No image was available,
so nothing was copied, parsed or flashed, and no USB/device access was performed.
Everything below is offline and read-only.

**Tool surface confirmed present (no image needed to check):**

| need | tool | state |
|---|---|---|
| PlayBook container parse/split/unpack | `tools/bbusb.py parse` / `split` / `unpack` | hardware-proven (reports 169/170) |
| BB10/CFP autoloader + container | `bb10mt autoloader extract` / `loaders`, `bb10mt split`, `bb10mt unpack -c` | present, **untested on a BB10 image** |
| raw flash image → partitions | `bb10mt raw dump` | present, untested |
| RCFS read | `bb10mt rcfs extract -i IMG -o DIR` | present; it **rejects** the PlayBook's QNX6-based `/base` (`Stream read error`), so it expects a different container — **untested on a BB10 `/base`** |
| RCFS build | `mkrcfs <dir> <out>` | present in the QNX 6.6 SDP; **not trusted** (below) |

**The `mkrcfs` caution (operator, session 25).**  The QNX 6.6 SDP ships
`mkrcfs` and the copy inside `local/vendor/bb10mt/` is the **same binary**
(sha256 `410a04a5…`); it does build an image from a directory (a scratch test
produced a valid 20 KiB image).  **The operator instructed that it must not be
trusted and that nothing may be based on it yet**, pending the **QNX 8.0.0**
`mkrcfs`.  So the BB10 RCFS format stays **open**, and no format claim from the
6.6 tool is recorded as fact.

**Fixed next step when the image arrives** (do not re-derive it):

1. copy to `local/work/`; never touch the vendor file;
2. `file`, size, sha256, and any signature/trailer;
3. identify the container: `bb10mt autoloader extract`/`split` for an
   autoloader, `bb10mt raw dump` for a raw flash image, `bbusb.py parse` for a
   PlayBook-style `.signed`;
4. list the components and their deltas; hash each;
5. on **copies**, try `bb10mt unpack`, `bb10mt rcfs extract` and (only once the
   QNX 8 tool exists) the QNX 8 `mkrcfs`/`dumpifs`;
6. record which tool understood which component, and stop.

## 5. Safety

The reference is the only working unit.  `SAFETY.md` applies: no RPMB writes by
an agent, no MCT/NVRAM writes, one variable per flash, and the way back verified
before anything is written.  A BB10 experiment that cannot be reverted is not a
valid first step.
