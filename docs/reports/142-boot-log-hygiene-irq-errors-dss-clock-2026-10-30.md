# 142 — Boot-log hygiene: the 121 DSI CIO errors and the DSS clock warning

**Session 18 (absolute), Linux-era session 12.  2026-10-30.**
Image: **`0x0D1DA0D3`** (`local/images/kernel-0x0D1DA0D3-noirqerr.img`).
Boot log: `local/logs/boot-20261030-084331.log` (1557 lines).

This closes the three real defects the session-18 boot logs exposed.  All three
were instrumentation or clock-init problems, not hardware faults; the display
and the region-1 root are unchanged.

---

## 1. What the 121 `DSI CIO error` lines were

The count is exact: **121 errors = 121 DSI bridge reads** performed by our own
`pb` instrumentation at boot:

| probe | reads |
|---|---:|
| `pb probe before-seq` (IDREG/GPIOO/SYSSTAT) | 3 |
| `pb probe after-seq` | 3 |
| `pb bridge dump` (16 regs) | 16 |
| `pb dphy dump` (11 regs) | 11 |
| `pb late lanes` (24 samples of 0x0214) | 24 |
| `pb late bridge` (8 regs x 8 attempts) | 64 |
| **total** | **121** |

`0x800000` is bit 23 = `DSI_CIO_IRQ_ERRCONTENTIONLP1_2` (`dsi.h:145`), the LP
contention the OMAP CIO reports during the **bus turnaround** of a generic read.
It fires on successful reads too: `pb probe after-seq` returned the correct
`IDREG=0x00006500`, and `pb late bridge` was `8/8` on every register.  The video
VC0/HS path was never involved.

Two of the three bursts map to the operator's observation: the DRM-init probes
(before the console) and `pb_late_check`, scheduled 6000 ms after DSI probe
(`dsi.c:4191`).

---

## 2. The three fixes

1. **The boot-time read probes are now opt-in.**  A module parameter
   `pb_probe` (bool, default **0**, `/sys/module/omapdss_dsi/parameters/pb_probe`
   or the built-in equivalent) gates `pb_bridge_probe()` (both), `pb_bridge_dump()`
   / `pb_dphy_dump()`, and the `pb_late_check` scheduling.  The **essential**
   PlayBook sequence is untouched: reset, `pb_vc_setup_lp`, `pb_start_hs_clock`
   (the VC0/HS null packet), `pb_bridge_send_seq`, and the required VC re-arm
   before the sequence.
2. **`pb_reg` is created once, through the driver's `.dev_groups`**, not by a
   manual `device_create_file()` in `dsi_probe`.  The encoder is probed twice
   (the DT dependency cycle defers the first probe), so the second manual create
   hit `sysfs_warn_dup()` and tainted the kernel (`G W`).  A driver group is
   created/removed by the driver core and cannot duplicate.
3. **The DSS clock warning is gone.**  `dss_setup_default_clock()` computed an
   arithmetic rate (128 MHz) that the tree cannot produce while the boot chain
   still has the parent at 48 MHz, so `dss_set_fck_rate()` warned
   (`clk rate mismatch: 48000000 != 128000000`).  On winchester it now uses
   `clk_round_rate()` for the pre-parent request, and `dss_probe()` records the
   **real** rate after raising the parent — `pb dss: parent want 100800000
   rr 0 -> 96000000, fck 96000000`.  This is the report 133 §6 clean-up.

---

## 3. Verification

Boot of `0x0D1DA0D3`, log `boot-20261030-084331.log`:

| pattern | count |
|---|---:|
| `DSI CIO error` | **0** |
| `duplicate filename` | **0** |
| `Tainted:` | **0** |
| `WARNING:` | **0** |
| `clk rate mismatch` | **0** |
| `Oops` / `BUG:` / `call trace` | **0** |
| `pb dss: ... fck 96000000` | 1 |
| `Console: switching to colour frame buffer` | 1 |
| `EXT4-fs (loop0): ... ro without journal` | 1 |
| `PB-ROOTFS: /init is running ...` | 1 |

The display still comes up and the region-1 root still mounts and runs.

---

## 4. Everything else in the log, explained

The anomaly scan leaves only these, all benign:

* **`supply ... not found, using dummy regulator`** (6x) — the DT has no
  regulator nodes; the rails are fixed.  Expected.
* **`panel-lvds ... No GPIO consumer enable/reset found`** — the panel is
  enabled through the bridge and backlight, and its TCON table is sent by
  `/init`.  Expected.
* **`hw perfevents: no irqs for PMU, sampling events not supported`** — the
  Cortex-A9 PMU counters are available; no sampling IRQ is wired.  We do not
  use them.
* **`gpio ... Persistence not supported for GPIO N`** — OMAP GPIO debugfs
  information.  Expected.
* **`ocp:target-module@48210000:mpu:fck: device ID is greater than 24`**
  (`drivers/clk/clkdev.c:204`) — a clock-lookup alias whose device name exceeds
  the 24-byte `dev_id` field.  The entry is marked `"bad"` and never matches,
  but the driver still obtains the clock through the DT.  Cosmetic.
* **`Warning: unable to open an initial console.`** — the initramfs has no
  `/dev/console` node; `/init` mounts devtmpfs and uses `/dev/ttyGS0`.  Benign
  (adding a device node to the initramfs would silence it).
* **The `/proc/interrupts` /dev/kmsg record with embedded binary fragments**
  (log lines 1509-1510) — the known `/dev/kmsg` anomaly of report 136 §5.  It
  repeats console-buffer bytes inside one record; still unexplained, still
  harmless.

**The loop capacity is not an anomaly.**  `/sys/block/*/size` and the loop's
`detected capacity change` print are in **512-byte sectors**:
`124583936` sectors = 59.4 GiB (the device), and the loop's `123240448` sectors
= device minus `0x29000000 / 512 = 1343488` sectors.  So the loop over
`/dev/mmcblk1` at `0x29000000` exposes **~58.76 GiB** — which is exactly the
capacity a future ext4 at `hd3`/region 1 needs.

---

## 5. Tooling / tree

* `drivers/gpu/drm/omapdrm/dss/dsi.c`: `pb_probe` + the three gates; `pb_reg`
  moved to `.dev_groups`.
* `drivers/gpu/drm/omapdrm/dss/dss.c`: the winchester `clk_round_rate()` and the
  real-rate record.
* `kernel/patches/0001-playbook-winchester-6.15.11.patch` re-exported:
  **4247 lines / 32 files**, applies cleanly to pristine 6.15.11, and all 32
  files compare byte-identically to `local/kernel-tree` after applying.
