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

1. **Target image** — which BB10 build/device and where is the file?  (Not
   decided at session 24 end.)
2. **The BB10 RCFS format** — what is it, and which tool dumps/repacks it?
   (`mkrcfs`? a QNX 8 tool? bb10mt has `rcfs` commands — inspect them.)  The
   PlayBook's `rfs_validator` is not in the BB10 path, but the BB10 image may
   have its own verification.
3. **The BB10 IFS** — its startup/imagefs format vs the PlayBook's QNX 6.6
   `imagefs`; does `dumpifs`/our emitter apply, or is it a QNX 8 format?
4. **QNX 8 SDP/toolchain** — is one available, where, and which host binaries
   (`dumpifs`, `mkifs`, `ntoarmv7-gcc`, `mkrcfs`) does it carry?
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

## 5. Safety

The reference is the only working unit.  `SAFETY.md` applies: no RPMB writes by
an agent, no MCT/NVRAM writes, one variable per flash, and the way back verified
before anything is written.  A BB10 experiment that cannot be reverted is not a
valid first step.
