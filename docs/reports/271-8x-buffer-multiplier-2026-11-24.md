# Report 271 — the 8× buffer waste is a pitch-alignment bug, and it is FIXED

Date: 2026-11-24 (session 70, second part).  Bench; no persistent device writes
(the shim runs from `/tmp` and only under `LD_PRELOAD`).
Follows report 270 §0.2, which measured the 8× and left it unexplained.

## 0. Answer, and it is a solved problem

**The DDK describes a linear buffer as `width = 1, height = <total bytes>,
bpp = 8`.  omapdrm aligns the scanline pitch to 8 bytes, so a 1-byte-wide row
becomes an 8-byte pitch — inflating every allocation by exactly 8×.**

The fix re-expresses the same byte count as `width = <bytes>, height = 1`:

```
before:  width=1        height=2457600  bpp=8  ->  pitch=8     size=19,660,800
after:   width=2457600  height=1        bpp=8  ->  pitch=2,457,600  size=2,457,600
```

**Measured working on the unit**: a live pvr client renders correctly
(`GL_RENDERER = PowerVR SGX 540`, magenta pre-swap, alternating-colour loop) and
**4/4 back-to-back runs were clean**, with pinned TILER flat at **66.0 MiB**
instead of climbing 59.4 → 112.3 MiB and failing.  Trap 349 does not reproduce
with the shim.

`tools/bench/gpu/dbmfix.c` → `dbmfix.so`, built with the armv7 musl toolchain.
Evidence: `docs/captures/gpu-pitch-fix-2026-11-24.txt`.

## 1. How the root cause was found (and two wrong hypotheses on the way)

| step | probe | finding |
|---|---|---|
| kernel recomputes size? | `sizein.py` | **yes** — `size = pitch × height`, any passed size ignored |
| is a raw ioctl 1×? | `dumbchk.py` | **yes**, 1.00× — the kernel is not at fault |
| is it tiling / units? | `final.py`, `bpp.py` | **neither** — uniform 8.00× at bpp 8/16/32 |
| what does libdbm *ask for*? | **`dbmlog.c` (LD_PRELOAD)** | **`width=1, height=2457600, bpp=8`** ← the real question |
| why does that cost 8×? | `pitch.py` | **omapdrm pitch-aligns to SZ_8**, so a 1-byte row → 8-byte pitch |

The logging shim was what cracked it: every earlier probe saw only the
*resulting size*, from which "height × 8" and "8 bytes per row" are
indistinguishable.  Logging the actual arguments showed the buffer is described
as a **one-pixel-wide column**, which is the bug.

### Two hypotheses that were wrong (kept so they are not re-run)

* **"The DDK asks for `height × 8` rows."**  Consistent with every size
  measurement, and a `/8` height clamp did "work" — but only because 8
  happened to be the alignment.  It was the right answer to the wrong question.
* **"The kernel/omapdrm rounds allocations."**  No: `drm_mode_size_dumb()` is
  faithful; it aligns the *pitch*, which is only wrong because the width is 1.

`omap_gem_dumb_create()` → `drm_mode_size_dumb(dev, args, SZ_8, 0)`
(`omap_gem.c:589`) is where the 8 comes from.

## 2. Why the DDK does it

`libdbm` treats a buffer as an opaque **byte count** and hands the kernel
`width = 1, height = bytes`.  For a driver whose pitch alignment is 1 byte that
is harmless; OMAP's is 8, so it costs 8×.  This is a real bug in the vendor
userspace (or at least an unwise assumption), not a deliberate padding — nothing
indexes the extra space: the framebuffer uses `pitch × height` of it, and after
the fix the object is exactly the frame.

## 3. Where the fix lives

| layer | file | licence | touched? |
|---|---|---|---|
| Mesa DRI driver | `pvr_dri.so` | MIT | no |
| `PVRDRIBufferCreate` wrapper | Mesa `pvrcompat.c` | MIT | no |
| allocator impl | `libpvr_dri_support.so` (`sgx-ddk-um-ti443x`) | proprietary | **interposed** |
| buffer allocator | `libdbm.so` (15 KB, `sgx-ddk-um`) | proprietary | **interposed, not patched** |
| kernel | `pvrsrvkm` | GPL | no |

`libdbm.so` reaches the kernel through **`drmIoctl`'s PLT entry**
(`blx ba0 <drmIoctl@plt>`, and it holds the only `CREATE_DUMB` literal in the
stack), so an `LD_PRELOAD` wrapper on `ioctl` fixes it **without modifying any
closed binary**.  Built as `dbmfix.so` (musl/armv7); gated on `DBMFIX=1`; logs
every rewrite.  Only the `width==1 && bpp==8` form is touched, so any other
allocation passes through untouched.

## 4. What it is worth

| | before | after |
|---|---|---|
| one 1024x600 AR24 client buffer | 18.75 MiB | **2.34 MiB** |
| client GEM objects (3) | 55.5 MiB | **6.9 MiB** |
| idle desktop pinned TILER | 59.4 MiB | **~7 MiB** (catacomb included, once it starts under the shim) |
| back-to-back pvr runs | 3/5 fail, occupancy 59.4 → 112.3 MiB | **4/4 clean, flat 66.0 MiB** |

It also cuts real DRAM use 8× for every client, compositor and offscreen buffer
— on a **1 GB** device that is the difference between one full-screen app and a
usable desktop.

## 5. THE COMPOSITOR IS RUNNING UNDER IT — validated end to end

The fix is now **deployed for the whole session** and validated on the panel.
Evidence: `docs/captures/gpu-pitch-fix-desktop-2026-11-24.txt`.

Deployment form (what a package should install):

| path | purpose |
|---|---|
| `/usr/local/lib/dbmfix.so` | the `LD_PRELOAD` shim (musl armv7) |
| `/usr/local/bin/catacomb-dbmfix` | exec wrapper: `DBMFIX=1` + `LD_PRELOAD=…`, then `exec catacomb` |
| `catacomb.desktop` `Exec=` | changed to `/usr/local/bin/catacomb-dbmfix` |
| `catacomb.desktop.orig` | the untouched backup (revert = restore this) |

Applied via a **`reboot -f`**, not a `tinydm` restart — the restart risks the
DSI wedge (trap 353) for no benefit.  The boot was clean: **zero** DSI errors,
`#109`, `nproc` 2, PID 1 mask 3, `BOOTCTR_FCTRY` `0x20 → 0x1f`.

### Results

| measurement | before | after |
|---|---|---|
| idle desktop, pinned TILER | 59.4 MiB | **14.0 MiB** |
| largest GEM object | 19,660,800 B (8.00×) | **2,457,600 B (1.00×)** — one full frame |
| every object in the tree | 3 × 19.6 MB + … | **all 1.00×** (2,457,600 / 2,396,160 / 81,920) |
| 8 re-rendering pvr clients, desktop up | 3/5 fail, 59.4 → 112.3 MiB | **8/8 clean**, flat **20.7 MiB** |
| panel | — | **operator confirms correct** (wallpaper, keyboard, bar) |

**Trap 349 no longer reproduces.**  Also note the plateau: with shimmed clients
the occupancy *settles* at 20.7 MiB and stays there across 8 runs, where the
unfixed path climbed monotonically until it failed.

### A measurement trap worth recording

Three `18,513,920 B` objects (the **old 8× size**) appeared after a test batch and
looked like a leak.  They were not: those runs had been launched **without** the
`LD_PRELOAD` (I passed the env explicitly per command), so they allocated the
old way and left their buffers behind.  Re-running the same loop *with* the shim
cleared them and settled at 20.7 MiB.  **When mixing fixed and unfixed runs, the
unfixed ones are the ones that leave debris** — check the object *sizes*, not
just the total, before concluding "leak".

## 6. What is NOT yet done

* **Packaging.**  It is `cp`-installed into `/usr/local` today.  Next: a proper
  `pvr-dbmfix` APK that installs the `.so` and the wrapper, then bake it into
  the hd3 rebake (which is where it becomes durable — see the rebake note).
* **The upstream fix.**  The right permanent home is `libdbm`/the DDK (describe
  a linear buffer as a byte count, not a 1-wide column) or omapdrm (accept a
  1-wide buffer without pitch-alignment waste).  The shim is the reversible
  experiment that proves the direction; it should not be forever.
* **phoc** was not retested under the shim — it uses the same `libdbm`, so it
  should benefit identically, but that is inference, not measurement.

## 7. Reproducing it

```sh
M=local/work/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf-gcc   # musl.cc
$M -shared -fPIC -O2 -o dbmfix.so tools/bench/gpu/dbmfix.c -ldl
tools/pbssh.sh 'cat > /usr/local/lib/dbmfix.so' < dbmfix.so
# observe:  LD_PRELOAD=/tmp/dbmlog.so python3 /tmp/final.py   (logs the args)
# apply:    DBMFIX=1 LD_PRELOAD=/usr/local/lib/dbmfix.so <client|compositor>
# verify:   cat /sys/kernel/debug/dri/omapdrm.0/gem   (2,457,600 not 19,660,800)
```

**Toolchain note:** `https://musl.cc/arm-linux-musleabihf-cross.tgz` (98 MB,
sha256 `11e155a0…`); the pmbootstrap build chroot is **deleted after each build**
(`rm -rf chroot_buildroot_armv7` in `local/work/nura/work/log.txt`), which is why
no compiler was present, and the host `local/toolchains/armv7-eabihf` is glibc
and unusable for the musl unit.
