# Session 67 (absolute) — the mailbox fault is a comparator lifetime bug; fixed and validated

> Numbering: absolute `67` (the project's chronological scheme — see
> `NUMBERING.md`).  The reports call this **Linux-era session 61**
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-23.
Unit state at the end: Linux/Nura on hd3, region 0 = **`0x3547D613`** (ARMv7
`#109 SMP` dual-core, **stock `omap_musb_set_mailbox()` restored**, comparator
fix, `panic=0`); `BOOTCTR_FCTRY = 0x28`; USB link up at the close.

## Goal of this session

Run HANDOFF-66 §0d's one experiment: **audit the TWL6030 `phy_companion`
lifetime statically**, not with more markers.  Done = the reason
`phy->comparator->set_vbus` is a slab address is explained, and — if a fix
follows — the stock `omap_musb_set_mailbox()` call runs on dual-core for 3+
clean boots.

## Unit state inherited and left behind

* Inherited: region 0 `0xC9FEE2D1` (device-mode-only workaround), hd3 the Nura
  ext4 `07555bde…`, patch 65 files / 9 110 lines (`6f9bef24…`),
  `BOOTCTR_FCTRY` last known `0x26`.
* **Left behind:** region 0 **`0x3547D613`** (10,448,868 B, `7.3.3 #109 SMP`,
  the stock mailbox call, the comparator lifetime fix, `panic=0`); hd3 and
  region 1 untouched.  `BOOTCTR_FCTRY = 0x28` (the three validation boots read
  `0x2a`, `0x29`, `0x28`).  No restore required.

## Confirmed (measured, read in a file, or seen on the bench)

* **The comparator is a freed `devm` object.**  Report 264's faulting `r0`
  (the `phy->comparator` value) is `c18d5280`; the kernel decodes it as
  `slab kmalloc-192 start c18d5240 pointer offset 64 size 192`, i.e. a
  **`devm_kzalloc`** allocation (data at offset 64 = `sizeof(struct devres)`,
  the same offset the `otg`/`glue`/`musb` registers show).  Its first word is
  `c2c05800`, a `kmalloc-1k` heap pointer — not `twl6030_set_vbus` text.
  `struct twl6030_usb` is ~120 B + 64 = `kmalloc-192`, so this is a
  freed/reused TWL comparator.  [capture
  `docs/captures/musb-mailbox-fault-2026-11-19.txt`; report 265 §2]
* **The bug is in `twl6030_usb_probe()`.**  It stores `&twl->comparator` in the
  PHY (`phy-twl6030-usb.c:358`) before `twl6030_usb_ldo_init()`; that function
  calls `regulator_get(dev, "usb")` (the board's `usb-supply = <&vusb>`) and
  returns `-ENODEV` for **any** error, including `-EPROBE_DEFER`.  A deferred
  `vusb` therefore fails the probe permanently, devres frees `twl`, and
  `phy->comparator` dangles.  The IRQ-request failures leave the same dangling
  pointer.  [report 265 §1]
* **The other writer is inert.**  `cpcap-charger.c:921/940` is the only other
  `omap_usb2_set_comparator()` caller; `CONFIG_CHARGER_CPCAP=m` and there is no
  CPCAP node in the board DTS, so it does not probe.  [report 265 §1]
* **The fix compiles and boots.**  `twl6030_usb_ldo_init()` returns
  `PTR_ERR()`, a devm action clears the comparator on probe failure/remove, and
  `omap_usb2_set_comparator()` drops its `usb_get_phy()` reference.  With the
  winchester early return removed, image `0x3547D613` (#109 SMP) gave **3/3
  clean dual-core boots** (`online=0-1`), gadget `binding gadget driver [g1]`
  at ~1.41 s, `twl6030_usb … Initialized` at ~0.8 s, and no
  Oops/panic.  [flash log `local/work/flash-session67.log`; evidence
  `local/work/session67-boot3-evidence.txt`]
* **`BOOTCTR_FCTRY` was healthy across the cycles:** `0x2a → 0x29 → 0x28`,
  each read as `>= 0x1e, no write`.  [ACM]
* **The patch re-exports cleanly:** 67 files / 9 136 lines, sha256
  `d19adff8…`, 0 rejects / 67/67 identical against pristine 7.3.3; the tree
  walk (trap 211) found only the known excluded diagnostics and generated
  artifacts outside the patch.  [report 265 §5]

## Inferred (reasoning from evidence, not directly measured)

* **The faulting session-66 boots took the `ldo_init()`-fails path.**  Their
  `dmesg` was never captured (panic=0, ring only), so this is inferred from
  the pointer class plus the code: a live `twl` cannot have a heap pointer in
  its first word, and the only free path is a failed probe (or remove, which
  cannot happen for this built-in driver).
* **The deferred-probe race is the trigger.**  `twl6030_usb` is a
  `subsys_initcall`, the `omap-usb2` PHY is a device initcall, and `vusb` is a
  sibling TWL-MFD child; the late deferred-probe retry can still find `vusb`
  unregistered.  The three validated boots happened to get a ready regulator
  (all logged `Initialized`), so the fix's retry path was not necessarily
  exercised — but the dangling/free path is removed either way.
* **The workaround was hiding the bug, not fixing it.**  The device-mode-only
  early return skipped `otg_set_vbus()`, which is the only reader of
  `phy->comparator`; the dangling pointer was harmless until the stock call
  was restored.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"The comparator object is live and corrupted in place."**  Rejected: the
  object is a `devm` `kmalloc-192` allocation (offset 64) whose first word is
  a heap pointer; that is a freed/reused slab, not an in-place overwrite of a
  live `twl` (which would keep `twl6030_set_vbus` text unless something else
  wrote it — no such writer exists).
* **"`cpcap-charger` clobbers the pointer."**  False on this board: the driver
  is a module and has no DT node.
* **"The report-265 fix must change `.config`."**  No: it is driver-only; the
  vermagic is unchanged and no module rebuild is needed.

## Unknown / open questions

* **What drives the intermittent VUSB teardown?**  Unchanged (trap 337).  The
  only writer is `twl6030_usb_irq()` on a `VBUS_DET=0` read; a `VBUS_DET`/
  `linkstat` log in that handler would settle it.  This session's three boots
  did not drop during the (short) checks.
* **How often does the probe's regulator actually defer?**  Not measured; the
  fix makes it safe and self-healing.  A boot-time log (or reading the deferred
  probe state) would quantify it.
* **OTG host mode** is reachable again but untested.
* **MPU OPP**, memory read throughput, UI performance, the hd3 rebake — all
  unchanged from session 66.

## Superseded / uncertain

* **Trap 338 / report 264's "next: audit the comparator lifetime"** is
  answered: the comparator is freed by a failed probe, not corrupted in place.
  Report **265** is current for this fault.
* **Session 65's "serializing the callers does not fix it"** stands; the mutex
  is kept as hardening but is not the fix.
* Nothing else this session contradicts a newer document.

## Second half — the CPU was effectively single-core (report 266)

After the mailbox fix, the operator asked to continue to the MPU OPP.  Looking at
the live unit first produced a much bigger finding.

**Confirmed:**

* **PID 1 was affinity-masked to `{0}`, so no userspace ever ran on CPU 1.**
  `online = 0-1` and `SMP: Brought up 1 node, 2 CPUs`, but `taskset -p 1` → `1`,
  `nproc` → `1`, and every userspace process (init, OpenRC, dropbear, the
  console shell) had `Cpus_allowed_list: 0`; only kernel threads had `0-1`.
  Reproduced on **3/3 fresh boots**; `taskset -c 1 <work>` always worked, so
  CPU 1 was functional and simply starved of userspace tasks.  [report 266 §1]
* **The fix is an inittab line** — `::sysinit:/usr/sbin/taskset -p 3 1` before
  the OpenRC entries.  Verified across two boots each way: with it, PID 1 is
  `3`, `nproc` is `2`, a respawned shell is `0-1`, and a two-thread busy loop
  spreads load evenly (`cpu0` 2643 / `cpu1` 2622 user ticks); without it the pin
  returns.  It is a root-filesystem change on hd3, so the region-0 CRC is
  unchanged.  [report 266 §1]
* **`cpufreq-dt` is bound and scaling works** — the handoff's "no `cpufreq`
  policy exists" is stale.  `scaling_driver = cpufreq-dt`, OPPs `300000 600000
  800000 1008000`, governor `ondemand`; max 300000 → cur 300000, restore →
  1008000.  What is missing is the CPU **supply**: no `supply-*` entries under
  `/sys/kernel/debug/opp/cpu0/`.  [report 266 §2]
* **The `twl6030_uv_to_vsel: OUT OF RANGE` lines are startup VC init**, not a
  runtime cpufreq fault: `omap4_mpu_vc_data.on = 1375000` and
  `OMAP4_VP_MPU_VLIMITTO_VDDMAX = 1410000` exceed `twl6030_vsel_to_uv(0x39)` =
  1316660, and the function clamps to VSEL `0x3A` = 1.35 V — the same 1.35 V the
  chainloader commands (`PRM_VC_VAL_BYPASS = 0x003A5512`).  So 1008 MHz @ 1.35 V
  is consistent and intended; only the table-driven voltage path is unbuilt.
  [report 266 §3]
* **SmartReflex MPU is present but idle** (`autocomp = 0`, Class3 init only).
  [report 266 §2]

**Inferred:** the widening in `sched_init_smp()`
(`set_cpus_allowed_ptr(current, housekeeping_cpumask(HK_TYPE_DOMAIN))`) is being
clamped to CPU 0 because `cpu_active_mask` has not yet gained CPU 1 when it runs
(the `CPUHP_AP_ACTIVE`/`sched_cpu_activate` state is set during secondary
bring-up).  `kernel/sched/core.c` and `isolation.c` are byte-identical to
pristine and there is no `isolcpus=`/`nohz_full=` in `/proc/cmdline`, so nothing
in our patch explains it; a single instrumented boot printing
`cpu_active_mask`/`cpu_online_mask` at `sched_init_smp()` entry would settle it.
[report 266 §1]

**Why it matters:** report 263 §4b's "two cores in parallel: 0.38 GB/s, no
scaling" and the UI-latency numbers were measured with the second core starved
of userspace.  Those items should be re-measured.

**Also this half:** the earlier session-66/67 observation that SSH/RNDIS does
not answer on the Nura root was confirmed again; the ACM (`local/work/pbcmd.py`)
was the console throughout.

## Third part — SSH was the host's VPN; the GPU path measured (report 267)

The operator asked why SSH was not working, and to prioritise GPU rendering over
re-measuring a software-rendered UI.

**Confirmed:**

* **The SSH failure was the host's VPN, not the unit.**  `ip rule` had
  `32765: not from all fwmark 0xca6c lookup 51820` with table 51820 =
  `default dev utun420`, i.e. before `lookup main`.  Every packet to
  `169.254.0.1` went into the tunnel, which is why
  `tcpdump -i enp8s0f3u1i2` showed **zero packets** while raw
  `arping -I enp8s0f3u1i2 169.254.0.1` got a 0.85 ms unicast reply.  With the
  VPN off, ping is 0.5 ms and `PB_LINK=usb tools/pbssh.sh` works.  The RNDIS
  gadget, the device and our drivers were all fine; the driver rebind /
  `authorized` / `usbreset` experiments were red herrings (they only reset
  counters).  `ufw` was also `default deny (incoming)` with no rule for the
  interface — a rule was added.  [report 267 §1]
* **hd3 had the broken `epitaph` (`r0`).**  It segfaulted ~120 ms after
  `Started Epitaph` — trap 292's exact symptom.  Installing the existing
  `epitaph-1.4.0-r10005.apk` (sha256 `aee3fdc8…`) fixes it; it now runs and logs
  its renderer.  It reports **`softpipe`** with `swrast_dri.so` (6.9 MB, amber),
  so the session-60 **llvmpipe replacement is absent from hd3** as STATE warned.
  [report 267 §2]
* **A Wayland client cannot reach the SGX, and the mechanism is now measured.**
  Surfaceless + `MESA_LOADER_DRIVER_OVERRIDE=pvr` →
  `GL_RENDERER = PowerVR SGX 540`; a Wayland client with the *same* override →
  `swrast_dri.so`/`softpipe`.  `libEGL_amber.so` advertises only the `wayland`
  platform, `pvr_dri.so` exports **zero** EGL symbols (it is a Mesa gallium DRI
  driver loaded by libEGL's DRM loader) and its only platform glue is
  `libdrm`/`drmGetNodeTypeFromFd`; the compositor reaches it through GBM/DRM.
  Trap 314 is correct and the override is powerless on the Wayland platform.
  [report 267 §3]

**Inferred / decided:** getting clients onto the GPU is a project, not a
config toggle — either a pvr Wayland EGL platform (wl_drm/linux-dmabuf), a
compositor that renders clients on the GBM/pvr path (pvrports' supported Phosh
+ `phoc`), or a dmabuf bridge.  Option 2 (Phosh/phoc) is the smallest first step
with a vendor-supported configuration to copy.  Keeping llvmpipe for clients and
pvr for the compositor is the status quo.  [report 267 §3]

**QNX answered:** the RNDIS gadget needs no per-OS change — it is a standard
USB Ethernet adapter, and both Linux `rndis_host` and QNX's RNDIS class driver
speak the same control protocol (hence `PB_LINK=door` and `PB_LINK=usb` both
reaching `169.254.0.1`).  Only the host side differs.

## Fourth part — the GPU session is planned (report 268)

The operator directed session 68 to GPU acceleration and supplied eight sources.
I surveyed all of them and turned the result into an ordered plan (report 268).

**Confirmed (measured this session, on the unit):**

* **The SGX works on libEGL's GBM/DRM platform.**  `gbm_create_device` on
  `/dev/dri/renderD129` (backend `drm`), then
  `eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, dev)` → `EGL 1.4`,
  `eglChooseConfig` → **12 window configs**, context current,
  `GL_VENDOR = Imagination Technologies`, `GL_RENDERER = PowerVR SGX 540`,
  `GL_VERSION = OpenGL ES 2.0 build 1.17@4948957`, and
  `EGL_MESA_image_dma_buf_export` **present**.  [report 268 §0]
* **A Wayland client still gets `swrast_dri`/`softpipe` even with
  `MESA_LOADER_DRIVER_OVERRIDE=pvr`**, and the override is visibly ignored.
  [report 267 §3]
* **A claim of mine from earlier in this session had to be withdrawn:** I said
  `libEGL_amber` advertised *only* the `wayland` platform.  That was a grep with
  `^anchor$` patterns against data that is not line-aligned — the real extension
  string lists **wayland, x11, gbm, surfaceless and device** (plus
  `EGL_WL_bind_wayland_display`).  Corrected in 267/268.  It also means
  `pvr_dri`'s missing `wl_*` symbols are **not** the explanation: `swrast_dri`
  has none either and works, because the winsys lives in libEGL.
* **The PFN/`vm_pgoff` bug class from the N9 port does not apply to us**:
  `mmap.h` defines `PVR_MAKE_ALL_PFNS_SPECIAL` whenever `VM_MIXEDMAP` is set, so
  the `remap_pfn_range`-with-`vm_pgoff` path is compiled out and we use
  `vmf_insert_mixed()`.  Hypothesis eliminated before spending bench time.
  [report 268 §4]

**Source value, one line each** (details in report 268 §1):
`zsavajji/pvr_sgx_rm696` = best *method* (N9 SGX530 on mainline 6.18 with the
closed userspace kept, a documented forward-port bug list and `check-abi.py`);
`pvrports` = our blobs' origin, its supported UIs are Phosh (their `phoc`) and
Plasma-Mobile, **not** catacomb; `The1TrueJoe/powervr_sgx` = fully open stack
with **SGX540 explicitly wanted** (long-term route; `docs/porting/` is the
bring-up method); `openpvrsgx-devgroup` = the kernel-only driver we vendor;
`Garnet/sgx540-reversing` = SGX540 ISA/PDS/microkernel RE tooling; the TI E2E
thread = historical only.

**Plan for session 68:** (1) a one-boot diagnostic of *why* Mesa's wayland
platform rejects pvr (`EGL_LOG_LEVEL=debug MESA_DEBUG=1
MESA_LOADER_DRIVER_OVERRIDE=pvr … epitaph`) — **do not patch Mesa before
that**; (2) prove the vendor-supported GBM path by bringing up pvrports'
`phoc`; (3) decide with data; (4) only then scope the open-stack SGX540 port.

## Artefacts produced

* Report **265** (`docs/reports/265-musb-comparator-lifetime-fix-2026-11-23.md`).
* Report **266** (`docs/reports/266-cpu-affinity-pinned-to-cpu0-2026-11-23.md`).
* Device-side (hd3 root, bench): `/etc/inittab` gains
  `::sysinit:/usr/sbin/taskset -p 3 1`; backups `/etc/inittab.session67-orig`
  and `/etc/inittab.fixed`.  No region-0 or region-1 change from this half.
* Report **267** (`docs/reports/267-host-vpn-ssh-and-wayland-sgx-2026-11-23.md`).
* Report **268** (`docs/reports/268-gpu-acceleration-survey-and-plan-2026-11-23.md`).
* Device-side (hd3 root, bench): `epitaph` upgraded `1.4.0-r0` →
  `1.4.0-r10005` (from `local/work/nura/work/packages/edge/armv7/`); the fixed
  binary is the only durable device change from the third part.
* Host-side (not part of the repository): `ufw allow in/out on
  enp8s0f3u1i2`; the WireGuard VPN must be off to reach `169.254.0.1`.
* Evidence (gitignored): `local/work/egltest.py`,
  `local/work/{pbcmd.py,reboot_verify.py}`,
  `local/work/session67-{cpuaffinity,state}.txt`, `session67-boot3-evidence.txt`,
  `local/work/flash-session67.log`, `local/work/export67/`.
* Patch re-export: **67 files / 9 136 lines**, sha256 `d19adff8…`;
  `kernel/source/` mirrors refreshed (`omap4-winchester.dts`,
  `phy-twl6030-usb.c`, `phy-omap-usb2.c`) and two stale ones corrected
  (`dsi.c`, `tc358764.c`).
* Region-0 image `boot/chainloader/kernel.img` — **`0x3547D613`**
  (10,448,868 B, sha256 `6ecd068d…`).
* Bench helpers/evidence (gitignored): `local/work/reboot_verify.py`,
  `local/work/flash-session67.log`,
  `local/work/session67-boot3-evidence.txt`,
  `local/work/{phy-twl6030-usb.c,phy-omap-usb2.c,omap2430.c,omap4-winchester.dts}.session67-orig`.
* Traps 339 added; DECISIONS §98; STATE/HANDOFF/ROADMAP/KNOWN-ISSUES/report
  INDEX updated.
