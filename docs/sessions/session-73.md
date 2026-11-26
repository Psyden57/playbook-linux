# Session 73 (absolute) — the SGX540 open port: identity measured, entry bound on hardware, microkernel blocked by list-0

> Numbering: absolute `73` (the project's chronological scheme — see
> `NUMBERING.md`); the reports call this **Linux-era session 67**
> (`absolute = Linux-era + 6`).  Continues session 72, which proved the closed
> pvr GBM cannot export dmabufs (the Kumo wall) and scoped the open
> `powervr_sgx` SGX540 port.

Date: 2026-11-26.  Unit at the end: **Linux `7.3.3 #111 SMP`** on hd3, region 0
**`0xBC5FB130`**, region 1 `20a5edd3…`, `BOOTCTR_FCTRY` **`0x2c`**, `nproc` 2 /
PID 1 mask 3, desktop up on `pvr_dri` (`pvrsrvkm` bound), `wlan0` up
(`192.168.0.232`), **0 DSI errors**, battery Full 4.149 V.

## Goal of this session

Track A of HANDOFF-72 §0b: the durable graphics route — the open `powervr_sgx`
SGX540 port.  The handoff's first zero-risk step was to read `CORE_ID`/
`CORE_REVISION` (0x0020/0x0024, expect `0x00010200`) with the vendor DDK clocking
the GPU and finalise `docs/reference/sgx540-factsheet.md`; it also asked for an
upstream "New SGX core" issue, which the operator **withheld until the core runs
on hardware** (DECISIONS §110).  The session then went past that: entry built,
flashed, bound on silicon, and the microkernel attempted.

## Confirmed (measured, read in a file, or seen on the bench)

### The SGX540 identity — first hardware read

Read-only 4-byte loads through `/dev/mem` (`CONFIG_STRICT_DEVMEM` is **off** in
`kernel/winchester-7.3.3.config`), while `pvrsrvkm_omap4_sgx540_120` clocked the
GPU.  Tool `tools/bench/gpu/sgxcoreid.py`; capture
`docs/captures/sgx540-identity-2026-11-26.txt`.

| register | value | meaning |
|---|---|---|
| `CORE_ID` (0x0020) | `0x01140000` | id **0x0114** — SGX545 is 0x0115, SGX530 0x0112 |
| `CORE_REVISION` (0x0024) | `0x00010200` | major 1, minor 2, maint 0 → **Rev 120**, as the DT binding says |
| `DESIGNER_REV_FIELD1/2` (0x0028/0x002c) | `0`, `0` | |
| ti-sysc `rev` (0x5600fe00) | `0x40000000` | what `ti-sysc.c` matches for `SYSC_MODULE_QUIRK_SGX` |
| ti-sysc `sysc` (0x5600fe10) | `0x0000003c` | |
| `BIF_INT_STAT` (0x0c04) | `0x00008000` | bit 15 `FLUSH_COMPLETE` only — idle BIF |
| `CLKGATECTL`/`2`, `CLKGATESTATUS`, `CLKGATECTLOVR`, `SOFT_RESET`, `EVENT_STATUS`/`2`, `EVENT_HOST_ENABLE`, `BIF_CTRL` | `0` each | quiescent; confirms the offsets, not bit meaning |

Read set = exactly the registers `docs/porting/kernel.md` §2 calls safe, plus the
identity group and read-only `BIF_INT_STAT`.  The write-only `*_CLEAR` registers
were not read; nothing past 0x0c08 was touched.

### The open driver BINDS and IDENTIFIES the core on hardware

Two flashed region-0 boots of an image built from `local/kernel-tree-sgx`
(a copy of `local/kernel-tree`, `mrproper` first, closed DDK off;
DECISIONS §111):

```
powervr_sgx 56000000.gpu: [drm] SGX540 has one memory context: clients are not
                               isolated from the firmware or from each other
                               (allow_unisolated=1)
powervr_sgx 56000000.gpu: [drm] SGX core id 0x01140000 revision 0x00010200:
                               TI OMAP4430 (SGX540 1.2.0)
```

The first boot, with `allow_unisolated` unset, refused the core with the exact
designed message; the second, with `powervr_sgx.allow_unisolated=1` in the DTB
bootargs, proceeded.  So the `regs` offsets, the `core_id` match and the
`cores[]` lookup are **C** facts on silicon.  Capture
`docs/captures/sgx540-bind-2026-11-26.txt`.

### The SGX540 microkernel builds and passes its offline tests

* `ukernel/cores/sgx540.py` assembles; its image differs from the SGX545's by
  **exactly three bytes** — `MISC_CORE` 545→540, `CORE_ID` 0x1c→0x20,
  `CORE_REVISION` 0x20→0x24 (verified byte-diff, offsets 0x5b8/0x5c0/0x600).
* `sgx-asm` had to learn the core: `SGX_ISA_CORE_SGX540`, one clause in
  `sgx_isa_core_supported()`, and the `<gen>` range widened 54500-54599 →
  54000-54599.
* **The SGX545 image stayed byte-identical**: sha256 `83b5b0a7d3c4b74eba6524c24
  c962cd235eb700b158ede6339503046f96e7d79`, its `ukernel/golden/sgx545.sha256`.
* `ukernel/test_emu.py` now takes a core argument; **all 7 tests pass** for
  `sgx540` (and still for `sgx545`).
* Image: 14,536 B, `core sgx540`, features 2 (SECURE); archived at
  `rootfs/powervr-sgx/firmware/sgx540_ukernel.bin`.

### On the unit, the driver FINDS the microkernel but rejects it

After embedding the image in the initramfs and blocking the closed DDK:

```
powervr_sgx 56000000.gpu: [drm] SGX core id 0x01140000 revision 0x00010200:
                               TI OMAP4430 (SGX540 1.2.0)
powervr_sgx 56000000.gpu: [drm] *ERROR* powervr_sgx/sgx540_ukernel.bin:
                               not a microkernel file
powervr_sgx 56000000.gpu: probe with driver powervr_sgx failed with error -2
```

"not a microkernel file" is `ukfw_parse()` returning `-ENOENT` (no `SGX5`/`UKRN`
header).  Capture `docs/captures/sgx540-ukernel-2026-11-26.txt`.

### The upstream tree is ahead of report 276

Read at upstream commit `95f78ca` in `local/work/powervr_sgx`:

* `pvrsgx_core_family_usable()` accepts `va_bits` **28 or 32** and
  `num_dir_lists` **exactly 1** for a core without `MULTIPLE_MEM_CONTEXTS`; it
  does not demand ≥32 bits or ≥2 lists.
* The SGX530 is a **complete** 28-bit family entry (`sgx530_regs`, `sgx530_heaps`,
  `pvrsgx_cmd_core_sgx530`, `PVRSGX_QUIRK_BRN_28889`), gated by
  `powervr_sgx.allow_unisolated=1`.
* `sgxerrata.h` gives the SGX540 **three** BRNs at rev 120: `26620`, `28011`,
  `34028` — the 29954-31780 list report 276 quoted is the **SGX543's**.
* `sgx540defs.h` defines `USE_CODE_BASE` with a **24-bit** address and DM at
  **bit 24**; `sgx535defs.h`/`sgx545defs.h` use 25 bits / bit 25.
* `EUR_CR_CLKGATECTL` on the 540 also gates **IDXFIFO b16-17 and TA b18-19**;
  `EUR_CR_BIF_INT_STAT` has **no fault-type field** (fault req b0-13, b14 is
  `PF_N_RW`).

### Microkernel registration offsets are in no GPL header

Checked each SGX545 microkernel-programming offset (0x618, 0x61c, 0x648, 0x638,
0x620, 0x624, 0x628, 0x668, 0x62c, 0x634, 0x274, 0x694, 0x698, 0x664, 0x684,
0x680, 0x69c, 0x65c, 0x63c, 0x428, 0x200) against `sgx540defs.h`: **none** appear.
The DDK's kernel also never writes `USE_CODE_BASE` (only `sgxinfo.h` names index
15 for the microkernel).  These are hardware behaviour the closed microkernel
proves and never documents.

## Inferred (reasoning from evidence, not directly measured)

* **The SGX540 entry is closer to a table row than report 276 said.**  The core
  check accepts its class, the single-context gate already exists, and the heaps
  come from the DDK's own `sgxconfig.h`.  Strong: the entry then bound and
  identified the part on hardware.
* **The unchanged microkernel register tables may well be right for the 540.**
  Same USSE1 ISA, same TSP/ISP/PBE/DPM blocks.  Weak — the tables are carried over
  as hypothesis (marked `G` in `cores/sgx540.py`) and nothing proves them.
* **The dma-buf export path should work on this stack.**  Only
  `.gem_prime_import_sg_table` is registered, but `PRIME_HANDLE_TO_FD` is served
  by the generic GEM path and the driver's own tests exercise it.  Code reading
  only (**D**) — relevant because export is exactly what the closed pvr GBM cannot
  do (report 275).

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Build the port from `local/pristine/linux-7.3.3` for a clean `O=`."**  The
  image flashed and **the unit fell back to bootrom**: pristine has no PlayBook
  support at all (no `pbmon.c`, no Munin, no WM8994, no
  `omap4-winchester.dts`).  Board support exists **only** in `local/kernel-tree`.
  Killed by the failed boot itself; trap 378 has the guard to run before wrapping.
* **"A module beside the closed DDK is the safe first test."**  Dropped for the
  operator's copy-of-the-tree idea, which is one variable: `pvrsrvkm` holds
  `56000000.gpu` from boot, so `insmod` can only get `-EBUSY`, and making room
  means `rmmod`, which tears down the desktop first (DECISIONS §111).
* **"Unbind the closed DDK live to free the device."**  It **crashed**:
  `Internal error: Oops - undefined instruction`, `PC is at PVRLinuxFenceDeInit`,
  from `PVRCore_Cleanup` → `pvr_drm_unload` → `pvr_remove`.  The closed driver
  does not survive unbind.  The bind that followed then hung in
  `device_driver_attach` (D state) and needed a reboot.  Use a boot with the
  driver blocked, never a live unbind.
* **"`modprobe.blacklist=` will stop `pvrsrvkm`."**  It did not:
  `/etc/init.d/modules` runs `modprobe <exact-name>`, which bypasses the kernel's
  alias-based blacklist.  Killed by `modprobe -n -v` still printing the `insmod`;
  trap 381.
* **"Put the microkernel in `/lib/firmware` on hd3."**  `request_firmware` still
  returned `-2`, because the probe runs at t≈0.88 s while the root is still the
  initramfs.  It must be in the **initramfs**; trap 380.
* **"An SGX540 microkernel is a `cores/sgx540.py` data file."**  It is not: the
  image is a **two-context `srvinit`** image and `powervr_sgx` needs a
  one-context one with a `pdtable` patch.  See Unknown / trap 382.
* **"The panel is fixed" (from a `grim` capture).**  `grim` reads the
  compositor's buffers, not the panel; it showed a full desktop while the panel
  was black.  Panel state comes from `dmesg` plus the operator's eyes.
* **"The black panel is the power-button DPMS binding."**  Retired: the OS
  registers **no** power/volume/pause buttons at all, so those initrc bindings are
  dead code; the black panel is a DSI wedge (trap 377).
* **"`USE_CODE_BASE` can be settled by a userspace read."**  The read
  (`0x0a0c + 4n`) is named in the header but is **not** on the guide's
  safe-to-read list; the unit hung immediately after it.  Causation is
  **unproven** (the next boot wedged with nothing read), so it is recorded as
  unnecessary risk, not proof (trap 376).
* **"`bbusb.py flash` has no dry-run."**  Wrong: omit `--apply` (operator).  I had
  declared the feature absent from the usage text instead of inferring it.

## Unknown / open questions

* **The microkernel's list-0 / PD-table path.**  `mkukfw.py` requires a
  `pdtable` patch for a single-context core: the driver allocates a PD table at
  `kernel_code.base + 0xf000` and the microkernel must load directory list 0 from
  it.  The image instead routes TA/3D to **list 1** via `BIF_BANK0` and assumes
  list 0 is `srvinit`'s shared kernel PD.  Its own source says: *"under
  powervr_sgx list 0 is an empty idle PD and the transfer BIF-faults on a client
  address"*.  **Settled by** adding the patch site + loading list 0 from it, then
  a boot that reaches a kick (trap 382).
* Whether `USE_CODE_BASE` exists on rev-120 silicon (fact sheet Gap 7).  Settle
  **inside the driver's reset sequence** (write, read back, restore) in a flashed
  boot — never by a userspace read (trap 376).
* `sgx540_init_regs`: cannot be derived statically (no SGX545 init offset is named
  in `sgx540defs.h`; the DDK kernel never writes them).  Needs a capture from a
  working microkernel boot.
* The DPM/TA/ISP/TSP/PBE/PDS state map (fact sheet Gap 10).
* `OCP_DEBUG_CONFIG` bit 31 (`THALIA_INT_BYPASS`) after module enable — a read,
  not done.
* Why the first modeset after a boot fails on some boots (the
  `polls=2000` DSI wedge, trap 377).  Belongs to display work, not the GPU port.
* Whether a newer `pvrports` Mesa (if one exists) exports dmabufs — unchanged
  from report 275.

## Superseded / uncertain

* **Region 0 `0x3547D613` is no longer what is on media.**  It was at the session
  start, and it cannot be rebuilt byte-exactly (`CONFIG_LOCALVERSION_AUTO=y`
  embeds `git describe`, so a rebuild of the same source differs).  Media now
  holds **`0xBC5FB130`**, built from the same tree and config; verified byte-true
  on media (trailer `1f 2d c8 d7 30 b1 5f bc`).  **Keep the image file, not just
  the CRC.**
* **Report 276's "≥32 bits, multiple contexts, ≥2 dir lists" core check** and its
  **errata list** — corrected above and in report 276's session-73 addendum §8.
* **Report 276 §2's "no `USE_CODE_BASE`"** — the header defines them with a
  different field layout; still unmeasured on silicon.
* **"Boot test kernels from RAM"** (upstream guide prose, which I had carried
  into HANDOFF/ROADMAP as a capability).  There is **no RAM-boot path** on this
  device (operator): region-0 IFS flash is the only way to boot a kernel.  Report
  276 meant that the chainloader copies a RAM-loaded zImage internally.

## Artefacts produced

* `tools/bench/gpu/sgxcoreid.py` — read-only identity probe (`/dev/mem`,
  page-aligned windows, the guide's safe register set).
* `tools/bench/gpu/sgxusecodebase.py` — the `USE_CODE_BASE` probe; **DO NOT RUN**
  (traps 376).  Kept as the record of what was read.
* `tools/bench/gpu/sgx540-heaps-check.py` — re-implements `heaps_fit()`,
  `pds_reach()` and non-overlap; it caught the texture-heap overlap.
* `docs/captures/sgx540-identity-2026-11-26.txt` — the identity read.
* `docs/captures/sgx540-bind-2026-11-26.txt` — the hardware bind and the
  `pvrsrvkm` race.
* `docs/captures/sgx540-ukernel-2026-11-26.txt` — the microkernel work and the
  list-0 blocker.
* `docs/reference/sgx540-factsheet.md` — **C** facts, corrected
  register/errata/`USE_CODE_BASE`, the microkernel section, the gap table.
* `docs/contradictions/sgx540-use-code-base.md` — the 24-vs-25-bit evidence.
* `docs/reports/276-…` — session-73 addendum and §8.
* `rootfs/powervr-sgx/` — `0001-sgx540-core-entry.patch` (driver entry),
  `sgx540.py` (microkernel core config), `firmware/sgx540_ukernel.{bin,json}`,
  `README.md`.
* Images (host, gitignored `local/work/`): `kernel-sgx540-opendriver.img`
  (`0x13172BFA`), `kernel-sgx540-unisolated.img` (`0x325193F7`),
  `kernel-sgx540-mk1.img` (`0xD2CEFD74`), `kernel-sgx540-mk2.img`
  (`0x290569CA`), `kernel-restore-20261126.img` (**`0xBC5FB130`**, on media).
* Docs: DECISIONS **§109/§110/§111**, ROADMAP 7a (a1/a1b), KNOWN-ISSUES traps
  **368-382**, STATE, HANDOFF (73→74), reports INDEX, NUMBERING, this note.
* `.gitignore` — `initramfs/root/lib/firmware/` as a built artifact.

## Note on reverting

Persistent device-side changes were made and **reverted**: the microkernel is
still at `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` and the five modules
(`powervr_sgx` + `drm_exec`/`drm_gpuvm`/`drm_shmem_helper`/`gpu-sched`) remain in
`/lib/modules/7.3.3/...` — both inert, as the driver is not loaded.  Removed:
`/etc/modprobe.d/powervr-open.conf` and `/etc/modules-load.d/powervr_sgx.conf`.
Region 0 was restored to the working image by re-flashing.  `pvrsrvkm` was
re-loaded live and holds the GPU again.  The host clone is under gitignored
`local/`.

## Wrap-up (session 73 close)

Closed by the operator.  Bench state, this note, the canonical docs and the
captures are recorded; `docs_audit` and the offline tests are green.  **The next
step is the microkernel's list-0 / PD-table work** (HANDOFF §4).
