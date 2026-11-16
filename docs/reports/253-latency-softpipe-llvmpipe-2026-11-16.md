# 253 — The ~2 s UI latency is softpipe; replacing the software rasterizer with llvmpipe

Date: 2026-11-16 (session 60, Linux-era 54).
Bench: the Nura root read-write from hd3, region 0 `0x294F8E36` (kernel
`7.3.3 #77`), catacomb 1.5.0-r10001 (with the env-gated `PBGL_PROFILE`
instrument), epitaph 1.4.0-r10006 (with draw-phase timing).

## 0. Headline

* **The delay is the client's software rasterizer, not the GPU driver or the
  compositor.**  During a 2 s slider drag, `epitaph` burned **3.16 s of CPU**
  and `catacomb` **0.16 s**.  Idle CPU is zero; a slow drag costs the same as
  a fast one.
* **The client was drawing on softpipe** — Mesa's reference software
  rasterizer.  Its draw of the 1024×585 drawer took **1.6–2.3 s** (`render=`),
  while the compositor's upload was 28–90 ms and the buffer swap 66–77 ms.
* **Replacing `swrast_dri.so` with Alpine's Mesa 21.3.9 gallium build (which
  contains llvmpipe) drops the drawer's `render` to 2.7–6 ms** and the drag's
  CPU from 3.16 s to 1.65 s.  The remaining cost is llvmpipe's deferred
  rasterization in `swap_buffers`: **420–550 ms** per 1024×585 frame.  Overall
  ~4× faster; the compositor keeps PVR.

## 1. The measurement

`epitaph` r10006 logs per draw:

```
epitaph: draw 1024x585 current=0ns render=1.651s swap=66ms   (softpipe)
epitaph: draw 1024x20  current=0ns render=47-66ms swap=5ms
```

and `/proc/<pid>/stat` over a 10-step / 2 s injected drag (`tools/bench/uitap.py
200 91 800 91 10 100`) gives 311 ticks for epitaph (3.11 s CPU) and 12 for
catacomb (0.12 s).  A 40-step / 2 s drag gives 316 ticks — the cost tracks the
draw, not the event count.  The drawer surface is a full-screen
`Layer::Overlay` (1024×585, the panel excluded), cleared twice and presented
whole; the visible content is only the lower part.

The compositor's `PBGL` line for the same surface:

```
PBGL import/s=2 texsub/s=4 ... avg_us=22598 max_us=43793 ... last=1024x585
```

## 2. llvmpipe

The amber Mesa stack ships only `swrast_dri.so` (softpipe), `kms_swrast_dri.so`
and `pvr_dri.so`; there is no llvmpipe, and a Wayland window client cannot use
`pvr_dri` (only the compositor's GBM/DRM path can).  Alpine 3.16 has Mesa
**21.3.9** — the same version — and its `mesa-dri-gallium` package's
`swrast_dri.so` (15.8 MB) contains llvmpipe.

Installed on the root (the amber driver backed up):

```
/usr/lib/xorg/modules/dri/swrast_dri.so         Alpine Mesa 21.3.9 gallium
/usr/lib/xorg/modules/dri/swrast_dri.so.amber   the original softpipe driver
/usr/lib/libLLVM-13.so                          llvm13-libs 13.0.1-r2
/usr/lib/libdrm_amdgpu.so.1                     libdrm 2.4.134-r0 (edge)
/usr/lib/libdrm_nouveau.so.2                    libdrm 2.4.134-r0
```

`libLLVM-13` needs only libffi/zlib/libxml2/libstdc++/libgcc, all present.
The device's `libdrm` is 2.4.134, so the edge APK matches; the Alpine v3.16
libdrm (2.4.110) would also have provided the two sub-libraries.

Verified:

```
epitaph: GL renderer "llvmpipe (LLVM 13.0.1, 128 bits)", version "OpenGL ES 3.2 Mesa 21.3.9"
epitaph: draw 1024x585 current=0ns render=2.7-6.0ms swap=420-552ms
```

and `/proc/<catacomb>/maps` still has `pvr_dri`/`libpvr_dri`, while
`/proc/<epitaph>/maps` has `swrast_dri`/`libLLVM-13`.

## 3. Caveats

* **First draw pays LLVM JIT**: the panel's first draw was 1.75 s and the
  drawer's first 4.15 s; subsequent frames are milliseconds.  A startup
  pre-warm would hide the hitch.
* **The swap is now the cost**: llvmpipe submits and rasterizes asynchronously,
  so `swap_buffers()` waits 420–550 ms for a 1024×585 frame.  Shrinking the
  drawer surface to its visible height (or damaging only the slider row) would
  roughly halve it.
* The driver is a 21.3.9-r0 Alpine build against the amber 21.3.9-r1 libEGL;
  the DRI interface matches and it loads.  Replacing `swrast_dri.so` changes
  every software EGL client, which is the intent; keep the `.amber` backup and
  re-copy it to roll back.
* The install lives on the live root only; the flasher-restorable hd3 image
  does not have it (nor `libLLVM-13`/the libdrm sub-libraries).
* `maxcpus=1` on the kernel command line means llvmpipe runs one thread; a
  second core would help it further.

## 4. Evidence

* `epitaph: draw …` lines in `/tmp/epi6.log` (softpipe) and `/tmp/epi7.log`
  (llvmpipe); `/proc/<pid>/stat` CPU sums; the `PBGL` lines in `tinydm.log`.
* `tools/bench/uitap.py` (drag mode) for the reproducible interaction.
