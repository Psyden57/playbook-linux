# HANDOFF — session 4 (2026-10-27) → session 5

> **SUPERSEDED by `HANDOFF-2026-10-27-session6.md`.**  Session 5 finished: the
> display *video* path now works under omapdrm, and the backlight is narrowed to
> a disabled-driver hypothesis.  Read the session-6 handoff first; this file is
> kept because its §7 trap list and §8 subagent/question-relay rules are still
> the long-form reference (the session-6 handoff restates them).

> ## SESSION 5 PROGRESS (2026-10-27) — the display numbers are MEASURED now
>
> **Headline: report `118` + `117` close 111's R1/R2 without a single flash.**
> TabletOS was made to run its own bring-up and every display register it
> programmed was read back live:
>
> * `PM_DSS_PWRSTCTRL` (`0x4A307100`) `POWERSTATE` must be 3 **before**
>   `CM_DSS_DSS_CLKCTRL.MODULEMODE` can leave idle.  With the domain off,
>   `MODULEMODE` latches, `IDLEST` stays 3, and **every DISPC/DSI access
>   SIGBUSes** — that is the real reason the DSI PHY page "aborts".  111/112
>   called it a firewall risk; it is a power domain.
> * Working order: `PM_DSS_PWRSTCTRL=3` → `CM_DSS_CLKSTCTRL=2` →
>   `CM_DSS_DSS_CLKCTRL=0xF02` → reads give `0x00040F02` (the value the
>   bootloader hands Linux).
> * Timings, proven: **1024×600, HBP/HFP/HSW = 138/162/20, VBP/VFP/VSW =
>   12/12/1, Htotal 1344, Vtotal 625, pixel clock 50.400 MHz @60 Hz**.
>   `DISPC_TIMING_H=0x0890A113`, `DISPC_TIMING_V=0x00C00C00`,
>   `POL_FREQ=0x00030000` (no inversion, posedge),
>   `DIVISOR=0x00010002`, `SIZE_LCD=0x025703FF`, `CONTROL=0x02000309`.
> * **DSI1 hangs off the LCD manager**, not the TV/DIGITAL manager
>   (111 §3.1 corrected).
> * `DSI_PLL_CONFIGURATION1=0x14A5E8BE` ⇒ REGM 756, REGN 95, M4=M5=6,
>   CLKDCOLDO 604.8 MHz on a 38.4 MHz reference; `CONFIG2=0x00056000`,
>   `STATUS=0x383` (locked).  111's "≈150 Mbps aggregate" is wrong.
> * **PHY and PLL values are captured** (they were unreadable in 112).
> * **Panel is Hitachi** — but GPIO176 reads 0 (⇒ "LG") until the panel rail is
>   up, so a cold read picks the wrong SPI table.  This session's cold
>   `winch_lcdctl` run selected LG and the panel stayed dark even though
>   DISPC was scanning, the PLL was locked and packets were being sent.
> * **QNX `/dev/spi3` is OMAP MCSPI4** (`0x480BA100`), not MCSPI3.
> * New device state: **`/accounts/devuser/memw32`** is deployed on the unit
>   (a register *write* tool).  PRCM/clock pokes only — never RPMB/eMMC.
>
> **Route (a) was built and tried, and it FAILED for a real reason — see 118 §7.**
> A userspace prover now exists in the console
> (`disp init|stat|mux|col|off`, `bl init|dump|rd|wr`, image crc `0x2AC833B8`,
> backup of the previous one at `chainloader/kernel-console-base.img`).  It
> cannot work on this kernel: the Linux boot path leaves the DSS module in
> **module standby** (`CM_DSS_DSS_CLKCTRL = 0x00040F02`, **bit 18 STBYST = 1**)
> because nothing consumes `dss_clk`, and `DPLL_PER`'s output dividers are in a
> different state than QNX's.  In that state DISPC/DSS reads `SIGBUS` and a
> write to `DSS_CONTROL` **hangs the interconnect** (trap 14).
>
> **Route (b) then WORKED — the video pipeline is alive (118 §8).**  Image
> `chainloader/kernel.img` crc **`0x33BF3664`**: `CONFIG_DRM=y` + `DRM_OMAP` +
> `DRM_PANEL_LVDS` + `DRM_TOSHIBA_TC358764` + `LEDS_LM3532`, the DTS nodes from
> 118 §8.1, and three small `tc358764.c` board patches (optional reset GPIO,
> lanes/hs_rate from DT, missing `of.h`).  On the unit:
> `/dev/dri/` + `/dev/fb0` exist, `omapdrmdrmfb` is registered, **DISPC is
> scanning** (`LINE_STATUS` advancing, `VSYNC1`), `SIZE_LCD` 1024x600 and the
> **DSI PLL is locked** (`STATUS=0x383`).  The DSS module is awake — the driver
> claiming `dss_clk` fixed exactly what §7 said it would.
>
> **What is left for a lit panel:** (i) the **LM3532 backlight**, which is now
> well-characterised as a *negative* result (118 §9.6):
> * the reference keeps **VAUX1/2/3 at 2800 mV, all ON** (`pmic-twl6030-ctl
>   get_state/get_vout`); Linux had them enabled but with **no voltage**.  Fixed
>   with `regulator-min-microvolt == regulator-max-microvolt` (the regulator
>   core then applies it via `apply_uV`) — all three now read 2 800 000 µV.
>   **0x38 still does not acknowledge.**
> * `backlight_win` has **`/dev/i2c0`** open on the reference, and `/dev/i2c0`
>   is `0x48070000` (the TWL's bus, the one we scan).  The other two QNX i2c
>   controllers (0x48072000, 0x48350000) *hang* on first access from Linux, so
>   they are not the answer either.
> * `winch_lcdctl` sets **GPIO2_13** and `pmic-winchester` sets **GPIO1_2**
>   (`regen1`); on the reference both are output+driven-high in `DATAOUT` but
>   **read low in `DATAIN`** ⇒ those pads are *not* muxed as GPIO (report 119
>   already warned the cfg's "GPIO 2/15" mapping is unproven).
> * **Driving GPIO1_2 high from Linux wedged the i2c bus** (recovered on the
>   next boot) — it is on that segment's power/enable path; do not poke it.
> * ⇒ the LM3532's enable is still unidentified.  **Cheapest next test is an
>   observation, not a poke:** with the DSI pipeline running and the rail up, is
>   the panel *lit-but-blank* or *dark*?  That decides whether the missing piece
>   is the backlight at all, or the SPI TCON init.
> * (ii) the **panel TCON init over SPI (MCSPI4)** — still no driver,
>   `&mcspi4` disabled, table in 111 §3.5.
>
> Console now has `bl scan [bus]` (forked), `i2c r|w <bus> <addr> <reg> [val]`
> (forked — a stuck transfer can no longer wedge the console) and
> `write <path> <text>` for sysfs.
>
> Images: see `HANDOFF-2026-10-27-session6.md` §3 — it carries the exact
> current CRC (recomputed from the file).  `kernel-safe.img` `0x7777931C`,
> `kernel-drm-ok.img` `0x33BF3664`.  **Never flash `0x3B04298E`** (LM3532
> without VAUX3 — wedges).

**Read this, then `NEXT-SESSION-BOOTSTRAP-2026-10-26.md` (the manual), then
`109-epoch-test-2026-10-27.md` (the measurement story).**  This file is the
state + "what to do next" entry point.

---

## 0. TL;DR — what changed this session

1. **The "1.4 s global CPU halt" does not exist.**  It is the kernel's own
   `Kernel panic - not syncing: VFS: Unable to mount root fs` — there was never a
   root filesystem — and `panic()` disables IRQs, so the tick stops and the log
   ends exactly like a frozen CPU.  `loglevel=0` suppressed even KERN_EMERG,
   which hid it for two sessions.  No PRCM/VC/VP, secure-world or CPU1 problem
   to fix (109).
2. **`WDT2` understood and neutralised.**  On this boot path it is armed by the
   boot rom with `LDR = 0xfffc1800` → **7.812 s** (not 58.6 s).  `LDR` writes are
   ignored, but the `TGR` kick works; the kernel now pets it from a 2 s
   `timer_list` (a tick-driven kick misses because an idle kernel is tickless).
   Clock control is `CM_WKUP_WDTIMER2_CLKCTRL = 0x4A307830` (not `0x4A31C030`,
   that one is the keypad).
3. **eMMC works.**  The card had been answering all along; `host->ocr_avail` was
   0 because `&mmc2` had no supply.  A `regulator-fixed` 3.3 V rail (`vemmc`) +
   `vmmc-supply` fixed it: `mmcblk1 = Samsung MCGAFA 59.4 GiB`, high speed,
   boot0/boot1/rpmb enumerated.
4. **Userspace runs** (static initramfs `/init`, PID 1, no VFS panic).
5. **USB gadget works** and **there is an interactive console** over it
   (`/dev/ttyGS0` ↔ host `/dev/ttyACM0`): `help`, `ls`, `cat`,
   `peek <hex> [n]`, `poke <hex> <val>`, `kick on|off`, `reboot`.
6. **Display path fully RE'd** (`111`) and its reachability proven (`112`):
   DSI1 video mode, RGB888, 2 lanes, TC358764 DSI→LVDS, 1024×600, panel init
   over SPI3, backlight = LM3532 on i2c 0x38.  Everything readable except the
   DSI PHY page (`0x48044200`), which aborts until its PLL is on.

## 1. How to work on the unit now (this is the big workflow change)

The unit is only reachable over USB, and **you no longer need a flash cycle for
hardware experiments** — only when the kernel or the initramfs changes.

```bash
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis

# door to TabletOS (the reference OS) — needed for reading DRAM after a run.
# The wrapper is DURABLE, in the workspace: ./pbssh.sh (never /tmp/pbssh.sh —
# that vanished on host reboots and left a session with no way in).
pgrep -af door-hold || nohup /home/psyden/rea-work/door-hold.sh >/dev/null 2>&1 &
lsusb | grep -i 0fca          # nothing at all => cable/power, not software
timeout 20 ./pbssh.sh 'echo OK'

# talk to our Linux console (only while our kernel runs)
stty -F /dev/ttyACM0 raw -echo
printf 'peek 48040000 4\n' > /dev/ttyACM0 & timeout 5 cat /dev/ttyACM0
```

**Cold start after a host reboot** — `/tmp` is volatile, so `/tmp/pbssh.sh`,
`/tmp/*.log` and `/tmp/ifs.bin` are gone (the user keeps copies in
`tmp-backup/`).  `door-hold.sh` is durable but its process is not.  Order:
start the supervisor → check `lsusb` against the table in **bootstrap §1.1** →
poll `DOOROK` with a long timeout (the door needs 1–2 min after TabletOS boots,
and a host tool's 30 s default will kill the poll before it succeeds).

Console notes (from 115): the gadget is `0fca:8010` — **the same VID/PID as
TabletOS**, so identify it by `lsusb -d 0fca:8010 -v | grep iProduct` →
*"PlayBook (Linux bring-up)"* (TabletOS shows *"RIM Network Device"*), or by
`/dev/ttyACM0` existing.  The node is
`root:uucp 0660` (`pkexec chmod 666 /dev/ttyACM0` worked, transient).  The
console stays silent until the first host byte, precisely so the host's line
discipline cannot echo its own output back into it.  `/init` reboots
unconditionally **180 s** after start, and `reboot` acts immediately.

## 2. Build → flash → capture (unchanged mechanics)

> ### The iteration loop does NOT need TabletOS (corrected session 5)
>
> `pre_os_ifs_flash.py` only needs the **bootrom**, not QNX.  So a whole
> build→flash→test→rebuild cycle can run with the test image on media:
>
> ```bash
> # while our kernel runs: start the flasher, then reboot from the console
> python3 pre_os_ifs_flash.py --template-ifs chainloader/kernel.img > /tmp/f.log 2>&1 &
> PBCON_TMO=10 ./pbcon.py 'reboot'        # -> bootrom -> flasher writes the image
> timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py
> ```
>
> No TabletOS boot and no 1–2 min door wait.  Restore the sealed IFS **only**
> when TabletOS is actually needed (door captures, the QNX reference), or when
> finishing.
>
> **Counter budget:** `BOOTCTR_FCTRY` is repaired to `0x32` (50) by
> `/accounts/devuser/startup.sh` on every full TabletOS boot, and each
> factory/CRC boot decrements it.  That gives ~50 boots of headroom; treat ~20
> as the working budget and restore before going past it.  A **reset-looping**
> image is the expensive case — it burns one per ~8–30 s WDT cycle — so arm the
> flasher whenever the image might wedge, and never leave a wedging image on
> media unattended.
>
> Full recipe: bootstrap §2.

Short form:

```bash
C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH
make -C "$C" ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage ti/omap/omap4-winchester.dtb
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
EP_TICKS=0 ZIMAGE=$C/arch/arm/boot/zImage \
  DTB=$C/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb python3 build_kernel_image.py
# then the flash/force-reset/restore dance from bootstrap §2
```

Initramfs only (no kernel change): rebuild `analysis/initramfs/root/{init,console}`
with `arm-linux-gcc -static`, then
`rm -f usr/initramfs_data.cpio* && make -C "$C" ... zImage` to re-embed.

**Timing trap:** after a flash the unit sits in the **boot rom** (`0fca:0001`)
holding the USB bus for ~10–12 s before it boots media; our kernel's gadget then
appears ~19 s in.  Start host-side polling immediately and allow ~30 s.

## 3. Current tree state (differs from "documented" in earlier reports)

| item | state |
|---|---|
| `omap4-winchester.dts` | `bootargs = "earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10"`; `&mmc2 { vmmc-supply = <&vemmc>; ... }` + a `regulator-fixed` `vemmc`; `&usb_otg_hs { status="okay"; mode=<2>; interface-type=<1>; power=<50>; }`; `&twl_usb_comparator { usb-supply = <&vusb>; }` |
| `.config` | `CONFIG_INITRAMFS_SOURCE="…/analysis/initramfs/root"`; `USB`, `USB_MUSB_HDRC`, `USB_MUSB_OMAP2PLUS`, `USB_GADGET`, `USB_LIBCOMPOSITE`, `USB_CONFIGFS(+ACM/ECM/RNDIS)`, `OMAP_USB2`, `TWL6030_USB`, `CONFIGFS_FS` all `=y`; `MMC_BLOCK=y` |
| `drivers/irqchip/irq-gic.c` | event log + MPU/32k sampler + WDT2 policy (`pb_wdt_kick()`, 2 s timer, `PB_WDT_KICK_HZ`) + the SMC probe & GIC census (compiled out via `PB_SMC_PROBES 0`) |
| `drivers/mmc/core/{core.c,mmc_ops.c}` | temporary `PBOCR:`/`PBVOLT:` voltage logging (harmless; remove when the card is stable) |
| `chainloader/chainloader_kernel.S.in` | no LED blinks; epoch block at `+0x88`; `EP_TICKS` delay; loader is 828 B |
| current image | `analysis/chainloader/kernel.img`, 0xA245C8 bytes, crc **0x642BDFBE**; also `kernel-usbgadget3.img` and the `kernel-{d0..d4000}`, `kernel-mpustate`, `kernel-r3*` history |

Session-5 additions (display; report 118):

| item | state |
|---|---|
| `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` | `&dss { status="okay" }`; `&dsi1 { status="okay" }` with `port/endpoint { lanes = <0 1 2 3 4 5> }`; `bridge@0` = `toshiba,tc358764` (`lanes=<2>`, `hs-rate=<604800000>`, `lp-rate=<10000000>`); top-level `panel-lvds` with the measured 1024×600@60 timing |
| `.config` | **`CONFIG_DRM=y`** (was `=m`), `DRM_KMS_HELPER=y`, `DRM_OMAP=y`, `DRM_PANEL_LVDS=y`, `DRM_TOSHIBA_TC358764=y`, `DRM_FBDEV_EMULATION=y`, `LEDS_CLASS=y`, `LEDS_LM3532=y`, `BACKLIGHT_LED=y` |
| `drivers/gpu/drm/bridge/tc358764.c` | board patches: optional `reset-gpios`; `lanes`/`hs-rate`/`lp-rate` from DT (was hardcoded 4 lanes and `hs_rate = 0`, which makes the OMAP host reject every mode); `#include <linux/of.h>` |
| `analysis/initramfs/console.c` | new commands `disp init|stat|mux|col|off` and `bl init|dump|rd|wr`; `disp` runs in a forked child with a **bounded** wait (report 118 §7) |
| `analysis/pbcon.py` | host-side console driver; waits for the `pb> ` prompt, `PBCON_TMO=<s>` |
| `chainloader/kernel.img` | **`0xA8FF00` bytes, crc `0x33BF3664`** — DRM kernel, display pipeline working |
| `chainloader/kernel-console-guarded.img` | previous image (crc `0x2AC833B8`), userspace prover only |
| `chainloader/kernel-console-base.img` | the image from the start of session 5 (crc `0x642BDFBE`) |

## 4. Device state at handoff

TabletOS up, door up, media sealed
`sha256 c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299`,
`BOOTCTR_FCTRY = 0x32` (50), no `ttyACM` on the host, no stray USB owners.

## 5. Next steps, in order

1. **Display driver work** (the visible milestone).  111 §6 has the DTS recipe;
   R3 is done (112).  Do R4 (`CM_DSS_CLKSTCTRL = 2`, already the value) and R5
   (verify by register readback, `FRAMEDONE`/`VSYNC` toggling, then DSI PLL
   lock).  Remember 111's two plan-changers: the **DSI PHY page aborts until its
   PLL is on**, and the **LM3532 backlight must be driven directly over i2c** or
   the panel is scanned but dark.
2. **Static, no bench:** 111 R1 — reverse `winch_lcdctl+0x1cce0` for the
   H/V↔register assignment and the DSI PLL/PHY values.  Good subagent job (give
   it a tight scope and tell it to write the file skeleton first — two RE
   subagents burned their turn budgets without writing; see 113's status note).
3. **USB extras:** ECM/RNDIS instead of ACM for a netdev on both sides, then a
   real root filesystem over USB instead of the initramfs.
4. **RPMB / `BOOTCTR_FCTRY`** — the long-term requirement: **every Linux boot
   must write `0x32` back to the counter**, because each factory/CRC boot
   decrements it, and **`0` or `> 0x32` hard-locks the unit** (the bootloader
   errors *"expired factory mode"* and refuses to boot; the only remedy is
   physical — desolder the eMMC, clear the record, resolder).  There is already
   one unit in that state.  Today that job is done by
   `/accounts/devuser/startup.sh` on every TabletOS boot (which is why the value
   always *looked* static — it is being repaired, not ignored).  Key recovered
   (the 32-byte literal `"retreating is advancing in the o"`, used as the
   fallback because this unit's KEK path fails).  Plan, exact semantics, the
   read-only-first ladder and the porting discipline:
   **`116-rpmb-bootctr-plan-2026-10-27.md`**.  When Linux takes over that script
   disappears, so this must land before Linux becomes the normal OS — but
   **never test a write on the working unit**: writes are proven on a
   sacrificial unit first, and the working unit stays read-only until then.
5. Optional cleanup: remove the `PBOCR`/`PBVOLT` debug prints; drop the leftover
   `PB_SMC_PROBES` machinery or wire it to a Kconfig symbol.

## 6. Report index (session 4's additions in bold; session 5 in bold-italic)

- ***`118-live-qnx-display-registers-2026-10-27.md`*** — **the whole display
  mode, measured live** (R1+R2+R4+R5).  Start here for anything display.
- ***`117-re-display-r1-2026-10-27.md`*** — static RE of the mode programming
  (`0x1cce0` is a defaults initialiser; the writers are `0x18a7c`/`0x179dc`/
  `0x16610`).  Read its session-5 correction header.
- **`116-rpmb-bootctr-plan-2026-10-27.md`** — RPMB/BOOTCTR plan + the recovered key
- **`115-usb-console-2026-10-27.md`** — the interactive console
- **`114-usb-gadget-works-2026-10-27.md`** — gadget enumerates (4 fixes)
- **`113-re-usb-device-driver-2026-10-27.md`** — TabletOS `devu-omap4430-musb.so` (**unaudited**, tail incomplete)
- **`112-bench-r3-usb-m3-2026-10-27.md`** — display/USB register probes from userspace
- **`111-re-display-2026-10-27.md`** — full display RE (705 lines)
- **`110-re-usb-2026-10-27.md`** — USB topology/clocks from the IFS
- **`109-epoch-test-2026-10-27.md`** — epoch test, the VFS-panic discovery, WDT2, eMMC
- `108`, `107`, `106` — QNX `wdtkick`/PMIC, `trustzone-omap4`, `setup-core-inactive`
- `105`, `HANDOFF-2026-10-26-global-stall.md` — session 3 (superseded in premise, still the measurement history)

## 7. Hard-won traps (all of these cost a cycle)

1. **`loglevel=0` hides panics.**  Boot probe images with `ignore_loglevel`.
2. **`/dev/kmsg` from userspace is unreliable near a faulting device access** —
   records vanish while the process lives.  Write the DRAM ring directly
   (`/dev/mem` + `0x88000000`, same count/index protocol as `ring_put`).
3. **Ring wrap loses the *head*:** a run that prints 80–90 KB keeps only the
   last 32 KB, so early boot lines disappear.  Keep runs short or read twice.
4. The **masked SMC clobbers r4–r12** — any Linux SMC thunk must save them (our
   first two probes died on that, looking exactly like a hung monitor).
5. The **OMAP musb glue reads `mode`, not `dr_mode`** — `mode = <2>` for
   peripheral, else "unsupported port mode 0" and no UDC.
6. **OMAP4 VBUS/ID sensing lives in the TWL6030** — without
   `&twl_usb_comparator { usb-supply = <&vusb>; }` the UDC binds but stays
   "not attached" forever.
7. A **gated target's read can abort** (DSI PHY, and `CM_WKUP` when addressed at
   the wrong base) — do risky accesses from a forked userspace worker.
8. **`jiffies`/event-log timestamps are honest to ~10 ms**, but a **tickless
   idle kernel** runs far fewer timer IRQs than `HZ`: never count ticks to
   estimate time, and never drive a periodic job from the tick.
9. **`BOOTCTR_FCTRY` out of range is a brick, not a fallback.**  `0` or
   `> 0x32` ⇒ *"expired factory mode"*, no boot, no software recovery.  Only
   ever write `0x32`, always read it back, and never on the working unit.
10. **A stable counter is not an unconsumed counter.**  `BOOTCTR_FCTRY` read 50
   at the end of ~40 boots; the reason was `/accounts/devuser/startup.sh`
   repairing it on every TabletOS boot, not inertia.  Before concluding anything
   is inert, look for the thing that restores it.
11. **`/tmp` is volatile — do not put anything durable there.**  The host gets
   powered off; `/tmp/pbssh.sh`, `/tmp/*.log` and `/tmp/ifs.bin` all vanish, and
   an earlier session began with no way to reach the device because the only
   copy of the SSH wrapper was in `/tmp`.  The wrapper now lives at
   `analysis/pbssh.sh`; `door-hold.sh` survives but its *process* does not; the
   user keeps `/tmp` copies in `tmp-backup/`.  Cold-start order and the
   `lsusb` product-ID table (including "nothing at all = cable/power"): bootstrap
   §1.1.
12. **A host tool's default timeout will cut a door poll short.**  Waiting for
   the door needs 1–2 min after TabletOS boots; a 30 s default killed the first
   attempt.  Always pass an explicit long timeout, or poll in the background.
   Measured door timings: cold start (supervisor only) **9 s**, supervisor
   respawn after the holder is killed **12 s** — and a dead door presents as
   `ssh: … port 22: Connection refused`, since the SSH path *is* the door.
13. **Never `pkill -f` the door or the BerryShell holder.**  A shell whose own
   command line contains `door-hold.sh` or `BerryShell-V4.py` matches the
   pattern, so `pkill -f` kills the shell running the test (it aborted one here,
   exit 143).  Kill by explicit PID instead — `pgrep -f 'BerryShell-V4[.]py'`
   (character class, so the pattern does not match your own argv) then
   `kill <pid>`.
14. **A write to a DSS target that is in module standby never completes.**  The
    Linux boot path inherits `CM_DSS_DSS_CLKCTRL = 0x00040F02` — MODULEMODE=2
    but **STBYST (bit 18) = 1** — because nothing is consuming `dss_clk`, and
    the DSS functional clock comes from a different DPLL_PER state than QNX's
    (118 §7).  In that state reads of DISPC/DSS `SIGBUS` and a **write to
    DSS_CONTROL (0x48040040) hangs the interconnect for ever**, which also
    wedges PID 1's `reboot()`: the unit then needs a *physical power cycle*.
    That happened once this session.  Rule: check `CM_DSS_DSS_CLKCTRL` bit 18
    before writing any DSS target; if it is set, use a kernel driver to claim the
    clock, do not poke.
15. **`/init`'s 180 s auto-reboot is reliable only while no bus cycle is
    wedged.**  Fresh boots recovered by themselves (verified twice); a boot with
    a hung L4 write did not.  So a failed *read* costs one cycle, a failed
    *write* can cost a power cycle.
16. **Blocking I2C is the same hazard.**  `bl dump` (a plain `ioctl(I2C_RDWR)`
    on `/dev/i2c-*` straight from the console process) hung the OMAP i2c driver
    and wedged the whole unit — PID 1's `reboot()` did not complete, exactly
    like trap 14.  Any device probe from the console must run in a forked
    worker with an alarm (the `disp` path already does), and the parent must
    use a **bounded** `waitpid`, never a blocking one.  The kernel driver path
    is no safer: binding `ti,lm3532` against 0x38 did the same thing (118 §9).
17. **A *stale* door holder also presents as `Connection refused`.**  The
    supervisor only respawns when the holder **exits**; a holder that survived
    a unit reboot keeps running but its connection is dead, so the door stays
    down for ever.  `kill <holder pid>` (explicit pid, never `pkill -f`, see
    trap 13) makes the supervisor start a fresh one, and the door is back within
    ~12 s.  This cost a round of "the door is broken" after the second power
    cycle.

## 8. Subagents, and how to ask questions the docs do not answer

**Use subagents for static work only.**  They are the right tool for
reverse-engineering the QNX binaries (`rizin`/`arm-linux-objdump` over
`playbook-dev/dumped4869ifs/proc/boot/` and `/home/psyden/SSDdrive/4869/berry-rcfs/`),
for reading large document sets, and for anything that would otherwise eat the
main context.  They are **not** bench actors: only one process may own the USB
link at a time, so if a subagent is to flash or drive the console, nothing else
may touch the unit, and it must be told the exact loop from bootstrap §2.

**Two hard-won rules for briefing them** (both were violated this session, both
cost a full agent):

1. **"Write the report file first, as a skeleton, then improve it in place."**
   Two RE agents hit their turn limit at 100 turns and would have delivered
   nothing without that instruction (one did deliver ~48 KB but its tail is
   unfinished — see 113's status note).
2. **Give them: the exact paths, the TRM text
   (`bootdumps-2026-09-11/swpu231ap.pdf.txt`), the ARM binutils path
   (`/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin/`),
   the report style reference (`106`/`107`/`111`), an explicit "stop when the
   question is answered", and what NOT to touch.**

**Asking questions — the relay channel exists and works.**  This handoff cannot
contain everything, and the person driving the session talks to the bench and to
earlier agents.  So: **when something is genuinely unclear or the docs conflict,
say so and ask the user to relay the question to the previous session's agent**
(rather than guessing or re-deriving it the hard way).  That channel resolved
several wrong turns this session:

- the epoch-test direction correction (`WDT2 CRR counts **up**`);
- the question about WDT2's reload value, which led to measuring
  `LDR = 0xfffc1800` (7.8 s) instead of trusting the 58.6 s read on live QNX;
- the `BOOTCTR_FCTRY` semantics: **RPMB, not NVRAM**, decremented per
  factory/CRC boot, topped up by `/accounts/devuser/startup.sh`, and **out of
  range = "expired factory mode" hard lock**, not a fallback.

Ask crisply: one question, the evidence you have, and what you would do either
way.  And when an answer contradicts a doc, **fix the doc in the same turn** —
that is how this file stays trustworthy.

## 9. Kickoff prompt for session 5 (paste this)

```
PlayBook (OMAP4430 "winchester") clean Linux 6.15.11 bring-up — session 5.

READ FIRST, in this order:
1. bootdumps-2026-09-11/analysis/HANDOFF-2026-10-27-session5.md   (state + next steps)
2. bootdumps-2026-09-11/analysis/NEXT-SESSION-BOOTSTRAP-2026-10-26.md  (the manual:
   access, build/flash loop, capture formats, instrumentation index, traps)
3. bootdumps-2026-09-11/analysis/109-epoch-test-2026-10-27.md      (measurements)
4. .../116-rpmb-bootctr-plan-2026-10-27.md  ONLY if the work touches RPMB/BOOTCTR.
5. .../111-re-display-2026-10-27.md (+112) if the work is display.

WHERE WE ARE: the old "1.4 s global CPU halt" was the kernel's own VFS panic
(no rootfs), not hardware.  Nothing to fix in PRCM/VC/VP, the secure world or
CPU1.  Working: DRAM-ring console, WDT2 petted (7.81 s arming value, TGR kick
from a 2 s timer), initramfs PID 1, eMMC fully initialising (mmcblk1 = Samsung
MCGAFA 59.4 GiB), USB gadget (0fca:8010 CDC-ACM) and an interactive REPL on
/dev/ttyGS0 (host /dev/ttyACM0) with help/ls/cat/peek/poke/kick/reboot.
Harness: TabletOS up, door held, sealed IFS sha256 c72b0530…, BOOTCTR_FCTRY=0x32.

FIRST TASK — do this, in this order (no need to ask which to pick):
  a. Get access: bootstrap §1.1. The SSH wrapper is ./pbssh.sh in the analysis dir
     (durable; /tmp is volatile). Start door-hold if needed, check `lsusb |
     grep -i 0fca` against §1.1's table, then poll DOOROK with a LONG timeout
     (the door needs 1-2 min after TabletOS boots).
  b. Launch ONE static-RE subagent for 111 R1 (reverse winch_lcdctl+0x1cce0 for
     the H/V->register assignment and the DSI PLL/PHY values). Static only, so it
     is safe to run while you work.
  c. Meanwhile, on the bench over the USB console (no flash needed): 111 R4
     (CM_DSS_CLKCTRL/DSS_CM2 state) and as much of R5 as is meaningful, i.e.
     DISPC register readback; note that DISPC/DSI need the DSI PLL before
     FRAMEDONE/VSYNC can toggle.
  Prefer the USB console for everything it can express; flash only when the
  kernel or the initramfs changes.

IF THE BENCH LOOKS DARK: `lsusb` showing no 0fca device at all means cable or
power, not software — say so instead of hunting in the docs. TabletOS shows as
0fca:8010 with iProduct "RIM Network Device"; our kernel shows the same ID with
"PlayBook (Linux bring-up)".

GUARDRAILS: never write RPMB (a wrong BOOTCTR_FCTRY = "expired factory mode" =
brick, physical recovery only); never mount/write the eMMC; single-owner USB;
always restore playbook-live-spare-2026-10-25/ifs0.sealed.bin and verify its
hash; don't trust the printk-ring tail (cache-stale) — use pb_ring.py /
pb_events.py, and /dev/kmsg is unreliable near a fault (write the DRAM ring
directly, see 115).

SUBAGENTS: static RE only (no bench); tell them to write the report skeleton
FIRST then refine, give them exact paths + the TRM text file + the ARM binutils
path, and an explicit "stop when answered".  If the docs are unclear or
contradict each other, ask me and I will relay the question to the previous
session's agent — don't guess.

UPDATE THE DOCS as you go; the handoff + bootstrap are the entry points and must
stay true.
```
