# 267 — the host SSH link was a VPN route; Wayland clients cannot reach the SGX (measured)

Date: 2026-11-23 (session 67 continued).  Host + bench.

## 0. Headline

* **The "SSH is broken" symptom was the host's VPN, not the unit.**  The
  WireGuard rule table had `default dev utun420` at priority 32765, i.e.
  *before* `lookup main`, so every unmarked packet to `169.254.0.1` went into
  the tunnel and `tcpdump -i enp8s0f3u1i2` saw **zero packets**.  With the VPN
  off, `ping` is 0.5 ms and `PB_LINK=usb tools/pbssh.sh` works.  Nothing was
  wrong with the RNDIS gadget, the device, or our drivers.
* **`epitaph` on hd3 was the broken `r0` build**, not the fixed one: it
  segfaulted ~120 ms after `Started Epitaph`, exactly trap 292's symptom.
  Installing the existing `epitaph-1.4.0-r10005.apk` fixes it — it now runs and
  logs its renderer.
* **A Wayland client still cannot use the SGX, and now we know exactly why.**
  `libEGL_amber.so` advertises **only** the `wayland` platform, and
  `pvr_dri.so` exports **zero** EGL symbols and contains no Wayland/platform
  glue (only `libdrm`/`drmGetNodeTypeFromFd`).  So on the Wayland platform the
  only driver Mesa can load is swrast/softpipe; `pvr_dri` is reachable **only**
  through libEGL's DRM/GBM path, which is what the compositor uses.  Measured
  both ways: surfaceless + `MESA_LOADER_DRIVER_OVERRIDE=pvr` →
  `GL_RENDERER = PowerVR SGX 540`; a Wayland client with the *same* override →
  `swrast_dri.so`, `softpipe`.  Trap 314 is correct, and the override is
  powerless on the Wayland platform.

## 1. The SSH diagnosis (the fix was on the host)

Symptom: `ping 169.254.0.1` 100 % loss, `ssh` timeout, while the device's
dropbear listened on `169.254.0.1:22` and the host interface was UP with carrier
and the right address/route.

What narrowed it:

| test | result | meaning |
|---|---|---|
| `ip -br addr` both ends | device `usb0 169.254.0.1/16` UP, host `enp8s0f3u1i2 169.254.0.2/16` UP | configuration is correct |
| `ss` on device | dropbear listening `169.254.0.1:22` | the server is fine |
| device→host ping | device `ip neigh` resolved the host `REACHABLE`, host `rx_packets` climbed | the USB/RNDIS link carries traffic |
| host→device ping | host `tx_packets` frozen, device `rx_packets` frozen | host never transmits |
| `tcpdump -i enp8s0f3u1i2` during ping | **0 packets captured** | the frames never reach the netdev |
| `arping -I enp8s0f3u1i2 169.254.0.1` | **unicast reply, 0.85 ms** | the link is fine; raw L2 bypasses routing |
| `ip rule` | `32765: not from all fwmark 0xca6c lookup 51820`; table 51820 = `default dev utun420` | **the VPN captures the route** |

`tcpdump` seeing nothing while `arping` succeeds is the tell: the packet never
reached the interface because the routing decision sent it to `utun420`.  With
the VPN off, the rule disappears and everything works.

Notes for later:
* The rebind experiments (`rndis_host` unbind/bind, USB `authorized` 0/1,
  `usbreset`) all appeared to "half fix" it only because they reset counters;
  they were red herrings caused by the same VPN blackhole.
* `ufw` was also `default deny (incoming)` with no rule for
  `enp8s0f3u1i2`; that would have blocked SSH even with a correct route, so a
  rule was added (`ufw allow in/out on enp8s0f3u1i2`).  Worth keeping.
* The ACM console was never affected (it is a tty, not IP).

**QNX question.**  The RNDIS gadget needs no per-OS configuration: it is the
device acting as a standard USB Ethernet adapter, and both the Linux
`rndis_host` and QNX's USB RNDIS class driver speak the same control protocol.
That is why `PB_LINK=door` (oracle) and `PB_LINK=usb` (our image) both reach
`169.254.0.1` with no device-side change.  Only the **host** side differs, and
only in *which* interface/IP the host uses.

## 2. epitaph: hd3 had the broken build

```
[    0.73] ... Started Epitaph
...  Segmentation fault        (rc=139, ~120 ms in)
```
`/lib/apk/db/installed` said `V:1.4.0-r0`.  Trap 292's fix (`pkgrel=10005`,
`fix-renderer-init.patch`: create the rasterizer after `create_surface`, cap the
atlas to `GL_MAX_TEXTURE_SIZE`, single-output premultiplied shader) lives in
`local/work/nura/work/packages/edge/armv7/epitaph-1.4.0-r10005.apk`
(sha256 `aee3fdc8…`).  Installed it (`apk add --allow-untrusted
--force-overwrite`), version is now `1.4.0-r10005`, and the client runs:

```
epitaph: GL renderer "softpipe", version "OpenGL ES 3.1 Mesa 21.3.9 Amber
         (git-53b2b224dc)", dual-source=true
```

That also confirms the second half of STATE's warning: **the session-60
llvmpipe replacement is not on hd3 either** — it reports `softpipe`, and
`/proc/<pid>/maps` shows `swrast_dri.so` (the amber original).

## 3. Why a Wayland client cannot use the SGX (measured, not inferred)

| probe | platform | `MESA_LOADER_DRIVER_OVERRIDE=pvr` | result |
|---|---|---|---|
| `/tmp/egltest.py` | **surfaceless** (`EGL_PLATFORM_SURFACELESS_MESA`) | yes | `GL_RENDERER = PowerVR SGX 540` |
| `/tmp/egltest.py` | surfaceless | no | `PowerVR SGX 540` (same) |
| `epitaph` | **wayland** (`wayland-1`) | yes | `swrast_dri.so`, `softpipe` |
| `epitaph` | wayland | no | `swrast_dri.so`, `softpipe` |
| `catacomb` (compositor) | GBM/DRM | `pvr` | maps `pvr_dri`, `gbm` |

Why the override cannot help:

* `strings /usr/lib/libEGL_amber.so.0.0.0` → the only platform name it
  advertises is **`wayland`** (no `drm`, `gbm`, `surfaceless`, `x11` as
  platform *names*; surfaceless still works via the platform-less default path).
* `nm -D /usr/lib/xorg/modules/dri/pvr_dri.so | grep -i egl` → **0** symbols.
  `pvr_dri.so` is a Mesa **gallium DRI** driver: it is loaded *by* libEGL's DRM
  loader, it is not itself an EGL implementation.
* Its only platform-ish strings are `libdrm.so.2` / `drmGetNodeTypeFromFd`, i.e.
  the DRM render node (`/dev/dri/renderD129`) path — exactly what the
  compositor uses.
* `libGLESv2_PVR_MESA` is the vendor GLES blob; it is the *backend* behind
  `pvr_dri`, not a way to get an EGL display for a Wayland surface.

So on the Wayland platform Mesa has nothing but swrast to load: the SGX is
reachable from a **DRM/GBM** client, not from a Wayland-surface client.  That is
what trap 314 recorded, and this session supplies the mechanism.

### What a real fix requires

Pick one; none is a config toggle:

1. **A pvr Wayland EGL platform.**  Write/port a platform that gives Wayland
   clients an `wl_drm`/`linux-dmabuf`-backed `EGLDisplay` and routes buffers to
   `pvr_dri`.  This is the "correct" fix and the largest piece of work; it is
   what the pvrports project has for Phosh/phoc but not for arbitrary clients.
2. **A compositor that renders clients itself** on the path that *does* work
   (GBM/pvr) — e.g. bring up `phoc` (pvrports ships it) and use Phosh, which is
   pvrports' supported configuration.  That gets GPU compositing; the *clients*
   would still be software unless (1) exists.
3. **A dmabuf/X bridge** so clients render into buffers the compositor uploads
   with pvr.
4. Keep llvmpipe for clients (what session 60 did) and accept software client
   rendering; only the compositor is on the GPU.

Given the operator's goal ("get it all on the GPU"), option (1) or (2) is the
route; both are projects, not tweaks.  Option 2 is the smaller first step and
has a vendor-supported configuration to copy.

## 4. Bench state at the close

Device: `7.3.3 #109 SMP`, `nproc` 2 (PID 1 mask `3`), `BOOTCTR_FCTRY = 0x24`,
`epitaph 1.4.0-r10005` installed on hd3, dropbear on `169.254.0.1:22`, ACM
`/dev/ttyACM0`.  Host: VPN was the fault; `ufw allow in/out on enp8s0f3u1i2`
added.

## 5. Artefacts

* `local/work/egltest.py` (copied to `/tmp/egltest.py` on the unit).
* `epitaph-1.4.0-r10005.apk` → installed on hd3 from
  `local/work/nura/work/packages/edge/armv7/`.
* Evidence: this session's SSH/`tcpdump`/`ip rule` transcripts; the maps and
  renderer lines quoted above.
