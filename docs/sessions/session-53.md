# Session 53 (absolute) — BT's core works and its pack registers `hci0`; the L1-error class is pinned; hd3 + SGX scoped

> Numbering: absolute `53` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 47** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-15.
Unit state at the end: **our Linux image `0xDD66D8C3`** on region 0, region 1
**`49e318bf…`** (43 modules).  `0fca:8010`, console `pb> `, **ssh on
`169.254.0.1`**.  `BOOTCTR_FCTRY = 0x22`.  No flash, no NVRAM/MCT/RPMB write,
no restore needed.

## Goal of this session

Handoff item (a): BT — power `hci0`, load the service pack, give it a stack.  The
operator then set the real target (**full BT including LE**, enough for HID),
then redirected the project: **SGX540 acceleration**, then **Nura on hd3**.

## Confirmed (measured, read in a file, or seen on the bench)

* **The BT core does real Bluetooth.**  Over a plain H4 tty (`/dev/ttyS1`,
  115200 8N1) it answers `HCI_Reset` (`04 0e 04 01 03 0c 00`), reports
  `hci ver 6 rev 0x0000` / `lmp ver 6 subver 0x0f00` / `mfr 0x002b` (Texas
  Instruments) / BD_ADDR `8e:e4:3c:22:61:00` (`btdump 0x1001`, `0x1009`), and
  **runs a full BR/EDR inquiry that finds a peer**: `Inquiry Result, 1
  device(s)` → `FOUND f0:a3:b2:bb:d1:00 class 0x011c00` → `inquiry complete`,
  reproduced twice.  [report 238 §0; `rootfs/bt/btinq2.c`]
* **The reference's service pack loads completely.**  `btsload` replays
  `local/work/1917/base/lib/firmware/bluetooth/WL1283.bts` over H4:
  **315/315 commands answered**.  The LMP subversion moves `0x2b0f` → **`0x2b33`**,
  which is what the file's own header declares
  (`# Quattro LMP Subversion: 2b33`, version `TI_P6_15.51`).  [report 238 §2]
* **The pack is what makes the controller register with mgmt.**  Same attach
  procedure, only the pack differing: without it `MGMT_OP_READ_INDEX_LIST`
  returns `11 B: … 03 00 00 00` (**0 controllers**); with it, `13 B: … 03 00 00
  01` (**1 controller**).  `MGMT_OP_READ_INFO` then returns BD_ADDR
  `8e:e4:3c:22:61:00`, HCI version 6, manufacturer `0x000d`.  First time `hci0`
  has been in mgmt's index list on this unit.  [report 238 §2; trap 249]
* **`0x201c` is `HCI_LE_Read_Supported_States`, and on a bare ROM it stops the
  controller registering.**  A bare ROM answers **Command Disallowed (`0x0c`)**
  with 5 payload bytes where `struct hci_rp_le_read_supported_states` needs 9,
  so `hci_cc_func()` logs `unexpected cc 0x201c length: 5 < 9`, returns
  `HCI_ERROR_UNSPECIFIED`, and `bt_to_errno()` maps it to `-ENOSYS` (the
  `failed: -38`).  `hci_le_read_supported_states_sync()` is the **last** step of
  `le_init2`, and `hci_init_stage_sync()` aborts the stage on non-zero, so
  `hci_dev_init_sync()` fails.  The pack fixes the **length** (11 → 12 bytes) and
  that is enough to register.  [report 238 §0; `hci_sync.c`, `hci_event.c`,
  `lib.c` read in-tree]
* **The pack survives `HCI_Reset` but not a `BT_EN` power cycle.**  `btsload
  --probe-reset` reads `0x201c` before and after a reset inside one session:
  both are 15-byte events.  [report 238 §2]
* **The remaining blocker is a lone `0x30` byte.**  An instrument in `h4_recv()`
  shows the identical shape in **6 of 6** runs: the last init reply arrives
  complete and correct, then ~200 ms later a **single `0x30`** arrives on its
  own.  `0x30` is not an H4 packet type, so `h4_recv_buf()` returns `-EILSEQ`
  (`Frame reassembly failed (-84)`).  [report 238 §4; trap 250]
* **The BT-firmware contradiction is settled** in favour of the reference's
  file.  Its header says `Compatibility: Quattro, 6.0.15 ROM`; linux-firmware's
  `TIInit_7.2.31.bts` (48 909 B) and `TIInit_6.2.31.bts` say `BL6450, WL1271/3
  2.0 ROM`, a different older family.  [report 238 §3; contradiction resolved]
* **The `wl128x-fw-4-sr/5-sr.bin` files are WLAN-core firmware**, loaded by
  `request_firmware()` in `wlcore/main.c`; they cannot change BT.  [report 238 §3]
* **hd3's live contents** (`dd` from `/dev/mmcblk1`): a DOS MBR at `0x49000000`
  with a QNX v1.2b boot sector (`eb 10 90 …`, "QNX v1.2b Boot Loader"), and the
  QNX6 superblock at **`0x49002000`** (magic `5d 55 29 c2 3e 56 45 21`) —
  matching report 138's corrected offset.  [measured this session]
* **The pre-OS loader's allow-list includes MCT type `0x1C`**, and `bbusb.py`
  already encodes the extent as `'0x1C FS Fixed': (0x4905, 0xFFFFF)`.  So hd3 is
  a legal flash destination through the existing loop.  [report 98 §2;
  `tools/bbusb.py:1462`]
* **In-place filesystem creation works on the device.**  Rehearsed
  non-destructively on a scratch file: `losetup -f -o 0` → `mkfs.ext2 -F -b 4096`
  → mount → write → unmount, all succeeded.  `losetup -o` exists;
  `initramfs/init.c` already mounts region 1 via `LOOP_SET_STATUS64` +
  `lo_offset`.  [bench; `initramfs/init.c:448` `ROOTFS_RAW_OFF`]
* **Device filesystem tools are incomplete:** busybox has `mke2fs` but **no
  `offset=`** option and **no `resize2fs`** (`applet not found`).  [bench]
* **`analyze_blocks()` skips runs of `0xFF`, not `0x00`** — so a sparse
  (zero-filled) ext4 does **not** get its holes skipped, and a fully expanded
  62.86 GiB image would send all of it over the bootrom link.  Image size is
  therefore a policy choice: 1 GiB = 1.59 % of hd3, 2 GiB = 3.18 %, 4 GiB =
  6.36 %.  [`tools/bbusb.py` `analyze_blocks`]
* **postmarketOS was renamed Nura on 2026-09-27** (announced at the project's
  conference; a 1.5-year process).  Primary sources: `nura.eco/blog/2026/09/27/
  nura-rename/`, Wikipedia, `wiki.nura.eco`, `docs.nura.eco`.  Package names and
  many URLs still say `postmarketos`/`pmaports`.  [web, cited in report 239]
* **The PlayBook is not a supported Nura device**; the OMAP4430 reference port is
  `device-samsung-espresso7` (Galaxy Tab 2).  Its `deviceinfo` gives the shape:
  `arch=armv7`, `dtb=omap4-samsung-espresso7`, `append_dtb=true`, `drm=true`,
  `chassis=tablet`, `depends="postmarketos-base linux-openpvrsgx mkbootimg"`, and
  `deviceinfo_flash_method="heimdall-bootimg"`.  [pvrports clone, read]
* **`pmbootstrap install` builds through `pmb.install.blockdevice.create(
  sector_size, size_boot, size_root, split)`**, producing image files with
  `split` — so a rootfs can be produced without flashing a device.
  [`docs.nura.eco` API reference]
* **Mainline 7.3.3 binds nothing to `ti,omap4430-gpu` / `img,powervr-sgx540`**
  although `omap4.dtsi` defines the target-module, `prm_gfx` and the L3_GFX
  clock.  SGX therefore needs an out-of-tree driver.  [grep of the tree;
  `omap4.dtsi:484–508`]
* **Reference SGX grading data:** `graphics.conf` declares
  `gpu-string = SGX540rev120` and lists the QNX userspace (`libsrv_um.so`,
  `libusc.so`, `libIMGegl.so`, `libImgGLESv1_CM.so`, `libImgGLESv2.so`,
  `libpvr2d.so`, `pvrsrvinit.so`, `pvrsrv.so`) under
  `local/work/1917/base/usr/lib/graphics/omap4430/`.  **No separate SGX firmware
  file exists** in the reference.  [read this session]

## Inferred (reasoning from evidence, not directly measured)

* **The `0x30` most likely comes from the host, not the chip.**  Strong on the
  byte and the timing (6/6 runs, instrumented), **open** on origin: it never
  appears when the same sequence is driven from userspace, including a passive
  listen with no command sent.  UART2 is in no `/proc/consoles`, so it is not
  console traffic.  Not proven either way.
* **LE is refused by the controller but that is not proof of a silicon limit.**
  Medium.  Three independent refusals (`0x0c`, `Write_LE_Host_Support` = `0x11`,
  all-zero LE feature page), but the chip reports **HCI 4.0 / LMP 4.0** with the
  LEMS bit set; the reference stack being BR/EDR-only reflects the vendor never
  shipping an LE stack on a 2011 tablet.  So no enable sequence is known;
  impossibility is **not** demonstrated (the operator's point).
* **The hd3/Nura rootfs will work around our existing chainloader.**  Medium-strong:
  the two halves are separable — the chainloader already boots zImage+DTB and the
  initramfs mounts a rootfs by raw offset, so only the *source* of the rootfs
  changes.  Unverified: that `pmbootstrap` will build a rootfs for a device whose
  kernel it does not own.
* **A Nura `linux-*` package can wrap the existing 7.3.3 tree + patch + config.**
  Medium.  `envkernel.sh` exists for out-of-tree kernel work, but the image must
  then be wrapped by `build_kernel_image.py` instead of used as a boot.img.
  Flagged in ROADMAP item 7a as "confirm against a real `pmbootstrap init` run".

## Dead ends and wrong assumptions (valuable — do not skip)

* **"`0x201c` is a benign TI vendor op."**  Wrong (report 237 §4).  It is
  standard `HCI_LE_Read_Supported_States`, and on a bare ROM it is the whole
  reason `hci0` never registered.
* **"Power the controller over mgmt and it will work."**  Unreachable on a bare
  ROM: `num_controllers = 0`, so `SET_POWERED` could not succeed.  The pack is a
  prerequisite nobody had established.
* **"The core is asleep, so it disallows the LE commands."**  Refuted: loading
  the pack with `0xFDA8 01` (disable Cortex Sleep) appended gives the same
  `0x0c`.  Sleep is not the gate.
* **"`Write_LE_Host_Support` must come first."**  Refuted: returns `0x11`, changes
  nothing.
* **"The chip sleeps between the kernel's init commands."**  Refuted: `0x1002`
  then `0x1003` answers back-to-back, after 1 s, and after 2 s.
* **"`hci_uart_set_flow_control()` is the framing fault" — MY OWN WRONG GUESS,
  retracted.**  H4 never calls it (only the vendor protocols `bcm`/`qca`/`intel`/
  `mrvl` do) and `hci_uart_setup()` is a no-op for H4.  Worse, the A/B that
  appeared to prove `CRTSCTS` fixed it was run while an instrumented module was
  still loaded; on a clean boot the controller registers with `CRTSCTS` **on and
  off**.  I presented this as a result before re-checking it.
* **"The `Read_Local_Name` reply is corrupt" — also mine, also retracted.**  It is
  255 bytes and arrives complete and correctly framed.  What looked like
  corruption was **my probe's own 240-byte display buffer**.  A diagnostic
  manufactured the fault it reported.
* **"Use 5-sr firmware to fix BT."**  The `wl128x-fw-*-sr.bin` files are WLAN
  firmware; BT's firmware is the `.bts` pack.  Different cores, different files.
* **"hd3 needs a 62 GiB transfer, or an in-place mkfs."**  Both wrong.  The
  operator's point settled it: hd3 should hold a **flasher-restorable ext4 image**
  (the recovery property), and the loader already accepts `0x1C`.  The real
  constraint is that sparse holes are **not** skipped, which makes image size a
  policy choice, not a technical barrier.
* **`insmod` "succeeding" while keeping the old module.**  `btbcm` holds a
  reference to `hci_uart`, so `rmmod hci_uart` fails and `insmod` silently keeps
  the old code — the instrument never fired and the experiment looked negative.
  Verify a module swap by **content**, never exit status.  (Trap 251.)
* **After a cold boot `/sys/class/gpio/gpio535` is not exported and reads low**, so
  a pack load fails with "no HCI_Reset answer" — identical to a dead chip.  A 2 s
  low pulse was reliably sufficient where 50 ms sometimes was not.  (Trap 252.)
* **Three ways my own tools lied** (each cost real time; all in
  `rootfs/bt/README.md`): buffered stdout discarding output on SIGTERM; the
  Command Complete vs Command Status offset difference (`0x0e` puts the opcode at
  offset 4, `0x0f` at 5) dropping every asynchronous reply; and a stale `N_HCI`
  eating bytes so a live chip looked dead.
* **A compound `ssh … 'a; b; c'` with nested quotes and redirects silently does
  not run.**  Three times a load-and-attach one-liner produced no log at all.
  Push a small script instead.
* **A subagent left running with no time-box** for 87 minutes produced nothing;
  the same analysis took minutes when done directly.  Time-box subagents.

## Unknown / open questions

* **Which side emits the stray `0x30`** — the chip, or something host-side ~200 ms
  after init settles.  **Would settle it:** extend the `h4_recv` instrument to log
  what `hci_uart_write_work` writes, with timestamps, on a cleanly-swapped module.
* **Why `0x1003` then times out** when `0x1003` alone answers instantly from
  userspace.  Almost certainly a consequence of the `-84` leaving the H4 receiver
  in an error state.  **Would settle it:** the same instrumented run, reading
  `rx_skb` state after the error.
* **Whether LE can be enabled on this chip at all.**  **Would settle it:** a
  working enable sequence (none known) or a TI datasheet/release note naming the
  LE gate.  The reference cannot help — it has no LE stack.
* **Whether `pmbootstrap install` will produce a rootfs for a device whose kernel
  it does not build/flash.**  **Would settle it:** one `pmbootstrap init` +
  `install` run with a scaffolded port, no device writes.
* **Whether a Nura `linux-*` package can wrap this 7.3.3 tree unchanged**, and
  whether its output can be fed to `build_kernel_image.py`.  **Would settle it:**
  the same run, inspecting the kernel package's install step.

## Superseded / uncertain

* **Session 52's "`hci0` exists, so use mgmt to power it"** — superseded: on a
  bare ROM the controller is never registered.  It *is* reachable once the pack
  is loaded, which is the new fact.
* **"BT transport works; powering is the next step"** — superseded: the transport
  is now shown to include a full inquiry with a real peer.
* **"Use linux-firmware's `TIInit_7.2.31.bts`"** — settled the other way; the
  contradiction file records the resolution.
* **"`hci0` in sysfs means a live controller"** — corrected: the node comes from
  `HCIUARTSETPROTO`; mgmt registration is the real milestone.
* **My own two claims retracted this session** (flow control; corrupt
  `Read_Local_Name`) — see "Dead ends" above.  The repository no longer carries
  either.

## Artefacts produced

* Report `docs/reports/238-wl1283-bt-inquiry-and-le-init-2026-11-15.md`.
* Report `docs/reports/239-nura-hd3-and-sgx-scoping-2026-11-15.md` (research
  notes for the redirect; includes the external citations).
* `rootfs/bt/` — five bench tools + `README.md`: `btdump` (raw hexdump; start
  here), `btsload` (`.bts` H4 loader, `--stay-awake` / `--probe-reset`),
  `btinq2` (BR/EDR inquiry), `btgate` (LE gate walk), `btdetach` (`N_HCI` →
  `N_TTY`).  All five compile clean with `arm-linux-gcc -static -O2 -Wall`.
* `rootfs/hci.c` — gained `--no-cts` / CRTSCTS-on-attach.  **Not a fix** (flow
  control proved irrelevant); kept only so the A/B is repeatable.
* Traps **249–252** in `KNOWN-ISSUES.md` §2.
* DECISIONS **§73** (the BT service pack and the kernel-side blocker).
* `docs/contradictions/bt-firmware-tiinit-vs-wl1283.md` — **resolved**.
* ROADMAP item **7a** (hd3 + SGX + Nura).
* No kernel source change: `hci_h4.c` instrument **reverted and proven
  byte-identical to `local/pristine/linux-7.3.3`**.  Patch unchanged at
  **61 files / 8 762 lines**, sha256 `20556c5f…`.
* No new image.  Region 0 `0xDD66D8C3` and region 1 `49e318bf…` are the
  session-52 builds, both re-verified this session against the media.
