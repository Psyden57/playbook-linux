# Handoff — session 74 (absolute) → session 75

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 75 = Linux-era 69**, written at the
session-74 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 74 RESULT: the open `powervr_sgx` SGX540 microkernel STARTS on
> silicon and completes a NULL job; one or two jobs pass per GPU reset, then it
> stalls.**  `open microkernel invfix, interface 1.0, features 0x8` →
> `EDM init: event status 0xa0000000, BIF int 0x8000` → `microkernel started`,
> with the driver's own debugfs agreeing (`firmware running: 1`, `uk-lockups 0`,
> `BIF_FAULT 0`).  `submit-null-job` went from a universal **`consumed 0`**
> timeout to **`1/1 passed`**.  Four SGX545-shaped assumptions were corrected;
> the decisive one is `EUR_CR_PDS_CACHE_STATUS` (0x138) having **one**
> completion group, not two.  Report **277**; traps **384–387**;
> `docs/reference/sgx540-ddk18-facts.md`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**The long-term graphics route is the open `powervr_sgx` stack** (DECISIONS
§107/§108): the closed pvr DDK's GBM cannot export dmabufs, which is what blocks
the browser (report 275).  This session is that port.

## 0a. Session 74's result in brief (do not re-derive)

* **The driver accepts the image and the microkernel runs.**  `features 0x8`
  (SINGLE_CONTEXT set, SECURE clear) with both `ctlblk` and `pdtable` patches;
  then `microkernel started`.  The driver's debugfs: `firmware running: 1`,
  `hostctl init 0x1`, `uk-lockups 0`, `resets 0`, `BIF_INT_STAT 0x00008000`
  (`FLUSH_COMPLETE`, **no fault**), `BIF_FAULT 0`, `idle PD == kernel PD`.
* **It needs `edm_init=1`** to start at all — the driver's documented §9
  single-context launch (trap 385).  Now a boot default on hd3
  (`/etc/modprobe.d/powervr-sgx-opts.conf`).
* **Four assumptions corrected**, each gated on the per-core `SINGLE_CONTEXT`
  flag, with the SGX545 serial image kept byte-identical (`83b5b0a7…`):
  1. no `BIF_DIR_LIST_BASE1` (0xc38) and no `BIF_BANK0` (0xc78) on this core —
     one list, `DIR_LIST_BASE0` 0xc84;
  2. a HW context's `page_dir` word is a **slot** into the driver's PD table at
     `kernel_code.base + 0xf000`, not an address;
  3. single-context images must set `SINGLE_CONTEXT`, clear `SECURE` and carry a
     `pdtable` patch;
  4. **`EUR_CR_PDS_CACHE_STATUS` (0x138) has ONE completion group** (`CSC_INV`
     b0, `DSC_INV0` b1, `DSC_INV1` b2, `DSC_INV3` b4), so the SGX545's
     `0x27`/`0x4b` done masks could never be satisfied.  Mask `0x1F` is the fix
     that made the job complete.
* **`EVENT_PDS_ENABLE` (0x0a58)** is the real per-source dispatcher; the DDK's
  own `srvinit` derives **`0x2204f00f`** for this core and the driver was one bit
  short (`SW_EVENT`).  Fixed for the `sgx540` entry.
* **`/dev/mem` no longer reaches the GPU** once `powervr_sgx` owns it
  (trap 384) — userspace register reads are over in this configuration.
* **A GPL-2 DDK 1.8 drop** supplied by the operator carries the 540-specific
  header, `srvinit` and the full errata table; summarised in
  `docs/reference/sgx540-ddk18-facts.md`.  Its microkernel spec PDFs are
  "Strictly Confidential" and **unusable**.

## 0b. The one experiment for session 75

**Question: after the init task, does the microkernel's task run at all?**

Why it is the blocker: a passing NULL job is reported with **`consumed 0`** by
the driver on the same boot, and a kernel-CCB trace showed **not one** of the
microkernel's own stores running even for a job that passed — while one or two
jobs pass immediately after a GPU reset and then none do.  Until "the task runs"
is separated from "the driver accounts for it", every further register question
is built on sand.

**Inputs (all present and verified this session):**

* Region 0: **`0x02C1DDC2`** (`boot/chainloader/kernel.img`, 10,450,520 B,
  sha256 `70474c01…`), which carries the corrected microkernel
  (`image_crc32 0xc1eebfaf`, build `invfix`).  **Keep the file**; a rebuild from
  the same sources is not byte-reproducible (`CONFIG_LOCALVERSION_AUTO`).
* hd3: `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = the same `invfix` image
  (md5 `12f12abb297d8d0b8aebfb0b10467ee5`); the module `powervr_sgx.ko` =
  `2e138afc0e832a4e7b41d97f74452508` (rebuild from
  `local/work/powervr_sgx`: `make -C local/kernel-tree-sgx ARCH=arm
  CROSS_COMPILE=arm-buildroot-linux-gnueabihf- M=$PWD/local/work/powervr_sgx/drm/powervr_sgx modules`).
* `/etc/modprobe.d/powervr-open.conf` = `install pvrsrvkm_omap4_sgx540_120
  /bin/false`; `powervr-sgx-opts.conf` = `options powervr_sgx edm_init=1`.
* The uAPI test binary is **built and on the unit**:
  `/tmp/powervr_sgx_test`, `arm-buildroot-linux-gnueabihf-gcc -static` from
  `drm/tests/powervr_sgx_test.c` with `-Idocs/uapi -I$SYSROOT/usr/include
  -I$SYSROOT/usr/include/drm`.  `-l` lists the subtests.
* **Register access must come from inside the driver** (trap 384).  The cheapest
  forms: a temporary `pr_info` of the counter at the point the ISR fires, a
  `pvrsgx_debugfs` node, or the existing `/sys/kernel/debug/dri/1/status` +
  `ctlblk` dumps (both readable with the module loaded and nothing held).
* A `modprobe -r` is impossible while anything holds `card1` (`elogind` does), so
  **a module change costs a reboot**.

**Method.**  Add one counter the EDM advances per dispatch and the host can read,
then submit **two** NULL jobs back to back (the test submits one per
invocation): read the counter after job 1 and after job 2.

**What each result means:**

* **The counter advances for both jobs, and job 2 still fails** → the task runs;
  the fault is downstream (a wait, the DPM free list, the completion write).
  Next: the image's `UK_WAITLOG` (`+0x35` wait-site) on that failure.
* **The counter advances for job 1 only** → the EDM stops being dispatched after
  its first post-init task.  Next: `EVENT_PDS_ENABLE`/`EVENT_STATUS` read from
  *inside* the driver at job 2, plus `SC_TAFLIGHT`/`SC_INFLIGHT` (`0x10ce`/`0x14`)
  to see whether it is stuck in a defer branch rather than never entered.
* **The counter never advances, yet job 1 passes** → the pass does not come from
  the microkernel's completion path at all.  Then the driver's own accounting is
  the thing to trust, and the question becomes "what does the test's fence
  actually wait on".
* **`k_cleanup`'s force-complete marker (`0x3c = 0x0C1E`) appears** → the
  microkernel deferred and then force-completed; that is a different failure
  again and the defer counter (`0x1c`) is the next read.

## 0c. The media, and the way back

* **region 0 = `0x02C1DDC2`** — `boot/chainloader/kernel.img`, 10,450,520 B,
  sha256 `70474c01…`, media trailer `1f2dc8d7 c2ddc102` (read on media this
  session).  Kernel `7.3.3 #5 SMP`, ARMv7, dual-core 1008 MHz, no `maxcpus`,
  closed DDK off (`CONFIG_DRM_POWERVR_SGX=m`), `panic=0`, and the initramfs
  carries the corrected microkernel.
* **region 1 = `20a5edd3…`** (64 MiB at raw `0x29000000`) — the BusyBox fallback
  root, not booted.
* **hd3 = the session-71 rebaked image** `5d3eac8f…`, flashed at loader block
  `0x4915`, grown to 62.5 G; live root `/dev/loop0` ext4 rw.
* **To boot the ORACLE:** flash `local/work/ifs-shim-sealed.ifs` at block
  `0x825`; qconn `169.254.0.1:9090`, `PB_LINK=door tools/pbssh.sh`.
* **To stock TabletOS:** the 1917 IFS at `0x825` **and**
  `local/work/rcfs-1917.rcfs` at `0x8D5`, plus the vendor UFS at `0x4915` if hd3
  is repurposed.
* **Flash loop:** `python3 tools/bbusb.py flash --component <img>
  --dest-block 0x4915 --from-bootrom --password 0000 --apply`; **start the
  flasher, then reset with `reboot -f`**.  A region-0 flash is block `0x825`.
* **A region-0 flash needs the initramfs rebuilt too.**  `make … zImage` after
  `rm -f $K/usr/initramfs_data.cpio*` — and if the zImage does not relink, the
  cpio is embedded from the *old* one: this session lost a wrap to exactly that
  (the printed image CRC did not change).  Verify by extracting the firmware
  from the built cpio and comparing md5s.

## 1. Bench state at the session-74 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3 #5 SMP`**, ARMv7, dual-core 1008 MHz — not TabletOS |
| region 0 | CRC **`0x02C1DDC2`** — media trailer `1f2dc8d7 c2ddc102` (read this session).  File `boot/chainloader/kernel.img`, 10,450,520 B, sha256 `70474c01…` |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** (re-read this session) |
| hd3 | the session-71 rebaked image **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw, 56.5 G, 682 M used |
| `BOOTCTR_FCTRY` | **`0x1f`** (`dmesg \| grep pbrpmb`: `0x1f000000`, `>= 0x1e, no write`; `/init` tops up below `0x1e`) |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| battery | Full, 100 % (`VOLTAGE_NOW` 4 150 000 µV) |
| GPU | `card1`/`renderD129` = **`powervr_sgx`** (open driver), microkernel running; `card0`/`renderD128` = `omapdrm` |
| microkernel | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`invfix`**, md5 `12f12abb…`, 16,640 B; the session-73 raw 14,536 B image is beside it as `.session73-raw` (inert) |
| module | `/lib/modules/7.3.3/kernel/drivers/gpu/drm/powervr_sgx.ko` = `2e138afc…`; `.bak` (session 73) and `.brn31620` (the reverted experiment) beside it |
| desktop | `swrast` (the closed DDK is blocked by the operator's standing direction; the open driver is the work) |
| panel | 0 DSI errors, `bl_power` 0 |
| WLAN | **up** at **`192.168.0.231`** |
| SSH | `PB_LINK=usb tools/pbssh.sh` works; if it times out check the cable, the host VPN (trap 341) and ufw (trap 342) |
| device-side files | `/etc/modprobe.d/powervr-open.conf`, `/etc/modprobe.d/powervr-sgx-opts.conf`, the firmware above, the module above, `/tmp/powervr_sgx_test` (**`/tmp` is on the ext4**, trap 354 — the binary survives a reboot but is not in the flasher image) |
| host | clone `local/work/powervr_sgx` (`95f78ca` + our patches); assembler `local/work/sgxasm/sgx-asm`; trees `local/kernel-tree` (good) and `local/kernel-tree-sgx` (open driver); **GPL-2 DDK 1.8 reference at `/home/psyden/Pdrive/GFX_Linux_DDK-master` (outside the repo)** |

## 2. Working rules (unchanged)

* Read `docs/project/SAFETY.md` before any write.  One variable per boot.  An
  agent never writes RPMB; NVRAM read-only.
* Kernel printk goes to the panel; `console=ttyGS0` is inert.  Use `reboot -f`.
  **Ask the operator what is on the panel**; do not screenshot.
* **Do not restart `tinydm`** (trap 353), and **do not leave Kumo running or kill
  it** (trap 367) — reboot.
* The eMMC can log `Card stuck being busy`; a power cut can damage the rw ext4
  (trap 336).  **Do not call `sync`** (trap 300).  `/tmp` is on the ext4
  (trap 354).
* **Register reads:** only from inside the driver now (trap 384); the userspace
  `/dev/mem` path is closed in this configuration.
* **Revert experiments and prove the revert.**  Look up every external fact.
  Write the durable fact into the repository in the same turn.

## 3. Useful local extracts

* `docs/reference/sgx540-factsheet.md` — the SGX540 facts, **C** where measured.
* `docs/reference/sgx540-ddk18-facts.md` — every K fact from the DDK 1.8 drop,
  with the closed-material boundary and two retractions.
* `docs/captures/sgx540-ukernel-{list0,start,first-job}-2026-11-26.txt` — this
  session's three stages, verbatim.
* `rootfs/powervr-sgx/` — the patch series (`0001` core entry, `0002` the
  one-context path), `sgx540.py`, `README.md`, `firmware/`.  **Nothing is
  upstream** (DECISIONS §110).
* `drm/tests/powervr_sgx_test.c` — the uAPI suite; `submit-null-job` is the gate.
  Build recipe in `docs/reports/277` and `§0b` above.
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`,
  `test_emu.py` (8/8 per core), `golden/check.py` (the SGX545 byte-identity
  gate), `cores/{sgx540,sgx545}.py` (all per-core values live here).
* Reports **277** (this session), 276 (the scope, with its session-73 addendum
  and §8), 275 (the Kumo negative), 274 (the browser build).

## 4. What could not be verified

* **That the EDM dispatches more than once.**  Measured only as "one or two
  NULL jobs pass per GPU reset, then none", which is a behaviour, not a
  mechanism (§0b is how to settle it).
* **Why a passing job is reported as `consumed 0`.**  Both statements come from
  the same boot and are not yet reconciled.
* **Any microkernel register table.**  No kick has ever been *exercised*: the
  values 0x618, 0x62c, 0x694, TA 0x200, 3D 0x428 remain carried-over
  hypothesis, because the EDM does not dispatch far enough to use them.
* **`0xb14`'s write being harmless.**  The K facts say it is a USSE service
  queue on this core; whether the image's write also *consumes* something is
  unmeasured.
* **`USE_CODE_BASE` presence on rev-120 silicon** — the unit is settled (shift
  8, DM at 24, from the DDK's own derivation); the existence is not.
* **The panel** after the final reboot: not confirmed visually by the operator
  this session; 0 DSI errors, and `grim` is not evidence either way (trap 377).
* Whether the DDK 1.8's ~50 rev-120 errata matter for a *microkernel* port
  beyond the three marked "workaround in uKernel".

## 5. What could shorten the port

* The DDK 1.8 drop's `eurasia/services4/srvinit/devices/sgx/sgx_mkif.h` is a
  primary source for every `ta3dctl` field (report 277 §4 has the derived
  table); the image's own offsets above `0x5c` diverge from it and should be
  checked field by field.
* The same drop ships the vendor microkernel as **C source**
  (`pbuKernelProgram[]` + `ukernel_labels.h`): a per-command entry table, not a
  single internal dispatcher.  Reading it is legitimate (source, GPL-2) and may
  answer §0b directly.
* The DDK's `policy`/`sgxinit.c` derives several values we hard-code; each is a
  candidate for the "carried-over hypothesis" list.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 75 (absolute; Linux-era 69)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 368-387) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 108-111, docs/reports/INDEX.md,
reports 277 (this session), 276 (graphics route + addendum), 275 (Kumo),
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-ukernel-{list0,start,first-job}-2026-11-26.txt,
docs/sessions/session-74.md and session-73.md.

WHERE IT STANDS: Linux 7.3.3 #5 SMP ARMv7, dual-core 1008 MHz; region 0
0x02C1DDC2 (trailer 1f 2d c8 d7 c2 dd c1 02; keep the image FILE — it is not
byte-reproducible), region 1 20a5edd3..., hd3 = 5d3eac8f flashed at 0x4915 and
grown to 62.5 G; BOOTCTR_FCTRY 0x1f; card1/renderD129 = powervr_sgx with its
microkernel RUNNING, card0/renderD128 = omapdrm; desktop on swrast; wlan0 up
192.168.0.231; 0 DSI errors.

THE OPEN SGX540 PORT RUNS: the driver accepts the image
("open microkernel invfix, interface 1.0, features 0x8"), starts it
("EDM init: event status 0xa0000000, BIF int 0x8000", "microkernel started") and
its debugfs agrees (firmware running: 1, uk-lockups 0, BIF_FAULT 0).  The NULL
job went from "consumed 0" to 1/1 passed.  Four SGX545-shaped assumptions were
corrected this session -- one directory list (no 0xc38/0xc78), page_dir is a
PD-table slot, SINGLE_CONTEXT feature bits, and (the decisive one)
EUR_CR_PDS_CACHE_STATUS 0x138 has ONE completion group so 0x27/0x4b could never
be satisfied.  It needs modprobe powervr_sgx edm_init=1 (the driver's §9
single-context launch; now a boot default).  NOTHING IS PUBLISHED UPSTREAM
(DECISIONS 110).

WHAT REMAINS: one or two jobs pass per GPU reset, then none; a passing job is
still reported as "consumed 0"; and a kernel-CCB trace showed NONE of the
microkernel's own stores running even for a passing job.  HANDOFF §0b is the one
experiment: instrument ONE counter the EDM advances per dispatch, read it from
inside the driver (the /dev/mem path is CLOSED now -- trap 384), submit two NULL
jobs, and interpret against the four outcomes listed there.  Do not ask another
register question before that.

TRAPS THAT COST TIME: /dev/mem cannot reach the GPU once powervr_sgx owns it
(Errno 13; /proc/iomem shows the reservation; trap 384) -- register evidence must
come from inside the driver.  edm_init=1 is required to start the microkernel and
is a supported path, not a hack (trap 385).  A modprobe -r is impossible while
elogind holds card1, so a module change costs a reboot.  Control-block words are
feature-flag dependent: 0x3a/0x3b belong to UK_WAITLOG and 0x3c to the cleanup
marker, so diff every candidate word against cores/<core>.py before
instrumenting (trap 386).  Build only from local/kernel-tree or a copy of it
(trap 378), rebuild the initramfs cpio or a firmware change is silently
dropped, and check that the zImage actually relinked.  Never unbind pvrsrvkm
live (trap 383).  grim captures the compositor's buffers, not the panel.

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw,
56.5 G; BOOTCTR 0x1f; nproc 2, PID 1 affinity 0,1; battery Full 100 %
(4.150 V); panel 0 DSI errors; wlan0 up 192.168.0.231.  SSH: PB_LINK=usb
tools/pbssh.sh works; if it times out check the cable, the host VPN (trap 341)
and ufw (trap 342) first.  The uAPI test binary is already at
/tmp/powervr_sgx_test (rebuild from drm/tests/powervr_sgx_test.c, static, with
-Idocs/uapi -I$SYSROOT/usr/include -I$SYSROOT/usr/include/drm).  Kernel printk
goes to the panel; use reboot -f; ask the operator for visual checks; do not
screenshot; do not restart tinydm (trap 353) and do not kill Kumo (trap 367).
An agent never writes RPMB; NVRAM read-only.  Revert experiments and prove the
revert; look up every external fact; do not call sync (trap 300); /tmp is on the
ext4 (trap 354).  There is NO RAM-boot path: region-0 IFS flash is the only way
to boot a kernel.

SAFETY: read SAFETY.md first.  One variable per boot.  Flash region 0 only when
only it changed; both regions in one bootrom session only when both changed.
Start the flasher, then reset with reboot -f; the bootrom holds ~20 s.  The hard
hazards (BOOTCTR_FCTRY 0 / >0x32 / failed write / flat pack) remain.

THE GPL-2 DDK 1.8 DROP IS THE BEST NEW SOURCE: /home/psyden/Pdrive/
GFX_Linux_DDK-master (outside the repo).  Its eurasia/hwdefs/sgx540defs.h is the
540-specific header, its services4/srvinit has the real init sequence and the
microkernel as C source, and docs/reference/sgx540-ddk18-facts.md summarises what
is settled.  Its microkernel spec PDFs are "Strictly Confidential" and must NOT
be used as evidence.

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.  WRAP-UP: only the operator
decides when the session stops; then record the bench state, write the session
note, update the canonical docs, verify with tools/docs_audit.py and
tools/tests/test_offline.py, commit privately and publish the snapshot
(docs/project/REPO-MANAGEMENT.md).  No restore is required.
```
