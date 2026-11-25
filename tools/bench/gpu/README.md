# GPU buffer-allocation probes and the pitch fix (session 70)

Two kinds of artefact:

**(1) Python `ctypes` probes** — need no compiler and no binutils on the unit
(which has neither, trap 344).  Every size they report is read from the kernel's
own record (`/sys/kernel/debug/dri/omapdrm.0/gem`), not guessed.

| probe | question |
|---|---|
| `dumbchk.py` | does a raw `DRM_IOCTL_MODE_CREATE_DUMB` allocate 1×? (yes) |
| `sizein.py` | does the kernel honour the `size` field, or recompute `pitch×height`? (recomputes) |
| `pitch.py` | **the root cause**: a 1-wide buffer gets an 8-byte pitch |
| `bochk.py`, `bochk4.py` | what `libgbm → gbm_bo_create` costs, across sizes/flags |
| `final.py` | the DDK's `PVRDRIBufferCreate`: 8.00× `stride×h`, every bpp |
| `heightspy.py` | how many rows the DDK effectively gets |
| `bpp.py` | `PVRDRIPixFmtGetBPP` returns **bits** |
| `stride.py` | reported stride is right while the allocation is 8× |

**(2) `dbmfix.c` — the fix, built and WORKING.**  An `LD_PRELOAD` wrapper on
`ioctl` that re-expresses the DDK's `width=1, height=<bytes>, bpp=8` request as
`width=<bytes>, height=1`, so omapdrm's 8-byte pitch alignment stops inflating
it.  Gated on `DBMFIX=1`, logs every rewrite, touches nothing else.

```sh
M=local/work/arm-linux-musleabihf-cross/bin/arm-linux-musleabihf-gcc
$M -shared -fPIC -O2 -o dbmfix.so tools/bench/gpu/dbmfix.c -ldl
tools/pbssh.sh 'cat > /tmp/dbmfix.so' < dbmfix.so
DBMFIX=1 LD_PRELOAD=/tmp/dbmfix.so <pvr client>
```

Measured: 19,660,800 B → 2,457,600 B; a live pvr client renders correctly and
**4/4** back-to-back runs are clean with pinned TILER flat at 66.0 MiB.

**Toolchain:** `https://musl.cc/arm-linux-musleabihf-cross.tgz` (98 MB, sha256
`11e155a0…`).  The pmbootstrap build chroot is deleted after each build, and the
host's `local/toolchains/armv7-eabihf` is **glibc** (won't load on the musl
unit), so this download is the way to rebuild.

Result and interpretation: **report 271**.
