# NEXT SESSION BOOTSTRAP — PlayBook OMAP4430 Linux bring-up

**There is a paste-ready kickoff prompt in `HANDOFF-2026-10-27-session6.md` §6**,
and §5 of the same file lists the traps (14–19 from session 5, 20–26 from
session 6); the bootstrap §6 list is the long-form version.  Read that first for
state and next steps.

**Start with `HANDOFF-2026-10-27-session6.md`** — it is the state + next-steps
entry point (updated at the end of session 6).  §6 there is the paste-ready
kickoff prompt for session 7 and **§7 is
the subagent briefing rules and the question-relay channel** (when the docs are
unclear or contradict each other, ask the operator: one question, the evidence,
and what you would do either way — and fix the doc in the same turn).  This file
is the manual (access, loop, capture, instrumentation, traps); read it next.  For
the display work, read `120-lm3532-panel-enable-2026-10-27.md` first (the
backlight is **solved** — do not re-derive it), then
`118-live-qnx-display-registers-2026-10-27.md` before `111`/`112` — 118 is the
live measurement and it corrects both.  `109-epoch-test-2026-10-27.md` is the
measurement story; `105`/`HANDOFF-2026-10-26-global-stall.md` are session-3
history (their premise — a hardware "wall" — has been disproved).

This file is the "how everything works" manual: access, the build/flash/capture
loop, capture formats, the instrumentation index, and the mistakes that cost
time.

> ## SESSION 7 UPDATE (2026-11-15) — panel lit, DSI matches the reference, no image yet
>
> **The authoritative state, next steps and command cheat-sheet are in
> `HANDOFF-2026-10-27-session6.md` §0/§1/§4.**  This block is the short version of
> what changed, so the manual below can be read with the right assumptions.
>
> **What works now:** the display pipeline is up under omapdrm (`/dev/dri`,
> `/dev/fb0`, `card0-LVDS-1` connected+enabled, `LINE_STATUS` advancing), the
> LM3532 backlight is on (`bl_power=0`, brightness 255, chip `0x1d=0xf9`), the
> TC358764 is alive, and the SPI TCON init over MCSPI4 is implemented and has been
> exercised in both byte orders and both orderings.  **The panel is lit but
> black.**
>
> **Three corrections that invalidate older text here:**
> 1. **The reference runs FOUR DSI data lanes, not two.**  `DSI_COMPLEXIO_CFG1`
>    is `0x2A054321` on QNX vs `0x2A000321` for a two-lane build.  1024x600 at
>    60 Hz RGB888 is 1.2096 Gbps ⇒ 302.4 Mbps/lane ⇒ a **37.8 MHz byte clock**,
>    which is exactly QNX's `TL = 1008`.  The "2× link rate" that 118 §8.2 called
>    benign was a lane-count mistake; with `lanes = <0 1 2 3 4 5 6 7 8 9>` and
>    `lanes = <4>` on the bridge, `COMPLEXIO_CFG1`, `VM_TIMING3` (`0x03F00258`)
>    and the byte clock become byte-identical to the reference (report 121 §11).
> 2. **`DSI_CTRL` differs in exactly two bits**: `CS_RX_EN` (bit 1, RX-only) and
>    `VP_CLK_POL` (bit 8).  Matched → `0x00EAEF9D`, same as QNX (TRM Table 10-530).
> 3. **The TC358764 needs no programming.**  Its control port is DSI-generic only
>    (not I²C), and nothing in the boot ROM, the eMMC second-stage loader, the IFS
>    or the RCFS programs it — the IFS splash proves DISPC + DSI + SPI TCON +
>    LM3532 all work with the bridge untouched.  Do **not** try to program it, and
>    never write `SYS_RST <- 0x1f` (it degrades the reference display until a cold
>    battery pull — trap 28 in the handoff).
>
> **What remains** is the *distribution* of the timing fields, not the link:
> `DISPC_TIMING_H` `0890B400` vs `0890A113`, `DISPC_DIVISOR` `00010003` vs
> `00010002`, `DSI_VM_TIMING1` `00086067` vs `01079074`, `DSI_CLK_CTRL`
> `A0306008` vs `A0346005`.  Totals already agree (1344 × 625 ⇒ 50.4 MHz).  Force
> all four together — never a partial mix.
>
> **New bench tooling this session** (all in `analysis/initramfs/console.c`):
> `spi tcon` / `spi w <hexword>…`, `fb <rrggbb>` (paint `/dev/fb0` — **always do
> this before judging the panel**; a black framebuffer and a dead panel look
> identical), `kmsg [substr]` (filtered kernel log — it answered in one command
> what register guessing had failed at for hours), and `i2c16 r|w` (16-bit
> register addressing).  For the reference OS: `analysis/pbi2c` (source
> `qnx-i2c.c`), an I²C probe built with the qnx660 toolchain.
>
> **Bench-loop corrections** (handoff §4 and traps 27–32): a flash cycle's
> precondition is media running, not the ramloader; the flasher's bootrom window
> is 300 s and it exits silently; `--reuse-loader` can fail and leave the bootrom
> blinking red (recover with a plain sealed-IFS flash); `/dev/ttyACM0` must be
> chmod'd after **every** boot; `pgrep -f`/`pkill -f` match your own command line.
> After a **host** reboot the door supervisor is gone — start `door-hold.sh`.
>
> **Unit:** sealed IFS verified (`c72b0530…`), TabletOS up, door up,
> `BOOTCTR_FCTRY = 0x32`.  `top_up_bootctr` in `/accounts/devuser/startup.sh` was
> fixed this session (it must **execute** `dd` to test readiness, not stat it) and
> now logs the record's previous value.  **The agent must not write RPMB** — that
> guarded script (or the operator) only.

---

> ## SESSION 6 UPDATE (2026-10-27) — the panel is lit; the LM3532 enable is GPIO2_16
>
> Full report: **`120-lm3532-panel-enable-2026-10-27.md`**.  Operating notes:
>
> * **The backlight works.**  The LM3532 at 0x38 is gated by **GPIO2_16**
>   (OMAP `gpio_48`, ball `gpmc_a24`, `CONTROL_CORE_PAD0_G` `0x4A100070[15:0]`).
>   Measured on the bench: line low ⇒ 0x38 NAKs, high ⇒ it ACKs.  The reference
>   leaves it high; our boot path resets GPIO2 (`OE=0xffffffff`,
>   `DATAOUT=0`) *and* leaves the pad at mux 7, so there was no connection at
>   all.  It is now a **pinctrl state + gpio-hog** in the board DTS.
> * **Use a hog, not a `regulator-fixed` GPIO.**  A regulator only drives its
>   GPIO at `regulator_init_complete()` (late_initcall_sync), which loses the
>   race with async device probing: same image, one boot bound `lm3532-led`, the
>   next did not (an i2c probe that NAKs is never retried).
> * **`bl_power = 4` silently swallows brightness writes** (`actual_brightness`
>   still echoes your value).  Fixed by naming the backlight in the panel node,
>   so `drm_panel_enable()` unblanks it — `bl_power=0`, brightness 255 at boot.
> * **The reference OS is an oracle now.**  `analysis/pbi2c` (source
>   `qnx-i2c.c`) is a QNX ARM I²C tool (`probe/rd/wr/scan/dump`) built with the
>   in-workspace qnx660 toolchain; deploy it with
>   `./pbssh.sh 'cat > /tmp/pbi2c' < pbi2c`.  It answered in minutes what
>   guesswork had failed at for two sessions (see report 120 §0).
> * **Killed hypotheses** (each tested on the reference itself): the TWL
>   `0x49[0x92]←0x30` "PWM init" (`0x92` reads 0 on QNX too), the `0x49` bit-0
>   config difference, the AUX rails (still ACK with `regen1` off), and REGEN1 /
>   GPIO1_2.  Also: **GPIO4 is not reset by our boot path** and already matched
>   QNX byte-for-byte, so those "untested candidates" were not tests at all.
> * **`PWM_TWL=y`/`PWM_TWL_LED=y` bind (`pwmchip0/1`) but do nothing for the
>   backlight.**  `TWL6030_GPADC` cannot be `=y` while `CONFIG_IIO=m`.
> * **New traps:** `/dev/ttyACM0` is recreated per enumeration (chmod after
>   *every* boot); the flasher's bootrom window is 300 s and it exits silently
>   (start it immediately before the reboot); `pmic-twl6030-ctl set_state vaux1
>   off` takes the whole unit down (needed a power cycle) — never toggle VAUX1
>   on the reference.
> * **Still missing: the panel TCON init over SPI (MCSPI4, `0x480BA100`)** —
>   lit but no image.  That is the next task (111 §3.5).

> ## SESSION 5 UPDATE (2026-10-27) — display mode measured; DSS is power-domain-gated
>
> Reports **`118-live-qnx-display-registers-2026-10-27.md`** (live capture) and
> **`117-re-display-r1-2026-10-27.md`** (static) supersede the *inferred* half
> of 111.  Operating notes for the next session:
>
> * **The DSS power domain is a separate switch.**  `PM_DSS_PWRSTCTRL`
>   (`0x4A307100`) `POWERSTATE` must be 3, then `CM_DSS_CLKSTCTRL`
>   (`0x4A009100`) = 2, then `CM_DSS_DSS_CLKCTRL` (`0x4A009120`) = `0xF02`.
>   Only then does `IDLEST` go to 0 and the DISPC/DSI registers become
>   readable.  With the domain off, `MODULEMODE` latches but `IDLEST` stays 3
>   and **every access SIGBUSes** — including the DSI PHY page that 112 saw
>   abort.  `memdump3` dieing on `0x48044200` is not proof of a firewall.
> * A live `memdump3` read of a *disabled* target is a SIGBUS, and QNX logs
>   `run fault pid … signal 10` in `sloginfo`.  `sloginfo` is the friendliest
>   way to see what the display stack did (`LCDCTL Winchester DVT`,
>   `L2DSS_Easy_QNX_Display`, `LCD is Hitachi`).
> * `winch_lcdctl` does **not** enable its own clocks; run it only with the
>   domain already on, or it dies at `_btext+0xde1c`.
> * A register **write** tool now exists on the unit:
>   `/accounts/devuser/memw32 <hexaddr> <hexval> [mask]` (deployed from
>   `rea-work/memw32`).  PRCM/clock pokes only — **never** RPMB, eMMC or
>   BOOTCTR.
> * Measured mode: 1024×600, HBP/HFP/HSW 138/162/20, VBP/VFP/VSW 12/12/1,
>   pixel clock **50.400 MHz**, `DISPC_TIMING_H=0x0890A113`,
>   `DISPC_TIMING_V=0x00C00C00`, `POL_FREQ=0x00030000`,
>   `DIVISOR=0x00010002`, `SIZE_LCD=0x025703FF`, `CONTROL=0x02000309`
>   (LCD manager, not DIGITAL).  DSI PLL `CONFIG1=0x14A5E8BE` (REGM 756,
>   REGN 95, M4=M5=6), `CONFIG2=0x00056000`.
> * **GPIO176 (GPIO6_16) reads 0 until the panel rail is up**, so a cold
>   panel-ID read says "LG" and selects the wrong SPI table.  This unit is
>   **Hitachi**; the panel control port is **MCSPI4** (`0x480BA100`), which
>   QNX calls `/dev/spi3`.
> * The panel stayed **dark** with DSS/DISPC/DSI all correct until `screen`
>   woke it — the LM3532 backlight is genuinely mandatory (111 §4.3).
> * **Do NOT poke the DSS targets from userspace on this kernel.**  The Linux
>   boot path leaves the module in standby (`CM_DSS_DSS_CLKCTRL = 0x00040F02`,
>   bit 18 **STBYST = 1**) because nothing consumes `dss_clk`; reads of
>   DISPC/DSS then `SIGBUS` and a **write to `DSS_CONTROL` (0x48040040) never
>   completes**, wedging the console *and* PID 1's `reboot()` — the unit then
>   needs a physical power cycle (happened once, session 5).  Let a kernel
>   driver claim the clocks instead (118 §7).
> * The initramfs console now has `disp init|stat|mux|col|off` and
>   `bl init|dump|rd|wr` (report 118 §7) and `analysis/pbcon.py` to drive it from
>   the host (`PBCON_TMO=<s>`; it waits for the `pb> ` prompt).  Its `disp`
>   subcommands are now mostly redundant — omapdrm does the display properly
>   (below) — but `disp stat` is the quickest way to dump the live DSS/DISPC/DSI
>   registers, and `poke`/`peek` remain useful.
> * `/dev/ttyACM0` is `root:uucp 0660`; `pkexec chmod 666 /dev/ttyACM0` works
>   non-interactively on the bench host (the user is in `wheel`).  `sudo` needs
>   a password, so use `pkexec`.
> * **The display pipeline works under omapdrm (118 §8).**  `CONFIG_DRM=y` +
>   `DRM_OMAP` + `DRM_PANEL_LVDS` + `DRM_TOSHIBA_TC358764` + `LEDS_LM3532`,
>   the DTS in 118 §8.1, and three `tc358764.c` board patches.  The unit comes
>   up with `/dev/dri/` + `/dev/fb0` (`omapdrmdrmfb`), DISPC scanning
>   (`LINE_STATUS` moving, `VSYNC1`) and the DSI PLL locked.  Note the OMAP host
>   takes the lane count from the **host endpoint's `lanes` property** and the
>   PLL range from `mipi_dsi_device.hs_rate` — a bare `panel-lvds` on the DSI
>   port does *not* work, because then no `mipi_dsi_device` ever attaches.
> * **Do not do blocking I2C from the console.**  `bl dump` hung the OMAP i2c
>   driver and wedged the whole unit (PID 1 could not reboot) — trap 16.  Fork
>   with an alarm and use a bounded `waitpid`.  Binding the kernel
>   `ti,lm3532` driver at 0x38 does the **same thing** (118 §9), so the node is
>   in the DTS but `status = "disabled"`; the slave is most likely unpowered and
>   holding the bus.  Lead: QNX forces `REGEN1` on and, on DVT3+/PVT4,
>   `regen1`/`regen2` become **TWL GPIO 2 / GPIO 15**; `vaux3` is also enabled
>   by `pmic-twl6030-ctl -p regen1,vaux3`.
> * **A stale door holder looks like a dead door.**  After a unit reboot the
>   holder can stay alive with a dead connection, so the supervisor never
>   respawns it and SSH says `Connection refused` for ever.  `kill <holder pid>`
>   (explicit pid) → a fresh holder in ~12 s.  Trap 17.
>
> ## SESSION 4 UPDATE (2026-10-27) — **there is no hardware wall**
>
> Full data and reasoning: **`109-epoch-test-2026-10-27.md`**.
>
> **The "global CPU halt" is the kernel's own VFS panic.**  With the console on,
> the boot ends at `[2.440] Kernel panic - not syncing: VFS: Unable to mount
> root fs on unknown-block(0,0)`; `panic()` disables IRQs, so the tick stops and
> the DRAM log ends exactly like a frozen CPU.  `loglevel=0` suppresses even
> KERN_EMERG, which is why session 3 never saw it.  Every "wall" number is just
> where the boot was when `prepare_namespace()` failed (2250 ms silent,
> 2440 ms with the console on).
>
> The **1.38–1.53 s cluster is the WDT2** (7.812 s after arming, see below),
> arriving before `prepare_namespace`; the D=4000 run is the same watchdog
> cutting `start_kernel`.  Session 3's busy-spin "deaths" were watchdog kills,
> not a freeze.
>
> So: nothing to fix in PRCM/VC/VP, the secure world, or CPU1.  The SMC liveness
> probe confirms the monitor is **present and returns instantly** (`select_kek`
> → `-1`, no KEK) at 300/800/1300/2000 ms.
>
> **An initramfs is now in the tree and it works**: `/init` runs as PID 1, dumps
> `/proc/interrupts`, and the boot no longer panics (109 §8).  The chainloader
> blink loops were trimmed (`0x04000000` → `0x00200000`), cutting the pre-kernel
> phase from 2764 ms to **840 ms**.
>
> **The eMMC works (109 §9).**  The card had been answering CMD1 all along;
> the host's `ocr_avail` was 0 because `&mmc2` had no supply.  A
> `regulator-fixed` 3.3 V rail (`vemmc`) plus `vmmc-supply = <&vemmc>` gives
> `ocr_avail = 0x00300000`, and the card comes up:
> `mmc1: new high speed MMC card at address 0001`, `mmcblk1: MCGAFA 59.4 GiB`,
> plus `boot0`/`boot1` and `rpmb` (chardev only — nothing mounted, nothing
> written).
>
> **Housekeeping done (109 §8.1, §9).**  The chainloader's LED blink loops are
> gone (`blink_n`/`delay`/`led_on` deleted); the loader is now 828 B and the
> pre-kernel phase is **781 ms** (two uncached copies of ~7.3 MB, which is
> inherent).  The WDT2 handling is one documented policy: kick `TGR` once at gic
> init and from a 2 s `timer_list` (`PB_WDT_KICK_HZ`), with a one-line boot
> diagnostic; verified with a heartbeat of **14 510 ms** and `/init` printing
> "STILL ALIVE at 12 s (past the WDT2 window)".
>
> **Interactive bench console works (115).**  `analysis/initramfs/console.c`
> (688 lines, static `/console`) is spawned by PID 1 after the ACM gadget
> binds and gives a REPL on `/dev/ttyGS0` ↔ host `/dev/ttyACM0`:
> `help`, `ls`, `cat`, `peek <hex> [n]`, `poke <hex> <val>`, `kick on|off`,
> `reboot`.  Every `/dev/mem` access runs in a forked worker with a 3 s alarm,
> so a gated target's L4 abort is a SIGBUS in the worker, not the console — and
> the transcript mirrors into the DRAM ring.  `/init` reboots unconditionally
> 180 s after start, so the unit can never wedge (no remote power control).
> **Bench iterations no longer need a flash cycle** for anything that a
> `peek`/`poke`/`cat` can express; use the flash loop only when the kernel or
> the initramfs changes.
>
> **USB gadget works (114).**  Linux now enumerates on the PC as
> `0fca:8010`, CDC-ACM, `/dev/ttyACM0`, product string *"PlayBook (Linux
> bring-up)"* — the host even sends SET_LINE_CODING, so it is a real
> conversation.  Four fixes were needed: build the USB stack in (it was all
> `=m`); the OMAP glue reads the **legacy `mode = <2>`** property, not
> `dr_mode` ("unsupported port mode 0"); `interface-type = <1>` (UTMI+);
> and `&twl_usb_comparator { usb-supply = <&vusb>; }` — OMAP4's VBUS/ID
> comparators are in the TWL6030, and without that the UDC binds but sits in
> "not attached".  Next easy win: a getty/shell on `/dev/ttyGS0`, which removes
> the flash cycle from the loop.
>
> **Both probes were run from userspace — `112-bench-r3-usb-m3-2026-10-27.md`.**
> *Display*: DSS (rev 0x40), DISPC (rev 0x40, RESETDONE), DSI1 core (rev 0x30)
> and the DSS clock registers all read fine, and `CM_DSS_CLKSTCTRL` is already
> `0x00000f02` (SW_WAKEUP — QNX's own value), so the display plan is viable.
> Reading the **DSI PHY page `0x48044200` aborts** — expected while its PLL is
> off; power the PLL first.  *USB*: DPLL_USB locked (0x7), OTG instance out of
> idle, TLL+host clocked, `MUSB_DEVCTL = 0x80` (BSESSVLD, SESSEND=0, IDDIG=0 =
> device), and `USBOTGHS_CONTROL` already `0x15`; re-writing it changes nothing,
> so M3 is a no-op and the next step is simply to bind a gadget (M4).
>
> **Display RE is done — `111-re-display-2026-10-27.md`.**  `winch_lcdctl` is
> the whole DSS/DSI driver: DSI1 video mode, RGB888, 2 lanes, into a TC358764
> DSI→LVDS bridge, 1024×600, panel init over **SPI3** (exact byte tables for
> both the Hitachi and LG panels, selected by GPIO176), backlight = **LM3532 on
> i2c 0x38** (not TWL, not PWM).  Two findings change the plan: QNX lights the
> panel by writing the LM3532 *directly* at splash time (`/dev/backlight` does
> not exist yet), and its only explicit DSS clock action is
> `DSS_CM2 CLKTRCTRL = 2 (SW_WAKEUP)` at `0x4A009100`.  **Next bench step is
> its R3**: one boot that only *reads* `DSS/DISPC/DSI_REVISION` and
> `CM_DSS_CLKCTRL` — if those read back sane, the DSS plan is viable; if they
> are 0/0xFFFFFFFF, the blocks are firewalled and no driver work will help.
>
> **WDT2 is neutralised (109 §8.1).**  `CM_WKUP_WDTIMER2_CLKCTRL` is
> `0x4A307830` (WKUP_CM + 0x30), not `0x4A31C030` — that one is the keypad.
> The module clock is already on; **`LDR` writes are dropped**, but the `TGR`
> kick does reload `CRR`.  A `timer_list` kicks it every 2 s (from the *tick*
> it misses, because an idle kernel is tickless).  Result: heartbeat 22 140 ms
> and `/init` printed "STILL ALIVE at 20 s, no panic, no reset".  The board no
> longer resets, so any experiment can take as long as it needs.
>
> Still-true measurements from this session: the pre-kernel delay was exact and
> the halt is kernel-entry relative (a 1000 ms delay changed it by 0 ms), and
> the MPU domain (`CM_MPU_CLKSTCTRL`/`MPU_CLKCTRL`/`PM_MPU_PWRSTST`/
> `CM_IDLEST_DPLL_MPU`) is unchanged from first tick to last.
>
> Three numbers in this file were wrong and are corrected in place below:
> - **WDT2 on the Linux path is armed with `LDR = 0xfffc1800` → 7.812 s**, not
>   `0xffe2b400`/58.6 s, and it is re-armed 7 ms before the chainloader starts.
>   The unit **reset-loops every ~7.8 s** while a test image is on media. The
>   "~55 s reset" was the restore flasher catching a bootrom after several
>   iterations.
> - The chainloader's pre-kernel phase is **1.77–4.78 s** (12 ×
>   `0x04000000`-iteration uncached delay loops in `blink_n`) and varies per
>   boot.  Kernel t0 is a further **1519 ms** after the jump (decompressor).
> - The DTS currently carries
>   `earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused
>   panic=10`.  **Keep `ignore_loglevel` for diagnosis** — `loglevel=0`
>   suppresses even KERN_EMERG, which is what hid the VFS panic for two
>   sessions.  Switch back to `loglevel=0` only for timing runs where the
>   console load matters, and decode the event log, not the ring.


---

## 1. Hardware / device state

- Spare 64 GB PlayBook, OMAP4430 ES2.2 "winchester", secure device.
- TabletOS (QNX 6.6) is up; `uname -a` →
  `QNX localhost 6.6.0 2011/11/11-16:00:16EST OMAP4430_Winchester_ES2.2_HS_PVT4_Rev:07 armle`.
- Region 0 (Linux test area): `/dev/hd1`; sealed IFS is at media offset
  `0x100000`, length `0x2BFE10`, sha256
  `c72b053027769c4d49270e9ee208fd2b0afcf69ce4fb3cf0c0be3c938ee05299`.
- `BOOTCTR_FCTRY` = `0x32` (50), read from `/dev/rpmb/BOOTCTR_FCTRY`
  (read-only; never write RPMB; keep RPMB off in the kernel).
- The door is a TCP qconn door kept alive by `door-hold.sh`
  (`BerryShell-V4.py ... hold`).  Plain SSH then works.

### 1.1 Door / SSH — and the cold-start procedure

The SSH wrapper lives in the workspace, **not** in `/tmp`:
`analysis/pbssh.sh` (it carries the same ssh options as `pb_ring.py` /
`pb_events.py`).  Earlier sessions used `/tmp/pbssh.sh`, which silently vanished
on every host reboot — that is exactly why a session once started with no way to
reach the device.

```bash
# 1. the door supervisor (restarts itself; harmless if already running)
pgrep -af door-hold || nohup /home/psyden/rea-work/door-hold.sh >/dev/null 2>&1 &

# 2. is the unit even there?  (see the USB table below)
lsusb | grep -i 0fca

# 3. wait for the door.  Measured: ~10 s if TabletOS is already up and only the
#    holder was restarted (cold start 9 s, supervisor respawn 12 s); 1-2 min if
#    the unit has just been rebooted into TabletOS, because that is the OS boot,
#    not the door.  Give the poll a LONG timeout: a host tool default of 30 s
#    kills it before the door appears.
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
for i in $(seq 1 40); do
    timeout 12 ./pbssh.sh 'echo DOOROK' 2>/dev/null | grep -q DOOROK && { echo "door up ($i)"; break; }
    sleep 5
done
timeout 20 ./pbssh.sh 'echo OK; uname -a'
```

Door-down looks like `ssh: connect to host 169.254.0.1 port 22: Connection
refused` — i.e. the SSH path *is* the door; there is no separate listener.  The
supervisor respawns the holder within ~2 s of it exiting and logs
`hold exited rc=…; restarting in 2s` to `/tmp/berryshell-hold.log`.

**Cold start after a host reboot** (`/tmp` is volatile — logs, `ifs.bin`, old
`/tmp/pbssh.sh` are all gone; `door-hold.sh` itself is durable but the *door*
process is not; `tmp-backup/` holds a copy of `/tmp` if it was saved):

1. `pgrep -af door-hold || nohup .../door-hold.sh &` — it logs to
   `/tmp/berryshell-hold.log` and retries every 2 s.
2. `lsusb | grep -i 0fca` and match against this table **before** concluding
   anything is broken:

| seen | meaning | what to do |
|---|---|---|
| nothing | unit unplugged / powered off / wrong port | ask for the cable or power; nothing software-side helps |
| `0fca:0001` | boot rom (USB recovery) | run `pre_os_ifs_flash.py --template-ifs <sealed>` or `force_reset_loader.py` |
| `0fca:8001` | ramloader | `force_reset_loader.py` once |
| `0fca:8010`, `iProduct = RIM Network Device` | **TabletOS running** (RNDIS) — the normal harness state | start the door, wait 1-2 min |
| `0fca:8010`, `iProduct = PlayBook (Linux bring-up)` | **our kernel + gadget** | `/dev/ttyACM0` should exist; the door will *not* answer (TabletOS is not running) |

3. Poll for `DOOROK` with a long timeout, as above.

`pb_ring.py`/`pb_events.py` embed the same SSH options themselves, so they work
without the wrapper.  If `analysis/pbssh.sh` is ever lost, its full ssh option
list is reproduced in `103-clean-kernel-crc-bringup-2026-10-26.md` §3.1 and at
the top of `pb_ring.py` — the key ones are `-i /home/psyden/rea-work/rsa`,
`HostKeyAlgorithms=+ssh-rsa`, `KexAlgorithms=+diffie-hellman-group1-sha1`,
`Ciphers=+aes128-cbc`, target `root@169.254.0.1`.
QNX shell lacks `head`, `xxd`, `od`, `md5sum`, `cmp`, `bc`, `python`.
Use `tail`, `dd`, `strings`; plain SSH stdout is binary-clean.  Never run
bare `dd` (it blocks on stdin).  `/tmp` **and `/tmp` on the QNX side** are wiped
on reboot.

### 1.2 Reading memory / registers from QNX

```bash
timeout 20 ./pbssh.sh 'on -C 0 /accounts/devuser/memdump3 4a314000 0x50'
```

Output is one 32-bit little-endian word per line (`addr: value`).
Peripheral reads work for live, clocked targets.  A **gated** target can
return 0 or fault memdump3 with SIGBUS (the ABE region at 0x40130000 did).
DRAM reads always work.

### 1.3 Verifying the media is in the sealed state

```bash
timeout 60 ./pbssh.sh 'dd if=/dev/hd1 bs=512 skip=2048 count=5632 2>/dev/null' > /tmp/ifs.bin
python3 - <<'PY'
import hashlib
d = open('/tmp/ifs.bin','rb').read()[:0x2BFE10]
print(hashlib.sha256(d).hexdigest())   # expect c72b0530...
PY
timeout 20 ./pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | xxd -p  # expect 32000000
```

---

## 2. The build → flash → boot → capture loop

All paths absolute; run from anywhere.

```bash
C=/home/psyden/rea-work/playbook-dev/kernel-clean
export PATH=/home/psyden/SSDdrive/wsl-migration/home/psyden/toolchains/armv7-eabihf/bin:$PATH

# 1. build
make -C "$C" ARCH=arm CROSS_COMPILE=arm-linux- -j12 zImage
make -C "$C" ARCH=arm CROSS_COMPILE=arm-linux- ti/omap/omap4-winchester.dtb

# 2. wrap into the chainloader IFS image (prints the CRC)
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
ZIMAGE=$C/arch/arm/boot/zImage \
DTB=$C/arch/arm/boot/dts/ti/omap/omap4-winchester.dtb \
python3 build_kernel_image.py
```

```bash
# 3. flash the test image while TabletOS is up
cd /home/psyden/rea-work/bootdumps-2026-09-11/analysis
python3 pre_os_ifs_flash.py --template-ifs chainloader/kernel.img  > /tmp/f1.log 2>&1 &
FPID=$!
sleep 2
timeout 30 ./pbssh.sh '/proc/boot/shutdown -S reboot'
wait $FPID                      # tool: waits for bootrom, uploads ramloader,
                                # PreFlash 0x20EE, frames, 0x40F9, 0x40C0,
                                # then calls bb.reboot_loader()

# 4. the tool's reboot usually leaves the unit at 0fca:8001 (ramloader);
#    one force-reset boots the image on media
timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py

# 5. restore the sealed IFS while the test kernel runs.  The tool polls for the
#    boot rom, so just start it now and leave it running:
#      - current images pet WDT2 and /init reboots 180 s after start, so it
#        catches the boot rom then (--delay 0..30 is fine), or
#        `reboot` from the USB console makes it immediate;
#      - an image that does NOT pet WDT2 dies ~7.8 s after the bootloader armed
#        it (not 55 s - that number was the boot rom window of a reset loop).
python3 pre_os_ifs_flash.py \
  --template-ifs playbook-live-spare-2026-10-25/ifs0.sealed.bin --delay 30 \
  > /tmp/f2.log 2>&1 &
RPID=$!
wait $RPID

# 6. one more force-reset boots TabletOS (otherwise it can stay in the loader)
timeout 40 python3 playbook-live-spare-2026-10-26-clean/force_reset_loader.py

# 7. wait for the door (17-25 polls of 5 s), then capture
for i in $(seq 1 40); do
  timeout 12 ./pbssh.sh 'echo DOOROK' 2>/dev/null | grep -q DOOROK && break
  sleep 5
done
python3 pb_ring.py  dump --ring 88000000 --out playbook-live-spare-2026-10-26-clean/latest-ring.txt
python3 pb_events.py | tee playbook-live-spare-2026-10-26-clean/latest-events.txt
```

Notes that matter:
- `pre_os_ifs_flash.py` waits up to 300 s for the bootrom; always start it
  **before** the reboot/force-reset.
- `force_reset_loader.py` opens USB id `0x8001`, sends `0x80EF` (→ `0x0`,
  then Errno 19 disconnect = success).  One invocation per phase; if it
  prints "no ramloader enumerated" it means the loader already went away —
  just wait.
- `--delay 30-40` on the restore is what makes it catch the WDT bootrom.
- The door takes ~1-2 min to return after TabletOS boots.
- Single-owner USB: check `ps -ef | grep -E 'BerryShell|pre_os_ifs_flash|force_reset'`
  before flashing.
- `CONFIG_MMC_BLOCK=n` for probe-only builds, restore `=y` afterwards.

### 2.1 Recovery if the unit looks stuck

- If `lsusb` shows `0fca:8001`, it is the ramloader: run
  `force_reset_loader.py` once.
- If `lsusb` shows `0fca:0001`, it is the bootrom: start
  `pre_os_ifs_flash.py --template-ifs .../ifs0.sealed.bin` now.
- If `0fca:8010`, TabletOS is up but the door is still coming up.
- A power cycle is the last resort (loses nothing on media, but resets the
  door and any in-progress capture).

---

## 3. Captures: what is authoritative

### 3.1 DRAM console ring (authoritative)

- Ring1 PA `0x88000000`, 32 KB.  `+0x80` count, `+0x84` index (wrap
  `0x7F00`), chars `+0x100..+0x7FFF`.
- Written by DEBUG_LL every printk character through the permanent
  **non-cacheable** alias `0xFE800000` (also `0xFE900000`, `0xFEA00000` for
  rings 2/3).  No cache maintenance at all.
- The chainloader zeroes `+0x80..+0xFFF` and sets count=0 each boot, so the
  ring always describes the **last boot**.
- `python3 pb_ring.py dump --ring 88000000 --out FILE`; the tail is newest.
- The ring is 32 KB; long boots wrap and the beginning is lost.

### 3.2 Printk ring (`__log_buf`) — do NOT trust the tail

- PA `0x81027ad0`, 64 KB; descriptors `0x80f523c8`; `last_seq` in the
  `printk_rb_static` struct at `0x80f26380` (+0x14).
- It is cacheable, so after a wedge the newest records may never have
  reached DRAM.  This session it stopped at seq 894 / ts 1.415 s while the
  DRAM ring ran to 1.49 s.  Use it for early boot only, never for the tail.

### 3.3 Printk-free event log (new this session)

Layout in the ring page's breadcrumb area (survives the chainloader):

| off | meaning |
|---|---|
| `+0x00` | heartbeat `(0x3700<<16) \| (jiffies & 0xffff)` (per TWD/GPT tick) |
| `+0x04` | scratch (last initcall fn ptr / async func / test counter) |
| `+0x08` | magic `0x50424556` "PBEV" (set in `gic_of_init`) |
| `+0x0C` | event counter |
| `+0x10` | 28 × u32; slot = counter % 28; word = `(code16<<16) \| (jiffies & 0xffff)` |

- `python3 pb_events.py` (or `--raw FILE`) decodes it, resolves the `+0x04`
  pointer against `kernel-clean/System.map`, and prints the last 28 events
  with ms timestamps.
- `jiffies` starts at `INITIAL_JIFFIES` = `-300*HZ`; the decoder subtracts
  `0x8AD0` from the low 16 bits.  (HZ=100.)
- Accesses are `__raw_writel/__raw_readl` (no barrier): a stuck posted
  peripheral write cannot stall the log.  A heartbeat that advances proves
  the CPU is alive; a stopped one does **not** prove it is dead.

Event code map: `0x11xx` set_ios entry (pm), `0x13/14/15xx` set_clock,
`0x16/17xx` init_stream, `0x18xx` set_ios done, `0x21..26xx` send_init_stream
steps, `0x31xx` request CMDn, `0x41/42xx` MMC IRQ, `0x50xx` MMC probe,
`0xa0xx` mmc_rescan, `0xa30x` mmc_attach_mmc, `0xa40x` mmc_power_off,
`0xb00x` hsmmc RPM suspend, `0xb01x` hsmmc RPM resume, `0xb02x/0xb03x`
ti-sysc suspend/resume, `0xb10x` RPM callback/suspend core, `0x600x`
prepare_namespace, `0x700x` wait_for_device_probe, `0x71/72xx` async sync,
`0x8000` initcall entry, `0x81xx/0x82xx` initcall level start/end,
`0xe0xx/0xd0xx` GIC IRQ enter/done, `0xe1xx/0xe2xx` TWL watchdog.

---

## 4. Where the bring-up stands

Works (verified session 4):
- **eMMC on MMC2**: full card init, `mmcblk1` = Samsung MCGAFA 59.4 GiB,
  high speed, boot0/boot1/rpmb enumerated.  Requires the `vemmc` fixed
  regulator (`ocr_avail`), see 109 §9.
- **Userspace**: a static initramfs `/init` runs as PID 1 (dumps /proc, then
  reboots), so `prepare_namespace()` is never reached.
- **WDT2 petted** by a 2 s `timer_list` kick (`TGR` read/invert/write; `LDR`
  writes are dropped), so the board no longer resets mid-experiment.

Works (verified session 3):
- DRAM, earlycon/DEBUG_LL ring, capture v2.
- GPIO2–6, i2c1–4, GPMC, PMU, IOMMU, DSS target (no reset), SGX/IVA off.
- MMC2 host probe, power-up, CMD0 init stream, `mmc_add_host`, **card rescan
  reaches CMD1 ×3 with real completion IRQs**, rescan completes, MMC runtime
  autosuspend and resume complete.
- MMC2 target verified awake: `fck=96 MHz, MODULEMODE=ENABLED, IDLEST=0,
  STBYST=0, CLKSEL=1, CLKTRCTRL=SW_WKUP, SYSCONFIG=0x14 (SIDLE/MIDLE=NO)`.

Current wall:
- **A global CPU halt at ~1.40–1.53 s, independent of the code running.**
  A 30 s `cpu_relax` busy spin with IRQs on dies ~60 ms in; the periodic
  tick (TWD IRQ 29 / GPT IRQ 37) stops.  The always-on WDT2 resets the SoC
  ~7.8 s later on this path (the "~55 s" seen in session 3 was the boot rom
  window of a reset loop, see below).
  *Corrected in session 4 (see 109): **this is not a freeze.**  With the
  console on the boot ends in `Kernel panic - not syncing: VFS: Unable to mount
  root fs` — there is no root filesystem, and `panic()` disables IRQs so the
  tick stops.  The end-of-log position is just wherever
  `prepare_namespace()` ran (2250 ms silent, 2440 ms with the console).  The
  1.40–1.53 s numbers are the WDT2 (**7.81 s** after arming) arriving first.*
- Ruled out: printk/console load, cpuidle/NO_HZ, SmartReflex/AVS, MMC2
  clocks/idle, the RPM callback path, the TWL6030 watchdog reload.
- Best remaining suspects: the pre-Linux PRCM/VC setup
  (`setup-core-inactive`), the secure world (`trustzone-omap4`), and CPU1
  (QNX's first action is `setup-core-inactive full`; Linux leaves CPU1 as
  the bootloader parked it).
  *Session 4 narrowed this: the wall is kernel-entry relative, so the secure
  timer / hardware-epoch family is out (109).*

---

## 5. Instrumentation index (what is patched in `kernel-clean`)

| file | what |
|---|---|
| `include/linux/pb_event.h` | event log + `pb_event/pb_ptr/pb_tick/pb_event_init` |
| `drivers/irqchip/irq-gic.c` | `pb_event_init()` in `gic_of_init`; IRQ enter/done events; heartbeat for IRQ 29/37 |
| `init/main.c` | initcall entry `0x8000` + last fn in `+0x04`; level start/end |
| `init/do_mounts.c` | prepare_namespace `0x6001..0x6004` |
| `drivers/base/dd.c` | wait_for_device_probe `0x7001..0x7004` |
| `kernel/async.c` | async sync `0x7101/0x7102`, pending count `0x72xx`, oldest func in `+0x04` |
| `drivers/base/power/runtime.c` | callback/suspend `0xb1xx` |
| `drivers/bus/ti-sysc.c` | runtime suspend/resume `0xb02x/0xb03x` |
| `drivers/mmc/host/omap_hsmmc.c` | PBX state dump, PBIRQ/PBINI prints, events, device link, `ti,no-idle` support |
| `drivers/mmc/core/core.c` | rescan/attach/power_off events |
| `drivers/mmc/core/mmc.c` | attach events |
| `drivers/mfd/twl-core.c` | TWL6030 watchdog reload (`0x2C=127`) + events |
| `arch/arm/boot/dts/ti/omap/omap4-l4.dtsi` | `mmc2_target:` label |
| `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` | `&mmc2_target { ti,no-idle; sidle/midle NO; }`; bootargs |
| `.config` | `CONFIG_MMC_BLOCK=y` (documented state), `CONFIG_MMC_RPMB` off, `POWER_AVS_OMAP=y` |

Session 4 additions (epoch test, see 109):

| file | what |
|---|---|
| `analysis/chainloader/chainloader_kernel.S.in` | `EP_TICKS` pre-kernel delay driven by the always-on WDT2 CRR; epoch block at breadcrumb `+0x88` (`EPCH`, status, LDR, CRR at kernel entry, 32k sync counter) mirrored to all four pages and **re-published every delay iteration** |
| `analysis/build_kernel_image.py` | `EP_TICKS=<ticks>` env → 32.768 kHz ticks of delay (0 = compiled out) |
| `analysis/pb_events.py` | now reads `0xec` bytes and decodes the epoch block (LDR, hand-over split, loader duration, CRR at entry, 32k) **and** the kernel MPU-state sampler |
| `drivers/irqchip/irq-gic.c` | `pb_state_init()` (ioremap 0x4A004000 / 0x4A306000 / 0x4A304000) + `pb_state_tick()` on INTID 29/37: latches `CM_MPU_CLKSTCTRL`, `CM_MPU_MPU_CLKCTRL`, `PM_MPU_PWRSTST`, `CM_IDLEST_DPLL_MPU` and the 32k counter into ring `+0xb4..+0xe8` (first, last, change count, jiffies of last change) |
| `arch/arm/boot/dts/ti/omap/omap4-winchester.dts` | `bootargs` = `earlyprintk keep_bootcon ignore_loglevel maxcpus=1 clk_ignore_unused panic=10` (current, diagnosis mode — `loglevel=0` hides panics, see the headline note) |
| `analysis/initramfs/{init.c,console.c}` → `root/{init,console}` | static PID 1 and the USB REPL (115).  `init` = mounts, configfs ACM gadget, forks `/console`, bounded 180 s lifetime; `console` = `help/ls/cat/peek/poke/kick/reboot`, `/dev/mem` accesses in forked workers, transcript to the DRAM ring |
| `.config` | `CONFIG_INITRAMFS_SOURCE=.../analysis/initramfs/root`; USB stack built in (`USB`, `USB_MUSB_*`, `USB_GADGET`, `USB_CONFIGFS(+ACM/ECM/RNDIS)`, `OMAP_USB2`, `TWL6030_USB`, `CONFIGFS_FS`); `MMC_BLOCK=y` |
| `drivers/irqchip/irq-gic.c` | SMC liveness probe (`pb_smc1`, ring3 `+0x00..+0x78`), fired at 300/800/1300/2000 ms, bracketed like `trustzone-omap4`; GIC census into ring3 `+0x38..+0x70`.  **The thunk must save r4-r12** — the monitor clobbers them |

Loader artifacts: `chainloader/kernel-d{0,1000,2000,3000,4000}.img`
`kernel-smcprobe-r12fix.img` (CRC `0xB5B8CE9D`) is the current probe build
(SMC liveness + GIC census + MPU sampler, console on).  Note `chainloader/kernel.img`
is currently the same build.
A test image on media leaves the unit in the 7.8 s reset loop until the sealed
IFS is restored.

Facts established by that instrumentation:
- WDT2 `LDR` = `0xfffc1800` → **7.812 s**; re-armed 7 ms before the chainloader
  entry; CRR counts **up**.  The unit reset-loops every ~7.8 s while a test
  image is on media.
- Chainloader pre-kernel phase = **1.77–4.78 s**, variable per boot (12 ×
  `0x04000000`-iteration uncached `blink_n` loops).
- `0x4A314028` (WDT2 CRR) and `0x4A304010` (32k sync counter) are both readable
  from pre-kernel code with MMU/caches off.  The 32k counter is reset per boot
  (its epoch is the reset into the bootrom).

**Source patch — regenerated 2026-10-27 (session 5), applies cleanly.**
`analysis/clean-kernel-6.15.11-changes.patch` (2 482 lines, 28 files,
`--- pristine/… +++ clean/…`), and the same delta in git style at
`playbook-dev/kernel-patches/0001-playbook-winchester-6.15.11.patch`
(`--- a/… +++ b/…`) alongside a refreshed `winchester.config`.  Both are
verified with `patch -p1 --dry-run` against a real pristine 6.15.11.

**The pristine tree exists after all:** `/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/pristine`
(unmodified 6.15.11 — `VERSION 6 / PATCHLEVEL 15 / SUBLEVEL 11`, no `PlayBook`
strings), with `linux-6.15.11.tar.xz` next to it.  **Do not diff against
`…/kernel/linux`:** that is the *modified* kernel tree from the earlier kexec
project — neither pristine nor the current tree.

Regenerate with (paths matter — this is what produced the current file):

```bash
K=/home/psyden/SSDdrive/wsl-migration/home/psyden/kernel/pristine
C=/home/psyden/rea-work/playbook-dev/kernel-clean
diff -rq -x '*.o' -x '.*.o.cmd' -x '*.cmd' -x '*.dwo' -x '*.a' -x '*.ko' \
     -x '*.mod' -x '*.mod.c' -x '*.symtypes' -x '*.order' -x '*.dtb' \
     -x '*.dtbo' -x '*.tmp' -x '*.d' -x '*.i' -x '*.su' -x 'Module.symvers' \
     -x 'modules.builtin*' -x 'vmlinux*' -x 'System.map' -x 'Image' -x 'zImage' \
     -x '*.cpio*' -x '.config' -x '.config.old' -x '.version' -x '*.orig' \
     -x '*.rej' -x '.git' -x '.tmp_*' -x '*.log' "$K" "$C" > /tmp/kdiff-list.txt
# 'diff -rq' reports modified files as "Files … differ" and *new* ones as
# "Only in $C: …".  Take the former, drop the scripts/dtc/include-prefixes
# symlink duplicates, then add the genuinely new sources by hand:
grep '^Files ' /tmp/kdiff-list.txt | sed 's/^Files //; s/ and .*//' \
  | sed "s|$K/||" | grep -v '^scripts/dtc/include-prefixes/' > /tmp/kfiles.txt
printf '%s\n' arch/arm/boot/dts/ti/omap/omap4-winchester.dts \
              arch/arm/include/debug/omap4bc.S >> /tmp/kfiles.txt
sort -u /tmp/kfiles.txt -o /tmp/kfiles.txt
while read -r f; do
  diff -u -N --label "pristine/$f" --label "clean/$f" "$K/$f" "$C/$f"
done < /tmp/kfiles.txt > clean-kernel-6.15.11-changes.patch
cd "$K" && patch -p1 --dry-run --forward < …/clean-kernel-6.15.11-changes.patch
```

Traps in that procedure: `diff -rq` **follows** the `scripts/dtc/include-prefixes`
symlinks, so `arch/arm/boot/dts/**` changes appear three times — filter them;
`-x` matches basenames only, so `-x config` would eat real files; and new files
must be added explicitly because `diff -rq` lists them as "Only in".

---

## 6. Lessons / traps from this session

1. `maxcpus=1` is **not** a valid kernel parameter ("Unknown kernel command
   line parameters"); the kernel is built NR_CPUS=1 anyway.  CPU1 is left as
   the bootloader parked it.
2. `drivers/base/dd.c:619` prints `PBR: dev <- driver` **before** calling
   the probe.  A `PBR:` line without the driver's own follow-up means the
   probe hung (not that it succeeded).
3. The console is expensive and shifts which code is running at the
   time-based wall.  `loglevel=0` (with `keep_bootcon`) silences it and lets
   the boot get much further; use it for timing/machine-state experiments.
4. The wall is time-based, not device-based: do not chase the last printed
   device.  Test hypotheses with the busy-spin and the event log.
5. `jiffies` starts at `-300*HZ`; subtract `0x8AD0` (low 16 bits) for ms.
6. The GPT1 counter is restarted by the clockevent driver; it is not a
   free-running clock.
7. Disabling the OMAP4 VC/VP entirely (`vp_param/vc_param = NULL`) causes an
   *early* hang at ~0.37 s in `__omap2_common_pm_late_init` — do not read
   that build as "the VC/VP is fine".
8. Live QNX peripheral reads can return 0 for a gated target or SIGBUS
   memdump3 (ABE).  Do not conclude "register is 0".
9. `force_reset_loader.py` after **each** flash phase; the tool's own
   `reboot_loader()` often leaves the unit enumerated as 0x8001.
10. The sealed IFS must be restored after every test image or TabletOS will
    not boot (the test image replaces the IFS region).
11. `pb_ring.py`/`pb_events.py` need the door; wait 1-2 min after TabletOS
    starts.
12. QNX shell: no `head`/`xxd`/`md5sum`; use `tail`/`strings`/`dd` and hash
    host-side.
13. The event ring is circular (last 28 events); if you need more, add a
    dedicated slot or make the ring bigger (it is 0x6C bytes of breadcrumb).
14. Keep the ring alias non-cacheable; any cache maintenance in the debug
    path aborts after L2C takes secure ownership.
15. **A test image on media makes the unit reset-loop every ~7.8 s** (WDT2).
    The capture you read is always the *last* boot before the restore, not
    necessarily the one you triggered — never assume the first iteration.
16. **The chainloader's blink phase (1.77–4.78 s) is bigger than any delay you
    will add** and it decides which freeze line you observe.  Measure it from
    the epoch block (`this loader=N ms`), never assume it is constant.
17. WDT2 `CRR` counts **up** from `LDR`; `sub start, now` silently makes every
    delay 0 ms.  Elapsed = `now - start`.
18. `pb_ring.py dump` cannot read a `loglevel=0` boot (count 0).  Use
    `pb_events.py`; use `pb_ring.py --ring 90000000` for the decompressor's
    ring (it carries `Uncompressing Linux...` plus the first printk lines).
19. The epoch block lives at `+0x88..+0xb3`; `+0x80/+0x84` and `+0x100..` are
    the ring's, and anything written before `clear_ring` there is wiped.

---

## 7. QNX/TabletOS IFS — the reference OS

Location: `playbook-dev/dumped4869ifs/` (`proc/boot/.script` is the raw
boot script; the rest are the boot binaries/drivers).

Boot order (from `.script`):

```
setup-core-inactive full
resource_seed dma=0,31
devc-seromap (0x48020000) ; slogger ; pipe
trustzone-omap4 -U99:102
i2c-omap35xx-omap4 (0x48070000, 0x48072000, 0x48350000)
omap4430-wdtkick -t 15000 -P -U 99:99      # WDT2 + TWL6030 PMIC watchdog
pmic_twl6030_cfg
pmic-winchester-twl6030 -U 99:99
sh /proc/boot/pmic-twl6030-ctl_script      # regulator config
devb-mmcsd-winchester (blk cache=10M ... crypto=enable)
spi-master, splash, backlight_win, rpmb-omap4, nvram_resource_manager ...
```

Key facts already recovered:
- `omap4430-wdtkick -P` kicks the OMAP WDT2 **and** the TWL6030 PMIC
  primary watchdog; `-T` = TWL timeout (default 127 s); the TWL kick writes
  i2c 0x48 register **0x2C = timeout seconds**; the OMAP kick reads TGR
  (0x30), inverts, writes back.
- OMAP WDT2 on the live QNX: LDR `0xffe2b400` ≈ 58.6 s, TGR `0xffffd47b`,
  CRR counting down — the ~55 s reset source.
- `setup-core-inactive` strings: `prcm_vc_global_init`, `prcm_configure_volcon`,
  `prcm_device_init`, `prcm_reinit_dmnstates`,
  `prcm_sr_interface_config_domain`, `prcm_vc_bypass_write 0x3a`,
  `prcm_vc_bypass_write 0x40`, "Core voltage is 1225"/"1250".
### 7.1 RE results (three reports written this session)

`106-re-setup-core-inactive.md` — recovered the ordered PRCM/VC/VP setup:
- `prcm_vc_bypass_write` = write `(VALID<<24)|(DATA<<16)|(REGADDR<<8)|SLAVE`
  to **`PRM_VC_VAL_BYPASS` = `0x4A307BA0`**, then spin on VALID with **no
  timeout**.
  - `0x013A5612`: DATA `0x3A` → TWL6030 slave `0x12`, reg `0x56` (VDD_MPU
    `SR_CMD`)
  - `0x01405512`: DATA `0x40` → TWL6030 slave `0x12`, reg `0x55` (VDD_MPU
    `SR_VOLT`)
- Both modes run `cpupm_init`; "full" additionally idles ~58 modules
  (`prcm_module_disable`), does L3/L4 interconnect RMWs, and writes
  `PM_MPU_PWRSTST(0x4A306304)=0x03000000`, `CM_SDMA_STATICDEP(0x4A008A04)=0`,
  `0x48243400/0x48243800 = 0x00030107` (A9 private blocks, secure-gated on
  this unit), and sets `CLKSTCTRL=SW_SLEEP` for a few domains.
- The `dpll_*/prcm_*` strings are config-dictionary keys, not OPP data;
  "Core voltage is 1225/1250" is dead code.
- Ranked hazard: the unbounded VC VALID poll (if the TWL SR path NACKs).
  Note: the busy-spin test rules this out for *our* wall — a software spin
  in any other thread cannot run while the spin test holds the CPU.

`107-re-trustzone-omap4.md` — this driver does **no** SMC at startup, no
timers, no GIC/FIQ, no periodic servicing; the only SMC (`0x7C4C`) is on
demand from a devctl client.  Its only persistent startup state is:
`CM_MPU_STATICDEP[14] = L4SEC_STATDEP` enabled and
`CM_L4SEC_CLKSTCTRL = HW_AUTO`.  So a "secure handshake Linux is missing"
is **not** required.  Best remaining secure hypotheses: (a) a secure-only
timer with a local MPU reset — the report flags **WDTIMER1 `0x4A318000`**
(note: this is also Linux's always-on clockevent, target-module@8000), and
(b) a forced MPU power-domain transition.  It also gives an SMC liveness
probe (`smc #1`, cmd `0x31`) to test the monitor.

`108-re-wdtkick-pmic.md` — `omap4430-wdtkick` fully recovered: it only
reads `TGR` (`0x30`), inverts and writes it back (kick period `-t` ms,
default re-arm from a one-shot timer, pulse 126).  It never enables,
disables or presets WDT2.  The TWL kick is a plain 2-byte write
`{0x2C, seconds}` to `0x48`; `0x2C` = PM-master `+0x0D` =
`PRIMARY_WATCHDOG_CFG` (7-bit seconds, max 127), **enabled by default in
TWL6030B1**, expiry = PMIC **WAIT-ON/OFF** (rails down).  The ~55 s reset
is WDT2 (`LDR 0xffe2b400` → 58.6 s), which nothing in QNX reprograms.
*(Session 4 correction: that LDR is the **live QNX** value.  On the
chainloader/Linux path WDT2 is armed with `LDR = 0xfffc1800` → **7.812 s**,
re-armed 7 ms before the chainloader entry, so the unit reset-loops every
~7.8 s between test boots — see 109.)*
**The PMIC watchdog is exonerated for the 1.4 s halt**: its action removes
the rails, which would also kill the always-on domain and the 55 s WDT2
reset (that reset always happens).  The report still gives the correct
neutralisation (kick `0x2C=0x7F` every 30 s, kick WDT2 `TGR` every 15 s)
for later bring-up.

### 7.2 Highest-value next measurement: **DONE in session 4** — result below

~~Insert a fixed delay D in the chainloader...~~  **Done. Report: `109-epoch-test-2026-10-27.md`.**

- Delay mechanism actually used: `EP_TICKS` ticks of the always-on **WDT2 CRR**
  (32.768 kHz, counts up), i.e. an exact wall-clock delay with no kernel timer.
- D=0 (×3): halt **2240 ms** kernel time, jitter ≤ 10 ms.  D=1000: entry
  +1000 ms exactly, halt **2240 ms** ⇒ **no shift**.
- Verdict: **kernel-entry anchored**, not a hardware epoch.  Report 106's
  PRCM/VC/VP/MPU family; the secure-timer family (107) is out.
- Two extra hazards found: a second freeze line at **~6.26 s after WDT2
  arming** (shows only with late kernel entry; it is the session-3 1.38–1.53 s
  cluster) and the WDT2 overflow at **7.81 s**, which at D=4000 cuts the kernel
  inside early `start_kernel`.
- Cap on D: with a 7.81 s WDT2 window and a 2.8 s loader phase, D must stay
  under ~2 s unless the WDT2 `LDR`/`TGR` is extended first.

---

## 8. Next steps (updated 2026-10-27, session 4 close)

**How to iterate now.**  The unit is only reachable over USB, and with the
console live the loop for hardware experiments is: flash the current image once,
then drive everything from the host with
`stty -F /dev/ttyACM0 raw -echo; printf '<cmd>\n' > /dev/ttyACM0 & timeout 5 cat /dev/ttyACM0`.
Remember the two traps from 115: the host line discipline echoes the gadget's
own output back into it if you write before setting `raw -echo` (the console
deliberately stays silent until the first host byte), and `/dev/ttyACM0` is
`root:uucp 0660` (the session used `pkexec chmod 666`, transient — the node is
recreated per enumerate and is absent once TabletOS is back).

Remaining work, in order (no wall to chase — 109; kernel, userspace and eMMC
all work):

1. **Display driver work** — R3 is done (112): everything is reachable, the DSI
   PHY page only aborts until its PLL is on.  Follow 111 §6 (DTS recipe), R4
   (`CM_DSS_CLKSTCTRL=2`, already the value) and R5 (verify by register
   readback: `DISPC_TIMING_H/V`, `POL_FREQ`, `DIVISOR`, `SIZE_LCD`, then
   `FRAMEDONE`/`VSYNC` toggling, then DSI PLL lock).  The LM3532 backlight must
   be driven directly over i2c (111 §4.3) or the panel stays dark.
2. **Display R1/R2** (static, no bench): close the H/V↔register assignment and
   the DSI PLL/PHY values by reversing `0x1cce0`, and plan the LM3532 backlight
   writes into the panel's `enable` hook (QNX's own ordering means nothing
   else lights the panel at splash time).
3. ~~USB gadget / console~~ **done (114, 115)**: `0fca:8010` CDC-ACM plus the
   interactive REPL.  Optional follow-ups: ECM/RNDIS for a netdev on both sides
   (TabletOS used RNDIS/ECM in this role), and a *real* root filesystem over the
   gadget instead of an initramfs.  `113-re-usb-device-driver-2026-10-27.md`
   (TabletOS's own `devu-omap4430-musb.so`) is written but unaudited — its tail
   ends mid-work, so treat its conclusions as unverified and it is not needed
   for anything that currently works.
4. **RPMB / `BOOTCTR_FCTRY`** (long-term, and blocking before Linux becomes the
   normal OS).  It is an **RPMB** record (**not** NVRAM — 108 §4.4 corrected).
   Every factory/CRC boot **decrements** it; `0` or `> 0x32` **hard-locks the
   unit** (*"expired factory mode"*, refuses to boot, recovery is desoldering the
   eMMC).  So each Linux boot must write **exactly `0x32`** and verify it — and
   never test that write on the working unit.  `/accounts/devuser/startup.sh`
   does that on every TabletOS boot — **but only from 2026-10-27 onward**: the
   script used to build the value with `printf`, which does not exist on
   TabletOS, so it was writing **`0x00000000`** (the brick value) six times a
   boot and logging "ok" because its check merely re-read the counter.  Fixed,
   verified and deployed (116 addendum; workspace copies in
   `analysis/device-startup.sh{,.orig-2026-10-27}`).  **Do not read earlier
   "it always read 50" observations as proof the repair worked.**  RPMB key recovered: the 32-byte fallback literal
   `"retreating is advancing in the o"` (the KEK path fails on this unit).
   Full plan, semantics, ladder and porting discipline: **`116-rpmb-bootctr-plan-2026-10-27.md`**.
   (`74`'s blanket "never write it" is superseded — writing `0x32` is the normal
   operation — but the hazard is a *wrong* value, which bricks: 116 §2.2.)
5. **Root filesystem** once either of those is useful: either mount a
   read-only partition of `mmcblk1` from the initramfs (currently forbidden by
   the read-only-eMMC guardrail — revisit deliberately, never write), or keep
   using the initramfs.
6. Long-term WDT2 story: the 2 s `timer_list` kick is the bring-up answer
   (`omap_wdt` cannot pet an already-running bootloader-armed WDT2 because it
   only arms on `open("/dev/watchdog")`); hand over once PID 1 pets the real
   driver.
