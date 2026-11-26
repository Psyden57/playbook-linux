# Handoff — session 73 (absolute) → session 74

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 74 = Linux-era 68**, written at the
session-73 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 73 RESULT: the open SGX540 port reached hardware — the driver BINDS
> and IDENTIFIES the core, and the first microkernel builds; the next blocker is
> the microkernel's list-0 / PD-table path.**  Measured identity: `CORE_ID` id
> **0x0114**, `CORE_REVISION` **`0x00010200`** (Rev 120), ti-sysc `rev`
> **`0x40000000`**.  On a flashed image the open driver logged `SGX core id
> 0x01140000 revision 0x00010200: TI OMAP4430 (SGX540 1.2.0)` — **C facts on
> silicon**.  The SGX540 microkernel assembles (3-byte delta from the SGX545
> image) and passes all 7 offline tests, but the driver rejects it: it is a
> **two-context `srvinit` image** and a one-context core needs a `pdtable` patch
> and a list-0 load.  DECISIONS **§109/§110/§111**; traps **368–382**; captures
> `docs/captures/sgx540-{identity,bind,ukernel}-2026-11-26.txt`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**Done.**  Everything through session 72: the SoC, display, hd3/Nura root,
dual-core 1008 MHz, SGX540 with the desktop and its clients, the pitch fix, the
rebaked/flashed hd3 image, and WPE WebKit 2.54.3 (fork) + Kumo 1.10.0 built and
installed.  Session 72 proved the browser is **blocked on the closed pvr DDK**
(its GBM cannot export dmabufs — report 275), fixing the direction: the open
`powervr_sgx` stack.

**Session 73** did Track A's zero-risk first step (§0a): the SGX540 identity read
and a re-read of the upstream tree, which turns the port from "multi-session of
new driver code" into "data plus a captured init script".

## 0a. Session 73's result in brief (do not re-derive)

* **The identity is measured** (read-only, `/dev/mem`, `CONFIG_STRICT_DEVMEM`
  off, `pvrsrvkm` bound): `CORE_ID` at 0x0020 = `0x01140000` → id **0x0114**;
  `CORE_REVISION` at 0x0024 = **`0x00010200`** (Rev 120, the DT binding's value);
  `DESIGNER_REV_FIELD1/2` **0**; ti-sysc `rev` **`0x40000000`** / `sysc`
  **`0x0000003c`**; `BIF_INT_STAT` at idle `0x00008000` (`FLUSH_COMPLETE`).
  [capture `docs/captures/sgx540-identity-2026-11-26.txt`; tool
  `tools/bench/gpu/sgxcoreid.py`]
* **The id field 0x0114** distinguishes the 540 from the SGX545 (0x0115) and the
  SGX530 (0x0112) — the driver-usable fact.
* **The upstream tree has moved past report 276** (`95f78ca`, 2026-11-25):
  `pvrsgx_core_family_usable()` accepts **`va_bits` 28 or 32** and
  **`num_dir_lists == 1`** for a core without `MULTIPLE_MEM_CONTEXTS`; the SGX530
  is a **full family entry** (`sgx530_regs`, 28-bit `sgx530_heaps`,
  `pvrsgx_cmd_core_sgx530`, `PVRSGX_QUIRK_BRN_28889`); `pvrsgx_of.c` already
  handles a TI `ti-sysc` parent; the single-context case runs with
  `powervr_sgx.allow_unisolated=1`.  [traps 370/371]
* **The errata list report 276 quoted is the SGX543's.**  Rev 120 has **three**
  BRNs: `26620`, `28011`, `34028`.
* **`USE_CODE_BASE` is 24-bit on the 540**, unlike the 535/545's 25-bit — a 540
  entry must not copy the 545's shifts; whether the registers exist on rev 120 is
  still a measured gap.
* **The fact sheet is finalised** with **C** facts and the corrected register,
  errata and `USE_CODE_BASE` facts; the contradiction file has the 24-vs-25-bit
  evidence.  [DECISIONS §109]
* **Operational:** `/dev/mem` `mmap` needs a **page-aligned** offset (trap 368);
  read only the registers the guide names (trap 369).

## 0b. The two tracks for session 74 (operator's choice)

**Track A (durable, chosen) — the SGX540 microkernel, on to a kick.**  The tree is
`local/work/powervr_sgx` (gitignored, upstream `95f78ca` plus our patches).
**Nothing goes upstream until the core draws** — no issue, no PR (DECISIONS §110).

0. **ALREADY DONE, do not redo:**
   * The **driver binds and identifies** the core on hardware (C facts) — see §0
     and `docs/captures/sgx540-bind-2026-11-26.txt`.
   * The **microkernel builds** for sgx540, differs from the SGX545 image by 3
     bytes, passes all 7 offline tests, and the SGX545 golden hash is intact.
   * Learn from my mistakes: build from `local/kernel-tree` or a **copy** of it,
     never `local/pristine/linux-7.3.3` (trap 378, fell back to bootrom); block
     the closed DDK with a modprobe.d `install … /bin/false`, not the kernel
     `modprobe.blacklist=` parameter (trap 381); the firmware goes in the
     **initramfs** (trap 380); never unbind `pvrsrvkm` live (it oopses).
1. **THE NEXT STEP (§4 in full): give the microkernel a list-0 / PD-table path.**
   Add the `pdtable` patch site to `ukernel/ukernel.py` + `cores/sgx540.py`, load
   directory list 0 from it, set the single-context feature bits (drop `SECURE`,
   set `SINGLE_CONTEXT`), wrap with `drm/tools/mkukfw.py --manifest`, embed in the
   initramfs, flash, and re-audit the ~39 references to `list 1`/`BANK0`/`0xc78`.
2. Then a **kick**, and only then do the carried-over register tables (0x618,
   0x62c, 0x694, TA 0x200, 3D 0x428) get tested — they are hypothesis, not fact.
3. **`sgx540_init_regs`** — capture from a working microkernel boot.
4. **`USE_CODE_BASE`** (Gap 7) — inside the driver's reset sequence, never a
   userspace read (trap 376).
5. Then Mesa.

**Track B (queue — smaller, durable items).**
1. **The next hd3 rebake must add `wpewebkit` + `kumo`** (live root only, not in
   the flashed image) and the `pvr-dbmfix` shim if not yet an APK.
2. **`pvr-dbmfix` APK** — package the pitch shim (today `/usr/local` files).
3. **The GEM growth** — the idle desktop reaches ~25.6 MiB pinned; the 9.7 MB
   objects are unexplained (a per-object walk, clients stopped).
4. **trap 350** (cosmetic glitches) and a **heavy-redraw/frame-latency**
   measurement (unblocked — the TILER pressure is gone).
5. The **CPU** items: 1.35 V-for-all-OPPs vs a real supply, the kernel-side PID-1
   affinity fix, the memory-throughput re-measure.
6. The **USB teardown** (trap 337) capture.
7. **The boot-time DSI wedge** (`NO_PACKET_SENT`, `polls=2000` vs a healthy
   `polls=1`; trap 377) — a display item, not a GPU one.

## 0c. The media, and the way back

* **region 0 = `0xBC5FB130`** — image file `local/work/kernel-restore-20261126.img`
  (10,452,788 B), kernel `7.3.3 #111 SMP`, ARMv7, dual-core 1008 MHz, no
  `maxcpus`, stock MUSB mailbox + comparator fix, `panic=0`.  Media trailer
  `1f 2d c8 d7 30 b1 5f bc`, **read on media this session**.  It replaced
  `0x3547D613` (`#109`) because `CONFIG_LOCALVERSION_AUTO=y` embeds
  `git describe`, so the older image cannot be rebuilt byte-exactly — **keep the
  image file, not just the CRC**.  Same config and same `uname -r` (`7.3.3`), so
  hd3's modules still load.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox fallback
  root, not booted.
* **hd3 = the session-71 rebaked image** `5d3eac8f…` (625,999,872 B), flashed at
  loader block `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4.  It
  carries catacomb r10003, epitaph r10008, the pitch shim, the 842-module ARMv7
  tree and the permissions; **it does not carry `wpewebkit`/`kumo`**.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if hd3
  is repurposed.  No restore is required at this wrap-up.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`** (a warm reset enters the bootrom).  A
  region-0 flash is block `0x825`; both regions in one bootrom session only when
  both changed.

## 1. Bench state at the session-73 close (re-measured on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3 #111 SMP`**, ARMv7, dual-core 1008 MHz — not TabletOS |
| region 0 | CRC **`0xBC5FB130`** — media trailer `1f 2d c8 d7 30 b1 5f bc` (read this session).  Image file: `local/work/kernel-restore-20261126.img`.  `0x3547D613` is NOT reproducible (`CONFIG_LOCALVERSION_AUTO`) |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** (re-measured) |
| hd3 | the session-71 rebaked image **`5d3eac8f…`**, flashed at `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4 rw, 56.5 G |
| `BOOTCTR_FCTRY` | **`0x2c`** (`dmesg \| grep pbrpmb`; `/init` tops up below `0x1e`) |
| CPU | `nproc` 2, `taskset -p 1` → **3** |
| battery | Full, 4.149 V, 100 % |
| GPU | `pvrsrvkm_omap4_sgx540_120` owns `56000000.gpu`; `card1`/`renderD129`; core id 0x0114 / rev 0x00010200 |
| desktop | catacomb + `tabula`/`epitaph`/`wvkbd-mobintl` running, clients on **`pvr_dri`**; **Kumo not running** |
| panel | `connected`, `bl_power` 0, **0 DSI errors** |
| WLAN | **up** at **`192.168.0.232`** |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; no door (Linux image, trap 88); if it times out check the cable, the host VPN (trap 341), ufw (trap 342) |
| device-side, inert | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin`; modules `powervr_sgx`, `drm_exec`, `drm_gpuvm`, `drm_shmem_helper`, `gpu-sched` in `/lib/modules/7.3.3/...`.  Reverted: `/etc/modprobe.d/powervr-open.conf`, `/etc/modules-load.d/powervr_sgx.conf` |
| host | clone `local/work/powervr_sgx` (`95f78ca` + patches); assembler `local/work/sgxasm/sgx-asm`; test trees `local/kernel-tree` (good), `local/kernel-tree-sgx` (open driver), `local/work/kbench` |

## 2. Working rules (unchanged)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only; never program WDT2 from the
  chainloader (trap 322).
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or kill
  it** (trap 367) — reboot.
* The eMMC can log `Card stuck being busy`; a power cut can damage the rw ext4
  (trap 336).  **Do not call `sync`** (trap 300).  `/tmp` is on the ext4, not
  tmpfs (trap 354).
* **GPU reads:** only the registers the upstream guide names are safe before the
  reset sequence (trap 369); `/dev/mem` `mmap` needs a page-aligned offset
  (trap 368).  A blind register sweep can hang the bus.
* **Revert experiments and prove the revert.**  Look up every external fact.
  Time-box subagents (trap 109).  Write the durable fact into the repository in
  the same turn.

## 3. Useful local extracts

* `docs/reference/sgx540-factsheet.md` — the SGX540 facts, **now with C facts**
  (identity, module `rev`, idle `BIF_INT_STAT`) and the corrected errata /
  `USE_CODE_BASE`; report 276 (with its session-73 addendum and §8) is the scope.
* `docs/captures/sgx540-identity-2026-11-26.txt` — the raw identity read.
* `tools/bench/gpu/sgxcoreid.py` — the read-only identity probe (extends easily
  to a named-register test).
* `local/work/powervr_sgx/` — the upstream tree at `95f78ca` (gitignored), with
  `drm/powervr_sgx/{pvrsgx_core.c,pvrsgx_of.c,pvrsgx_cmd_defs.c}`, the porting
  guides, and `kernel/src/services4/srvkm/hwdefs/sgx540defs.h`.
* `tools/bench/gpu/` — `gbmplane.py`, `gbmprime.c`, `gbmprime2.c`, `ioctllog.c`
  (session-72 probes) and `dbmfix.c` (the pitch shim).
* `local/work/nura/pmaports/main/{wpewebkit,kumo}/` — the browser packages;
  `local/work/nura/work/packages/edge/armv7/` — the APKs.
* `local/work/nura/images/blackberry-playbook-root-gpu-20261125.img` — the
  flashed hd3 image (`5d3eac8f…`); `tools/rebake_hd3.py` / `rebake-hd3.sh`.
* `kernel/vendor/pvrsgx-1.17.4948957-101c687a.tar.xz` — the vendored SGX540 DDK
  (GPL-2 headers: `sgx540defs.h`, `sgxfeaturedefs.h`, `sgxerrata.h`, `sgxmmu.h`).
* `local/work/ifs-shim-sealed.ifs` — the way back to TabletOS (block `0x825`);
  `local/work/rcfs-1917.rcfs` (0x8D5); `local/work/ufs-1917-devuser.ufs` (0x4915).
* Reports **276** (open-stack scope + addendum), **275** (the Kumo runtime
  negative), 274 (browser build), 272 (rebake), 271 (pitch fix), 270 (trap 349).

## 4. The one thing to start with

**Give the SGX540 microkernel a list-0 / PD-table path**, so `powervr_sgx` will
accept the image.  Everything else is already in place: the driver binds and
identifies the core, and the image builds.

Why it is the blocker: `mkukfw.py` (the wrapper the driver requires) rejects an
image that is not `SGX5`/`UKRN`-headered, and for a **single-context** core it
additionally requires `SINGLE_CONTEXT` set, `SECURE` clear, **and a `pdtable`
patch** — the driver allocates a PD table at `kernel_code.base + 0xf000` and the
microkernel must **load directory list 0 from it**.  `ukernel/ukernel.py` cannot:
it routes TA/3D to **list 1** via `BIF_BANK0` and assumes list 0 is `srvinit`'s
shared kernel PD.

Exact inputs:
* `local/work/powervr_sgx` (upstream `95f78ca` + `rootfs/powervr-sgx/0001-…patch`
  + `rootfs/powervr-sgx/sgx540.py`); assembler at `local/work/sgxasm/sgx-asm`
  (`SGX_ASM=…`); build with `python3 ukernel/build.py sgx540`.
* Wrap with `drm/tools/mkukfw.py IMAGE -o OUT --manifest JSON` (it reads
  `entries.event`, `patches.ctlblk`, `features`, `build_id`); the `pdtable` patch
  must be added to the manifest first.
* Embed the result in `initramfs/root/lib/firmware/powervr_sgx/` (trap 380), rebuild
  from `local/kernel-tree-sgx`, flash region 0.
* Block the closed DDK with `install pvrsrvkm_omap4_sgx540_120 /bin/false` in a
  modprobe.d file (trap 381) — the kernel `modprobe.blacklist=` parameter does not
  work for OpenRC's exact-name load.
* Guard for the next tree problem: build from `local/kernel-tree` or a copy of it,
  never `local/pristine/linux-7.3.3` (trap 378).

What each result means:
* **`powervr_sgx … open microkernel <id>, interface 1.x, features 0x…`** then a
  probe that completes → the image is accepted and the core is running our code.
  Next: a kick, and only then do the carried-over register tables get tested.
* **`not a microkernel file`** → the `mkukfw` header is still wrong (magic, CRC, or
  a table past the image).
* **`not an image for a single-context core`** → the feature bits; `SINGLE_CONTEXT`
  must be set and `SECURE` clear.
* **`a PD table patch on a multi-context core`**, or a BIF fault with
  `DIR_LIST_BASE0` idle → the list-0 load is not wired.

Do **not** probe `USE_CODE_BASE` from userspace (trap 376) and do **not** unbind
`pvrsrvkm` live (it oopses in `PVRLinuxFenceDeInit`).  Publish nothing upstream
before a frame (DECISIONS §110).

## 5. What could not be verified
## 5. What could not be verified

* Whether `USE_CODE_BASE` exists on our rev-120 silicon (needs a write/read-back).
* `OCP_DEBUG_CONFIG` bit 31 (`THALIA_INT_BYPASS`) after module enable (a read,
  not done).
* The 28-bit heap layout the SGX540 needs, and the DPM/TA/ISP/TSP/PBE/PDS state.
* Kumo's RSS/first-render/touch behaviour (it never presents — report 275).

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 74 (absolute; Linux-era 68)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 368-382) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 109-111, docs/reports/INDEX.md,
reports 276 (graphics route, with its session-73 addendum and §8), 275 (the Kumo
runtime result), docs/reference/sgx540-factsheet.md (has C facts and the
microkernel section), docs/captures/sgx540-{identity,bind,ukernel}-2026-11-26.txt,
docs/sessions/session-73.md and session-72.md.

WHERE IT STANDS: Linux 7.3.3 #111 SMP ARMv7, dual-core 1008 MHz; region 0
0xBC5FB130 (trailer 1f 2d c8 d7 30 b1 5f bc; 0x3547D613 is NOT reproducible because
CONFIG_LOCALVERSION_AUTO embeds git describe -- keep the image FILE), region 1
20a5edd3..., hd3 = the session-71 rebaked image 5d3eac8f flashed at 0x4915 and
grown to 62.5 G; BOOTCTR_FCTRY 0x2c; pvrsrvkm owns the GPU, desktop up, 0 DSI
errors; wlan0 up at 192.168.0.232.

THE OPEN SGX540 PORT REACHED HARDWARE (session 73): the driver BINDS and
IDENTIFIES the core — "SGX core id 0x01140000 revision 0x00010200: TI OMAP4430
(SGX540 1.2.0)" — so its regs offsets, core_id match and cores[] lookup are C
facts.  The SGX540 microkernel BUILDS (ukernel/cores/sgx540.py; 3-byte delta from
the SGX545 image; all 7 offline tests pass; the SGX545 golden hash is intact) but
the driver REJECTS it: it lacks the SGX5/UKRN header and, for a single-context
core, the pdtable patch -- the driver allocates a PD table at
kernel_code.base+0xf000 and the microkernel must load directory list 0 from it,
but the image routes TA/3D to list 1 and assumes list 0 is srvinit's shared kernel
PD.  It is a two-context srvinit image.  NOTHING IS PUBLISHED UPSTREAM
(DECISIONS 110).

TRAPS THAT COST TIME IN SESSION 73 (read them): build only from local/kernel-tree
or a copy of it, NEVER local/pristine/linux-7.3.3 -- it has no PlayBook support
and the unit falls back to bootrom (trap 378).  The GPU firmware must be in the
INITRAMFS; the probe runs at ~0.9 s, before hd3 is mounted (trap 380).  The kernel
modprobe.blacklist= parameter does NOT stop OpenRC's exact-name load; use a
modprobe.d "install <mod> /bin/false" (trap 381).  NEVER unbind pvrsrvkm live -- it
oopses in PVRLinuxFenceDeInit and the rebind deadlocks; use a boot with it blocked
(trap/decisions).  Do not probe USE_CODE_BASE from userspace (trap 376).  grim
captures the compositor's buffers, not the panel.

THE NEXT WORK — two tracks, operator's choice.
TRACK A (durable, chosen): give the microkernel a list-0 / PD-table path.  Add the
pdtable patch site to ukernel/ukernel.py + cores/sgx540.py, load directory list 0
from it, set SINGLE_CONTEXT and clear SECURE, wrap with drm/tools/mkukfw.py
--manifest, embed in initramfs/root/lib/firmware/powervr_sgx/, rebuild from
local/kernel-tree-sgx, flash region 0 with the closed DDK blocked.  Then a kick --
and only then do the carried-over register tables (0x618, 0x62c, 0x694, TA 0x200,
3D 0x428) get tested; they are hypothesis, not fact.  See HANDOFF §4 for what each
dmesg result would mean.
TRACK B (queue): the next hd3 rebake must add wpewebkit + kumo (+ pvr-dbmfix);
package pvr-dbmfix; the GEM-growth walk; trap 350 + a heavy-redraw measurement; the
CPU items; the USB teardown; the boot-time DSI wedge (trap 377).

BENCH: region 0 0xBC5FB130; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw,
56.5 G; BOOTCTR 0x2c; nproc 2, PID 1 mask 3; battery Full 4.149 V; panel
connected, bl_power 0, 0 DSI errors; wlan0 up 192.168.0.232.  SSH: PB_LINK=usb
tools/pbssh.sh works; if it times out check the cable, the host VPN (trap 341) and
ufw (trap 342) first.  Kernel printk goes to the panel; use reboot -f; ask the
operator for visual checks; do not screenshot; do not restart tinydm (trap 353)
and do not kill Kumo (trap 367).  An agent never writes RPMB; NVRAM read-only.
Revert experiments and prove the revert; look up every external fact; the eMMC can
log Card stuck being busy and do not call sync (traps 336/300); /tmp is on the
ext4 (trap 354).  GPU reads: only the guide's named-safe registers; /dev/mem mmap
needs a page-aligned offset (traps 369/368).  There is NO RAM-boot path:
region-0 IFS flash is the only way to boot a kernel.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.  The hard
hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
