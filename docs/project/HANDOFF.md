# Handoff — session 75 (absolute) → session 76

Self-contained.  **A paste-ready kickoff prompt is at the end (§6).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 76 = Linux-era 70**, written at the
session-75 close.  See `docs/sessions/NUMBERING.md`.

> **SESSION 75 RESULT: the SGX540 microkernel's event program is entered ONCE
> and never again; the timer fires but nothing consumes it; one dispatch bit is
> simply absent on this silicon; and the NULL job that "passed" in session 74 was
> the driver's own 2 s timeout.**  Four boots.  A dynamic tick counter read **0
> over 12 s** while `EUR_CR_EVENT_TIMER` cycles inside its 1 ms period;
> `EVENT_STATUS` b29 stays **latched and unacknowledged**; IRQ 147 has **never**
> fired; `kick` is never entered; six jobs cost six resets; and the two `1/1
> passed` runs took **2016806** / **2020499** µs against
> `job_timeout_ms = 2000`.  A per-bit sweep of `EVENT_PDS_ENABLE` shows **bit 14
> `SW_EVENT` does not exist in that register here** — so session 74's
> `0x2204f00f` was a no-op and the SGX545's `0x2204b00f` is equivalent.  The init
> task *does* run (`SC_VERSION`, `EVENT_HOST_ENABLE` b29).  Report **278** (§8
> timer, §9 registers, §10 sweep); traps **388–397**; DECISIONS **§113**;
> captures `docs/captures/sgx540-{ukernel-kick-counter,tick-counter,dispatch-regs,pds-enable-sweep}-2026-11-26.txt`.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 7.3.3** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 with an initramfs that hands PID 1 to a real root on **hd3**, with
a GPU-accelerated desktop (compositor **and** clients), and a modern browser.

**The long-term graphics route is the open `powervr_sgx` stack** (DECISIONS
§107/§108): the closed pvr DDK's GBM cannot export dmabufs, which is what blocks
the browser (report 275).  This session is that port.

## 0a. Session 75's result in brief (do not re-derive)

* **The event program is entered once and never again.**  An entry-counting
  instrument (`UK_KICKLOG`, control-block words **0x3d** = `kick` entries and
  **0x3e** = empty-CCB exits, free words, read through the existing `ctlblk`
  debugfs node) read **0 at every sample of a whole 2 s job window**.  The kernel
  ring held the command (`ccb wo 1 ro 0`) while the microkernel's own read of the
  same ring was `0/0` (`UK_CCBLOG`'s words 0x39/0x3a).  **Six back-to-back NULL
  jobs cost six GPU resets.**  So the doorbell never opened; `kick` never ran.
* **A uAPI `1/1 passed` here is the driver's 2 s timeout** (trap 388).  The two
  passing runs took **2016806** and **2020499** µs against
  `job_timeout_ms = 2000`.  The test's syncobj is bound to `drm_sched`'s
  `s_fence->finished`, which `pvrsgx_timedout_job()` finishes through
  `drm_sched_start(sched, 0)` with `errno = 0` and no error bit, and
  `drm_syncobj_array_wait_timeout()` returns 0 the moment the fence is signalled
  without ever inspecting the error.  The context is then `GUILTY`, so the next
  run on it reads `0/1`.  **Do not use the suite's pass column as evidence about
  the microkernel**; use `resets` and the control-block counters.
* **Two session-74 figures are withdrawn.**  `consumed 2..5` measured nothing —
  `pvrsgx_fw_start()` calls `pvrsgx_ccb_reset()`, which sets
  `ccb_reset_seq := ccb_issued`, so an idle ring reports `consumed == issued`
  (the idle boot read `issued 1 consumed 1`); read `wo`/`ro`, not `consumed`
  (trap 389).  And that boot ran build **`ccb2`**, not `invfix` — the file on
  hd3 was swapped after its module load, and the firmware is read only at module
  load (trap 390); `dmesg | grep 'open microkernel'` is the only truth.
* **The event program is not entered — but the TIMER is not why.**  `UK_TICKLOG`
  increments a **32-bit dynamic** counter in the microkernel's own memory (free
  control-block words 0x2c/0x2d) once per pass through the timer branch; after
  **12 s idle** it read **0**.  Then the register itself was read: `EUR_CR_EVENT_TIMER`
  is **alive** — `ENABLE` b24 set, five samples all inside `0..0x04B000`, i.e.
  cycling at ~1 kHz.  So the timer fires, and the event is raised but never
  consumed: `EVENT_STATUS` b29 stays **latched**, nothing acknowledges it, and
  IRQ 147 has never asserted.  A static `ctlblk` word could never have shown a
  1 kHz event (trap 394) — that is why the counter had to be dynamic.
* **`EVENT_PDS_ENABLE` bit 14 is ABSENT on this silicon — and session 74's
  "correction" was a no-op.**  The driver writes `0x2204f00f` and reads
  `0x2204b00f`.  A debugfs node that **writes** 0x0a58, one bit at a time, shows
  every bit of the word sticks **except 14**, which returns 0 even when written
  on its own — absent, not masked.  `SW_EVENT` *is* implemented in
  `EVENT_HOST_ENABLE` (0x130 = `0x20004000`), so the header field is real and
  merely mis-attributed by register: the fact sheet's first **C** fact to
  contradict a **K** one.  **Consequence: `SGX540_CR_EVENT_PDS_ENABLE_ALL
  0x2204f00f` is functionally identical to the SGX545's `0x2204b00f`, so that
  part of session 74 changed nothing.**  The other three corrections stand (and
  the improvement it was credited with is separately withdrawn — traps 388/389).
  Trap 397.
* **The init task really does run**, on three independent proofs, none of them
  the `microkernel started` log line: `SC_VERSION` (0x18) = `0x00010000` (a
  microkernel-only store), `EVENT_HOST_ENABLE` b29 set (`0x20004000`, ORed in by
  `init_set` after the timer write), and `BIF_FAULT 0` throughout.  With
  `edm_init=1` the **host** writes `ui32InitStatus` itself, so that log line is
  the host's own bit (trap 396).
* **The instrument is in the durable patch series**:
  `rootfs/powervr-sgx/0003-sgx540-kick-counter.patch` and
  `0004-sgx540-tick-counter.patch` (the second is the dynamic one).  Built
  `UK_CCBLOG=1 UK_KICKLOG=1 UK_BUILD_ID=kick1` → md5 `229b838c…` and
  `UK_CCBLOG=1 UK_KICKLOG=1 UK_TICKLOG=1 UK_BUILD_ID=tick1` → md5 `c94d8d2f…`;
  applying 0001→0004 to a pristine `95f78ca` reproduces both byte-for-byte.  With
  the new flags unset the same command
  reproduces the un-instrumented build **byte-for-byte** (`7056fc74…`), and
  `ukernel/golden/check.py` keeps the SGX545 image at `83b5b0a7…`.  A negative
  control in the emulator (`init` + 3 empty events) gives `0x3d=3`, `0x3e=3`.
* **hd3 has no `scp` and no `sftp-server`**, so `tools/pbscp.sh` cannot push a
  firmware file there.  Use `cat FILE | tools/pbssh.sh 'cat > /path'` and always
  `md5sum` before rebooting (trap 391).

## 0a2. Session 74's result in brief (still true, do not re-derive)

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

## 0b. The one experiment for session 76

**Question: does `EVENT_PDS_EXEC` (`0x00804090`) point at code this core will
execute — i.e. is the PDS program where the exec address says it is?**

Why this is now the leader: the sweep closed the alternatives one by one.  The
timer is a PDS source, it is enabled and it **fires**; the event latches in
`EVENT_STATUS` b29; nothing acknowledges it; the host IRQ has never fired; and
bit 14 (`SW_EVENT`) does not exist in `EVENT_PDS_ENABLE` at all (trap 397).  With
every *enable* path now either working or absent, the remaining untested link
between "a source fires" and "the event program is entered" is the address the
program is fetched from.

`check_event_pds()` compares it against our own derivation, so it proves the
store landed and nothing about the target.  The cheap test is a **read of the
PDS program's first words through the CPU-side mapping** — `sdev->fw.cpu[
PVRSGX_FW_PDS0]` and `pds_base + addr` — and comparing the entry it points at
with the manifest, which names the image's `event` entry.  If the address lands
mid-instruction or in the wrong heap, that is the answer; if it lands exactly on
the entry, the fault is downstream again.

A job is still worth submitting in the same boot: `EVENT_KICK` does not depend
on the timer or the PDS source, and the `kick` counters stayed 0 through a whole
job window.

**Note:** the bench is left on the **read-only dump module** (`9a0ebbe3…`), not
the sweep module (`a4aa13a9…`), because its node *writes on read*.  If the sweep
is needed again, `.sweep` is beside it on the unit; do not leave it installed.

The write side is `ccb_write()`: it writes **`EUR_CR_EVENT_KICK` (0xac8,
`NOW` b0)** after storing the command, and the address form is
**`EVENT_KICKER` (0xac4, address b4-27, a 24-bit field at shift 4)** — on a
**28-bit** core that field has one more bit of range than the SGX545's 32-bit
register, which the `regs` table may still be copying.  The dispatch mask is
`EVENT_PDS_ENABLE` (0x0a58), programmed `0x2204f00f` by the DDK's own
derivation and never read back.

**Already checked and CLEARED statically (session 75) — do not spend a boot on
these two.**

* *Which kick register.*  The init kick goes to `EVENT_KICK2` (0x0ac0) and every
  job kick to `EVENT_KICK` (0x0ac8) — and that is exactly what the DDK's own
  kernel driver does when `SGX_FEATURE_MULTI_EVENT_KICK` is set, which it is for
  the SGX540.  The differing offsets are **not** a defect; `pvrsgx_regs.h`'s
  `EVENT_KICK2 = 0x0ae8` is the SGX545-family constant and is overridden by our
  entry.  Full derivation: `docs/contradictions/sgx540-event-kick2-offset.md`
  (resolved), trap 392.
* *Whether the event program's address is right.*  `event_pds_addr()` uses the
  same shape as the vendor's (`addr − pds_pixel.base` when
  `EDM_VERTEX_PDSADDR_FULL_RANGE` is clear), the result (`0x800000`, 8 MiB) is
  far inside `OTHER_PDS_EXEC`'s 18-bit field, and the `check_event_pds()`
  read-back passes.  That read-back cannot detect "the PDS program is in the
  wrong heap" (trap 393), but the derivation itself agrees with `srvinit`.

So neither of the two cheap explanations survives.  What remains is **whether
the writes land at all** — see the method below.

**Method — one boot, three reads, no new instrument.**  Add a temporary debugfs
line (or a `pr_info` in `ccb_write()`, rate-limited) that prints all three
**from inside the driver**, around one NULL job:
`EVENT_PDS_ENABLE` (0x0a58) as programmed and as read back, `EVENT_KICKER`
(0xac4), `EVENT_KICK` (0xac8) read just after the write, and `EVENT_STATUS`
(0x12c).  Everything is a register the driver already has in its `regs` table, so
this is arithmetic, not new machinery.  **Register access must come from inside
the driver** (trap 384) — `/dev/mem` is closed in this configuration.

**What each result means:**

* **`EVENT_KICK` reads back 0 and `EVENT_STATUS` gains a bit** → the doorbell
  *did* fire and the microkernel ignores it.  Then the fault is the microkernel's
  entry: with `kick` never entered, look at whether the event program's first
  store is even reached — `k_getmiscinfo` is the one command that does not need
  a client PD, so a `GETMISCINFO` submit is the cheapest thing that would prove
  the entry runs at all (the driver sends one at init, which is why `SC_VERSION`
  is set).
* **`EVENT_KICK` reads back 0 and `EVENT_STATUS` is unchanged** → the write does
  not land.  Next: `EVENT_KICKER`'s field width and the `SGX540` `regs` entry
  against `sgx540defs.h`, and whether the core needs the kick through
  `EVENT_KICK2` (0xac0) or a PDS exec-address pair instead.
* **`EVENT_PDS_ENABLE` does not read back `0x2204f00f`** → the write did not
  stick (a gated unit, or the wrong offset).  Settle that before anything else.
* **The three read back correctly and nothing changes** → the doorbell path is
  right and the fault is in the microkernel's own event admission (the §9 launch
  admits only the launch task; an entry-top store faulted in session 74, which is
  the same class of symptom).

**Inputs (all present and verified — session 75 re-verified them):**

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

* **The exec address lands ON the image's `event` entry** → the PDS program is
  where it should be, and the fault is downstream of the fetch: the microkernel's
  own event admission (§9 launch) or the core's PDS execution.  Then a
  `GETMISCINFO` submit is the cheapest proof that the entry runs at all.
* **It lands mid-instruction, in the wrong heap, or off the image** → that is the
  bug, and it is ours: `event_pds_addr()` would be wrong despite agreeing with
  `srvinit`'s *shape*.
* **The PDS program reads back as a copy still holding `0xdbeef` patch
  placeholders** → the loader never patched the image it is executing, i.e. the
  firmware and the PDS buffer are not the same copy (`fw_build`'s
  `pvrsgx_fw_pds_entry` path).
* **A job still resets with `kick` counters 0** → unchanged; that is the doorbell,
  which does not depend on any of this.

**Do not** spend this boot on anything else: a NULL job costs a GPU reset, and
the control block is free to read.

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

## 1. Bench state at the session-75 close (read on the unit)

| item | value |
|---|---|
| OS | **Linux `7.3.3 #5 SMP`**, ARMv7, dual-core 1008 MHz — not TabletOS |
| region 0 | CRC **`0x02C1DDC2`** — media trailer `1f2dc8d7 c2ddc102`.  File `boot/chainloader/kernel.img`, 10,450,520 B, sha256 `70474c01…` (**no flash this session**) |
| region 1 | sha256 **`20a5edd3a791a37ef5e6ab9a50d1c83f8967d8257ed76c9fe23a318d4b887422`** |
| hd3 | the session-71 rebaked image **`5d3eac8f…`** at `0x4915`, grown to 62.5 G; `/dev/loop0` ext4 rw, 56.5 G |
| `BOOTCTR_FCTRY` | **`0x2f`** (`>= threshold 0x1e, no write`).  It was topped to `0x32` earlier in the session and the four experiment boots spent the difference; `/init` refills below `0x1e` |
| CPU | `nproc` 2; PID 1 affinity **`0,1`** |
| battery | Full, 100 % (`VOLTAGE_NOW` 4 130 000 µV) |
| GPU | `card1`/`renderD129` = **`powervr_sgx`** (open driver), microkernel running, **`resets 0`** (the last two boots submitted nothing); `card0`/`renderD128` = `omapdrm`; GPU IRQ **147, count 0 — never fired** |
| microkernel | `/lib/firmware/powervr_sgx/sgx540_ukernel.bin` = **`invfix`** (md5 `12f12abb…`, 16,640 B, **reverted and `md5sum`-proven**); the session-75 instrument is beside it as `.kick1` (`229b838c…`, inert), the pre-session copy as `.before-kick1`, the session-73 raw as `.session73-raw`.  **The boot that produced report 278 ran `ccb2`, not `invfix`** (trap 390) |
| module | `/lib/modules/7.3.3/kernel/drivers/gpu/drm/powervr_sgx.ko` = **`9a0ebbe3…`** — the session-75 build of the module **plus the seven dispatch registers added to `pvrsgx_dump.c`** (patches 0005).  Session 273's `2e138afc…` is `.live-prev` (revert by copying it back and rebooting).  **`.sweep` (`a4aa13a9…`, patch 0006) is the per-bit sweep and it WRITES the register on read — do not leave it installed**; it is reverted already.  `.bak` (session 73) and `.brn31620` (the reverted experiment) also beside it |
| desktop | `swrast` (the closed DDK is blocked by the operator's standing direction; the open driver is the work) |
| panel | 0 DSI errors, `bl_power` 0 |
| WLAN | **up** at **`192.168.0.231/24`**, default via `192.168.0.1`; `usb0` `169.254.0.1/16`.  (A `10.195.132.240/13` lease was observed mid-session and was transient — but read `ip -br addr` before quoting an address, since the DHCP lease can move.) |
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
* `docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt` — **this session**:
  the instrument, its negative control, the measurement and the fence mechanism.
* `docs/captures/sgx540-tick-counter-2026-11-26.txt` — the dynamic counter, the
  12 s zero, and the "1000×" note that is now retracted.
* `docs/captures/sgx540-dispatch-regs-2026-11-26.txt` — **the register read**:
  the whole dispatch configuration, `EVENT_TIMER` cycling, and the one field
  (`EVENT_PDS_ENABLE` bit 14) that does not read back as written.
* `docs/captures/sgx540-pds-enable-sweep-2026-11-26.txt` — **the per-bit sweep**:
  bit 14 is absent (not masked), which makes session 74's PDS constant a no-op.
* `docs/captures/sgx540-ukernel-{list0,start,first-job}-2026-11-26.txt` — the
  session-74 stages (read with trap 388 in hand: the `1/1 passed` there is the
  timeout).
* `rootfs/powervr-sgx/` — the patch series (`0001` core entry, `0002` the
  one-context path, `0003` the kick counter), `sgx540.py`, `README.md`,
  `firmware/`.  **Nothing is upstream** (DECISIONS §110).
* `drm/tests/powervr_sgx_test.c` — the uAPI suite; `submit-null-job` is the gate,
  but its verdict is not evidence without trap 388.  Build recipe in
  `docs/reports/277` and `§0b` above.
* `local/work/powervr_sgx/ukernel/` — the microkernel; `build.py`,
  `test_emu.py` (8/8 per core), `golden/check.py` (the SGX545 byte-identity
  gate), `cores/{sgx540,sgx545}.py` (all per-core values live here).
* Reports **278** (this session), 277 (the session-74 run), 276 (the scope, with
  its session-73 addendum and §8), 275 (the Kumo negative), 274 (the browser
  build).

## 4. What could not be verified

* **ANSWERED in session 75, do not re-derive:** *does the EDM dispatch more than
  once?* — no, the entry counter stayed 0 through a whole job window and six jobs
  cost six resets (report 278).  *Why is a passing job reported `consumed 0`?* —
  it is not a passing job; `consumed` is a ring-reset artefact and the "pass" is
  the timeout (traps 388/389).
* **Why the doorbell does not open.**  The next experiment (§0b).  Nothing
  between "the driver wrote `EVENT_KICK`" and "the entry ran" is measured.
* **Whether `EVENT_PDS_ENABLE`'s bit 14 (`SW_EVENT`) is implemented here.**  It
  is the one dispatch field that does not read back as written, it is stable, and
  it is the microkernel→host signal.  Trap 397; §0b is the per-bit test.
* **Why the latched TIMER event is never acknowledged.**  `EVENT_STATUS` b29 is
  set at every reading and never clears, which is what an event program that is
  never entered would leave behind — but it is not proof of the ordering.
* **Whether the event entry executes at all past init.**  A `GETMISCINFO` submit
  would settle it cheaply — it is the one command needing no client PD.
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
  primary source for every `ta3dctl` field; the **derived offset table** is
  `docs/reference/sgx540-ddk18-facts.md` §7, and report 277 §4 has the summary.
  The image's own offsets above `0x5c` diverge from the vendor block and should
  be checked field by field.
* The same drop ships the vendor microkernel as **C source**
  (`pbuKernelProgram[]` + `ukernel_labels.h`): a per-command entry table, not a
  single internal dispatcher.  Reading it is legitimate (source, GPL-2) and may
  answer §0b directly.
* The DDK's `policy`/`sgxinit.c` derives several values we hard-code; each is a
  candidate for the "carried-over hypothesis" list.

## 6. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 76 (absolute; Linux-era 70)

Workspace: this repository — a git checkout.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md (this
file), ROADMAP.md item 7a, KNOWN-ISSUES.md §2 (traps 368-397) and §3,
DEVELOPMENT.md §2/3/3a/4/5/7a, DECISIONS.md 108-113, docs/reports/INDEX.md,
report 278 (this session's predecessor), 277 (the run it corrects), 276
(graphics route + addendum), 275 (Kumo),
docs/reference/sgx540-factsheet.md AND docs/reference/sgx540-ddk18-facts.md,
docs/captures/sgx540-ukernel-kick-counter-2026-11-26.txt,
docs/captures/sgx540-tick-counter-2026-11-26.txt,
docs/captures/sgx540-dispatch-regs-2026-11-26.txt,
docs/captures/sgx540-pds-enable-sweep-2026-11-26.txt,
docs/contradictions/sgx540-event-kick2-offset.md (resolved; read so you do NOT
re-run it),
docs/sessions/session-75.md and session-74.md.

WHERE IT STANDS: Linux 7.3.3 #5 SMP ARMv7, dual-core 1008 MHz; region 0
0x02C1DDC2 (trailer 1f 2d c8 d7 c2 dd c1 02; keep the image FILE — it is not
byte-reproducible), region 1 20a5edd3..., hd3 = 5d3eac8f flashed at 0x4915 and
grown to 62.5 G; BOOTCTR_FCTRY 0x2f; card1/renderD129 = powervr_sgx with its
microkernel RUNNING, card0/renderD128 = omapdrm; desktop on swrast; wlan0 up
192.168.0.231; 0 DSI errors; GPU IRQ 147 count 0 (never fired).

THE SGX540 MICROKERNEL STARTS BUT ITS EVENT PROGRAM IS ENTERED ONCE AND NEVER
AGAIN, and the "1/1 passed" everyone has been quoting was the driver's own 2 s
timeout.  Measured (session 75): an entry counter (control-block 0x3d/0x3e, free
words, read via the ctlblk debugfs node) stayed 0 at every sample of a whole job
window; the kernel ring held the command (ccb wo 1 ro 0) while the microkernel's
own view was 0/0; six back-to-back NULL jobs cost SIX GPU resets.  The two
"passing" runs took 2016806 and 2020499 us against job_timeout_ms = 2000 -- the
test's syncobj is bound to drm_sched's finished fence, which the reset finishes
WITHOUT an error, and drm_syncobj_array_wait_timeout() never inspects the error
(trap 388).  Also withdrawn: "consumed 2..5" (a ring-reset artefact, trap 389)
and "the boot ran invfix" (it ran ccb2, trap 390).  Four SGX545-shaped
assumptions were corrected in session 74 and are still correct -- they are what
makes the image START (one directory list, page_dir a PD-table slot,
SINGLE_CONTEXT bits, and 0x138's single completion group).  It needs
modprobe powervr_sgx edm_init=1 (the driver's §9 single-context launch, a boot
default).  NOTHING IS PUBLISHED UPSTREAM (DECISIONS 110).

WHAT REMAINS: the microkernel's init task DOES run (SC_VERSION, EVENT_HOST_ENABLE
b29 -- not the "microkernel started" line, which the host writes itself under
edm_init=1, trap 396), and the EDM timer DOES fire (EUR_CR_EVENT_TIMER cycles
inside its 1 ms period; my earlier "1000x too long" claim is retracted, trap
395).  But nothing consumes it: EVENT_STATUS b29 stays latched and
unacknowledged, GPU IRQ 147 has never asserted, a dynamic tick counter read 0
over 12 s, kick is never entered, and the ring keeps the command.  Of the whole
dispatch configuration exactly ONE field is not what the driver wrote:
EVENT_PDS_ENABLE reads 0x2204b00f where it writes 0x2204f00f -- bit 14 SW_EVENT.

HANDOFF §0b is one boot: a per-bit write/read-back of EVENT_PDS_ENABLE (0x0a58)
from INSIDE the driver (trap 384 closes /dev/mem) to learn whether SW_EVENT is
implemented or merely masked, plus the doorbell (EUR_CR_EVENT_KICK 0xac8,
EVENT_KICKER 0xac4) around one NULL job.  The dump registers are already in place
in pvrsgx_dump.c, so no new instrumentation should be needed.

Three cheap explanations were CLEARED and must not be re-run: the init kick
going to EVENT_KICK2 (0x0ac0) while jobs use EVENT_KICK (0x0ac8) is the vendor's
own shape for a MULTI_EVENT_KICK core (trap 392), event_pds_addr() agrees with
srvinit and lands 8 MiB inside the 18-bit field (trap 393), and the timer's
period is right (trap 395, retracted).

TRAPS THAT COST TIME: a uAPI "1/1 passed" is the driver's timeout here, not a
completion (trap 388); "consumed" after a reset equals "issued" and proves
nothing (trap 389); dmesg's "open microkernel <build>" is the only truth about
which firmware ran (trap 390); hd3 has no scp/sftp-server, so push a file with
cat FILE | tools/pbssh.sh 'cat > /path' and md5sum it first (trap 391).  /dev/mem
cannot reach the GPU once powervr_sgx owns it (trap 384) -- register evidence
must come from inside the driver.  edm_init=1 is required to start the microkernel
and is a supported path, not a hack (trap 385).  A modprobe -r is impossible
while elogind holds card1, so a module change costs a reboot.  Control-block
words are feature-flag dependent: diff every candidate word against
cores/<core>.py AND the diagnostic flags before instrumenting (trap 386), and an
instrumentation store at the top of the event entry FAULTS (session 74).
Build only from local/kernel-tree or a copy of it (trap 378), rebuild the
initramfs cpio or a firmware change is silently dropped, and check that the
zImage actually relinked.  Never unbind pvrsrvkm live (trap 383).  grim captures
the compositor's buffers, not the panel.

BENCH: region 0 0x02C1DDC2; region 1 20a5edd3...; hd3 live /dev/loop0 ext4 rw,
56.5 G; BOOTCTR 0x1e; nproc 2, PID 1 affinity 0,1; battery Full 100 %
(4.151 V); panel 0 DSI errors; wlan0 up 192.168.0.231.  SSH: PB_LINK=usb
tools/pbssh.sh works; if it times out check the cable, the host VPN (trap 341)
and ufw (trap 342) first.  The uAPI test binary is already at
/tmp/powervr_sgx_test (rebuild from drm/tests/powervr_sgx_test.c, static, with
-Idocs/uapi -I$SYSROOT/usr/include -I$SYSROOT/usr/include/drm).  The instrumented
firmware of session 75 is on hd3 as sgx540_ukernel.bin.kick1 (md5 229b838c...,
inert); the live one is invfix (12f12abb...).  Building a variant image must use
EXPLICIT output paths -- a default-output build writes sgx540_ukernel.bin, and
clobbering that makes the offline emulator read garbage.  Kernel printk
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
