# Handoff — session 30 (absolute)

Self-contained.  **A paste-ready kickoff prompt is at the end (§5).**
Numbering: the reports call the Linux-era sessions "session 1…N", i.e. absolute
`N+6` — this handoff is for **absolute 30 = Linux-era 24**.  See
`docs/sessions/NUMBERING.md`.

> **Session-29 update (read first).  The CM3217 now works; the WL1283 is
> parked.**  Two reports are the session: **180** (WL1283) and **182** (CM3217).
>
> **ROADMAP item 9 step 2 is COMPLETE.**  `cm3217.ko` binds `3-0010` and
> `in_illuminance_input` reads **32–37 lux with the room lights off and 191 with
> them on**.  The whole bug was **timing**: there is *no* settle delay — the count
> is valid the instant `0x26` goes to 0x10, and every wait from 1 ms to 200 ms
> reads back zero, so the driver polls for the first nonzero pair and then
> restores the vendor idle state (`0x60`→0x11, `0x27`→0x10).  **Leaving the part
> enabled wedges the panel's own automatic brightness** — any future tool that
> pokes this chip must put it back.
>
> **The WL1283 does not enumerate and is parked for fresh eyes.**  It is on
> **MMC5 (`0x480D5000`)**, not MMC1; WL_EN = **GPIO2_22**, WL_IRQ = **GPIO2_21**
> (input).  `&mmc5` probes and clocks and the card never answers CMD5
> (`STAT=00018000` = CTO).  **Every SoC-side surface now matches the reference**
> — pads CORE+WKUP, all six GPIO banks (GPIO4 mirrored), all 256 PMIC registers,
> CM2/PRM (we hold *more* domains awake), the FREF mux — and the chip still does
> nothing: no SDIO, no HCI answer on UART2, IRQ never driven.  Read report 180 §6
> before touching it; it lists what is already falsified so it is not retried.
>
> **Two rules this session produced, both worth internalising:**
> * **DECISIONS §30** — ask the reference OS what it maps (`pidin mem` /
>   `pidin irq`) before writing a DT node.
> * **Trap 103** — do not characterise a device on TabletOS if a QNX driver owns
>   it; our accesses raced `ls-cm3217` and produced every false reading we had.
>
> **Working images:** region-0 **`0xFC32F008`**
> (`local/images/kernel-0xFC32F008-cm3217.img`) + region-1
> `local/images/rootfs-region1.ext4` (`0faeadff…`, 16 modules).
> **MEDIA STATE: our Linux image is in region 0 — the 1917 IFS is NOT on media**
> (the session ended on Linux, operator's call).  Restore it with
> `tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs
> --dest-block 0x825 --from-bootrom --password 0000 --apply`, and re-read
> `BOOTCTR_FCTRY` — it was `0x32` at the last TabletOS boot and is **unverified**
> since.
> **`tools/bbusb.py` is the flasher**; pack both regions for one bootrom session
> (`DECISIONS.md` §29).  Kernel patch re-exported: 5 703 lines / 34 files.

## 0. The job, and where it stands

**Goal.**  A clean, upstream **Linux 6.15.11** on a **BlackBerry PlayBook**
(OMAP4430 ES2.2 "winchester", secure device), booting our chainloader image in
media region 0 (`/dev/hd1`, IFS at region-0 offset `0x100000`) with an
initramfs that hands PID 1 to a **real userland on the eMMC** (media region 1),
and the board's own sensors/peripherals working.

**Working:** the SoC basics (DDR, UART3 console, eMMC on MMC2, WDT2), the USB
gadget/console, the whole display chain — **the panel shows a clean, stable
image** (DSS/DSI, the TC358764 bridge, the Hitachi TCON table, the LM3532
backlight) — and a **64 MiB ext4 BusyBox root in region 1** at raw
`0x29000000`; the initramfs mounts it read-write, chroots and execs
`/sbin/init`, the unit boots to a shell, and `insmod`/`modprobe` work.  The
peripherals so far: I²C4 motion sensors (BMA150/HMC5883L/MPU3050), TMP103 and
the BQ27541 gauge, the FAN5702 status LED.  The kernel patch is current:
**5 703 lines / 34 files** (`kernel/patches/README.md`).

**Last known-good image:** **`0xFC32F008`**
(`local/images/kernel-0xFC32F008-cm3217.img`, session 29: everything above
plus `&mmc5` for the WL1283, the WLAN enable hog, the FREF/TWL WKUP pinctrl and
the one-shot `PBGPIO:`/`PBD:` dumps)
with the region-1 root `local/images/rootfs-region1.ext4` (sha256 `0faeadff…`,
16 modules).  `0xB759C3A2` (session 28) and the earlier steps `0x1D9159E0`,
`0x3E479F6F` and `0xAEDF68D0` are behind; `0x0D1DA0D3` is the clean pre-userland
fallback.  **Build from this.**  (Image CRC convention: `zlib.crc32(image[0:-4])`
in the container's last word; a rebuild with the same CRC has usually failed to
link — trap 39.)

**Diagnostic images** (the `pbmon` RPMB work) are built from the current
`local/kernel-tree` (which now carries the working two-stage transform);
`0x0D1DA0D3` is the clean fallback.  Session 27 built `0x4F988620`,
`0x74D34374`, `0x4EBF1F97`, `0x48166F12`, `0x1FEBC300` and `0xD3D42853`; the
last is the image that reads the store.

**The region-0 IFS is compressed and boots (session 27).**  The QNX IFS format
is the same on the PlayBook as on BB10, so `tools/bb10_ifs.py splice` +
`89-crc_tool.py reseal` turn the uncompressed 7.9 MB container into a booting
3.47 MB one (`local/work/ifs1917-compressed.ifs`).  That is the route if a
region-0 payload ever needs the room — but our chainloader image is not a QNX
imagefs, so it needs a decompressing entry path first, not just `splice` (see
the session note's open questions, including the unverified 11.25 MB figure).

**Item 6 is CLOSED (session 27).**  The transform is reproduced (report 175)
and the operator performed the single `BOOTCTR_FCTRY = 0x32` top-up **from
Linux**: `pbrpmb prep 0x3a` builds the whole pipeline read-only (authenticated
read, monitor unseal, patch 4 bytes, monitor seal, seal round-trip check, one
type-3 frame) and `pbrpmb write 0x3a yes` sends it, reads the result and
unseals the read-back.  The write was accepted, the read-back read `32 00 00
00`, a read-only scan either side showed 512/512 frames authenticating and
107/107 blocks unsealing, and the restore boot's bootloader consumed that very
record.  **An agent still never writes RPMB** (`SAFETY.md` §6): session 20's
accepted write was a *plaintext-patched* block and left the spare in the `0x5B`
loop; the fix is the monitor re-seal, not a licence to write.

**Still open:** the peripheral bring-up (ROADMAP item 9, report 176) and,
**parked by the operator**, the `hd3` cut-over.  Packaging (item 7) is
untouched.

## 0a. The way back (the unit is currently up on 2.1.0.1917)

The unit is **running PlayBook OS 2.1.0.1917** (door up, `BOOTCTR_FCTRY =
0x32`).  The ramloader/BB10 text below is the session-25 procedure for the case
where the unit is stuck in the loader.  The way back is packed and verified:

```bash
# from the ramloader — NO --from-bootrom
python3 tools/bbusb.py flash --signed local/work/back-1917.signed \
    --password 0000 --apply
```

That restores PlayBook OS 2.1.0.1917 (our 1917 IFS + edited 1917 RCFS + the
vendor `.5.ufs` with `autorun::0`), and its boot tops `BOOTCTR_FCTRY` back to
`0x32`.  Verify with `tools/door-hold.sh` and
`tools/pbssh.sh 'uname -a; mount | grep base; tail -3
/accounts/devuser/.bootctr.log'` (**root** ssh since 2026-11-05).
**Do not flash anything without the operator's go-ahead.**

From **our Linux image** the way back is the shorter one: flash
`local/work/ifs-shim-sealed.ifs` (the 1917 IFS) with
`tools/pre_os_ifs_flash.py` and reboot with `tools/pbcon.py 'reboot'`
(`DEVELOPMENT.md` §6).

## 1. The one thing to do next

**`ROADMAP.md` item 10 — port to `linux-postmarketos-stable` 7.3.3** (operator,
session 29 end).  Full plan and risks: **`KERNEL-PORT.md`**.  Step 0 is all
desk work, no bench: fetch upstream `linux-7.3.3.tar.xz` into
`local/pristine/linux-7.3.3` (keep 6.15.11), copy `local/kernel-tree` aside as
`local/kernel-tree-6.15.11`, and settle the one open question — whether
`linux-postmarketos-stable` at 7.3.3-r0 still declares `arch="all !armhf
!s390x"` (at 7.2.2 it does).  If it does, use **upstream 7.3.3** as the base
with pmOS's `config-stable.armv7` as the config starting point, not the package.

Then, for reference, the item 9 order that was in flight:
Item 5 is closed; the `hd3` cut-over is **parked**.  One variable per boot:

1. ~~The sensor bus + BMA150/HMC5883L/MPU3050.~~  **DONE (report 177).**
2. ~~CM3217 `0x10`/`0x11`, TMP103 `0x77`, BQ27541 `0x55`.~~  **ALL DONE
   (reports 178/182).**  The CM3217 works: 32–37 lux dark, 191 lit.
3. ~~FAN5702 `0x36`.~~  **DONE (report 179).**
4. **WL1283 — parked (report 180).**  Bus and wiring measured, enumeration
   never happens; see §1a for what is already falsified.
5. **Audio (WM8994 + ABE machine)** and **touch (Cypress CY8CTMA3,
   Munin/Pegasus)** — the two big RE/driver tasks.  **This is the natural next
   work**, and the touch controller is also the only other plausible source of
   an ambient-light input if the CM3217 route is ever doubted.

### The one next experiment: does the WL1283 answer when the reference clock is live?

Only when someone picks the WL1283 back up.  Everything else about it is
measured; this is the one mechanism still untested.

**Inputs.**  Report 180 §6 and this handoff's preamble.  The unit must be on
TabletOS to read the reference (that route is only for *comparison* — do not
characterise the chip from there, trap 103).  On our Linux image the knobs are:
`/sys/module/omap_hsmmc/parameters/trace`, `init_hz`; `pbhci` and
`devmem`.  The vendor config is `local/work/qnx-fw/tiwlan_winchester.ini`.

**The experiment.**  The vendor ini says the chip wants a **38.4 MHz reference
clock** (`STRFRefClock = 2`) with a **CLK_REQ** handshake.  The pads for that
(`0x4A10019C` = `fref_clk2_out`, WKUP `0x4A31E054/058/05C`) are now muxed, but
**muxing a pad does not enable its parent clock**.  So: enable the FREF_CLK2 /
DPLL_PER-M5 output path in CM, then re-run `pbhci` on UART2 and rescan MMC5.

**Steps (one variable).**  Change one clock enable, rebuild
(`DEVELOPMENT.md` §3), flash **both** regions in one bootrom session
(`pack -l both.lst -v 1,2 -s -f` + `flash --signed`), boot, then
`dmesg | grep -E 'attach_sdio|new .*card'` and `/bin/pbhci /dev/ttyS1`.

**Decision table.**

| result | meaning | next |
|---|---|---|
| `pbhci` answers (`04 0E 04 01 03 0C 00`) | the chip's digital core runs — the clock was it | stop the WL1283 hunt at the source: `wlcore` + firmware, then BT/GPS |
| SDIO enumerates (`attach_sdio` succeeds) but BT still silent | SDIO path right, BT needs its own enable/`.bts` | convert `firmware.bin` → `wl128x-fw-4-sr.bin`, `tiwlan_winchester.ini` → `wl128x-nvs.bin` |
| both still silent | the clock is not the gap | stop: the next evidence must be a bus capture or the datasheet (report 180 §6) |

## 1a. Tools that make this cheap

* **`tools/pbssh.sh`** (the door; **root** ssh since 2026-11-05, no `PB_USER`
  needed) and **`tools/pbcon.py`** (USB console — the only way into our Linux
  image); `tools/door-hold.sh` keeps the door up.
* **`tools/build_kernel_image.py`** — wraps `zImage` + DTB into a copy of the
  sealed IFS with the chainloader, prints the CRC.
* **`tools/pre_os_ifs_flash.py`** — the pre-OS `0xF7` region-0 write; it resets
  the loader itself.
* **`tools/pb_ring.py` / `pb_events.py`** — the DRAM console ring and the
  printk-free event log.
* **`tools/pbdsi`** (`qnx-dsiread.c`) — the TC358764 oracle over DSI from a
  running TabletOS; push it after every reboot.
* **`local/kernel-tree/drivers/misc/pbmon.c`** + `/dev/pbseal` /
  `/dev/pbunseal` / `/dev/pbkeys` / `/dev/pboracle` — the monitor transform.
  Session 27 implemented the bootloader's **two-stage** sequence (`0x2F` is the
  cipher, `0x2E` the digest; unseal takes 224+32 and checks the tag), added
  `/dev/pbkeys` ({keyA,keyB}) and the `op2e` parameter, and pinned both key
  defaults to the seed.  `dom` (default 5) still selects the domain.
* `tools/env.sh` — both cross-toolchains; `tools/env.sh --check`.

## 2. Traps

Full text: `KNOWN-ISSUES.md` §2.  The ones that end sessions:

9. A write to a DSS target in module standby never completes; let `omapdrm`
   own the DSS.
16. Blocking I²C is the same hazard — fork with a bounded wait.
17. A stale door holder looks like `Connection refused`; kill by explicit pid.
24. `/dev/ttyACM0` is recreated per enumeration (`pkexec chmod 666` after every
   boot; if `pkexec` is denied, the operator runs `sudo chmod`).
26. `pmic-twl6030-ctl set_state vaux1 off` takes the unit down; read rails.
27. A flash cycle's precondition is **media running** (`0fca:8010`), not the
   ramloader; end with a loader reset.
28. Panel-chain writes outlive a warm reset (`SYS_RST <- 0x1f`; trap 28).
31. Non-burst DSI video mode is rejected at this pixel clock.
33. Our images no longer auto-reboot; a boot costs one counter unit.
39. A failed link leaves the previous image with the previous CRC.
43. `local/kernel-tree/.config` can point at a different initramfs tree.
45. The DISPC core clock is raised at probe time (96 MHz).
46. Reading DSS/DSI registers from userspace perturbs the picture.
47. MCT/loader block numbers are not raw offsets: `(block-0x15)*0x10000`.
49. eMMC writes are only as large as the writer makes them (≥64 KiB).
51. The ACM console is not a bulk transport.
54. A smaller hd3 filesystem is reformatted and reseeded at boot.
59. **Never `lseek()`+`read()` `/proc/<pid>/as`** — it reset the unit.
60–63. RPMB: the block is reversed; the 224-byte block is monitor-encrypted; the
   monitor reads identity addresses below `0xB0000000`.  **No agent writes RPMB.**
80. The loader's factory count is the first diagnostic after a flash
   (`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`).
82. The bootloader's code addresses in reports 150/151 are `0x9000` too high
   (the arm9000 section loads at `0x80000000` from media `0x9000`).
83. The bootloader wipes its own RPMB state before the OS jump, and its mount
   reset-loops when called from the chainloader.  Never boot the mount probe.
84. `local/images/ifs0.sealed.bin` is the 2.0.0.4869 IFS — wrong for a 1917
   media set.  Use `local/work/ifs-shim-sealed.ifs`.
85. RPMB keys are per-unit; the compiled keys were the spare's; the transform is
   a two-stage `0x2E`+`0x2F` (report 174).
86. **The raw RPMB relay is the WIRE view** — reverse the 256-byte data field
   before `/dev/pbunseal`, or every block fails the tag check.
87. **The initramfs CRNG is unseeded** (`random: … uninitialized urandom read`)
   — fix it before any RPMB write, whose replay protection is the nonce.
88. **On our Linux image there is no door and no `/proc/boot/shutdown`**: reboot
   with `tools/pbcon.py 'reboot -f'`, and start the flasher just before it.
89–96. see `KNOWN-ISSUES.md` (monitor keys, the CRNG, the IIO module closure,
   the CM3217, the FAN5702 pad).
97. **The WL1283 is on MMC5, not MMC1**; WL_EN = GPIO2_22, WL_IRQ = GPIO2_21
   (input).  (Session 29, report 180.)
98. **`&mmc5` probes but the card never answers CMD5**; every SoC-side register
   matches the reference, so the next evidence must come from the chip.
   (Session 29, report 180 §6.)
99. **Reading one register bank past GPIO6 (`0x4805F000`) aborts and, with
   `panic=10`, reset-loops the unit** — one `BOOTCTR_FCTRY` unit per loop.
100. **Never read another module's registers from `omap_hsmmc_set_ios()`** — it
   faults on a runtime rebind and kills `sh` with irqs disabled.
101. **A QNX `SIGBUS` on MMCHS5 is block-specific**, not a firewall and not
   `ThreadCtl`; only io-pkt can read the SDIO host.
102. **Synchronise a probe with the operator action it observes**, or record the
   run as invalid rather than interpreting it.
103. **Do not characterise a device on TabletOS if a QNX driver owns it** — our
   accesses raced `ls-cm3217` and produced every false CM3217 reading.
104. **The CM3217 needs no settle delay and must be left idle** — read
   immediately (poll for nonzero) and restore `0x60`→0x11, `0x27`→0x10, or the
   panel's automatic brightness wedges.
105. **Your own knowledge is out of date — look external facts up and cite
   them.**  Kernel versions, pmaports package contents, `arch=` lists and driver
   internals drift; a session asserted `7.3.3` "doesn't match the 6.x series"
   when `kernel.org/releases.json` shows it is the current stable.  DECISIONS
   §32.

Session-25/26/27 additions relevant here: the factory counter is read with
`bbusb.py info --from-bootrom --password 0000 --keep-loader --json`
(`boot_mode.factory_count`); the password is `0000`, sent once, never retried;
`pkexec` is only for `chmod /dev/ttyACM0`, never for the USB/flash tools; **the
door ssh is root** (2026-11-05) and `devuser` cannot reboot or read `/dev/rpmb`;
**the flasher's precondition is a booted OS** — `0fca:8012` for TabletOS,
`0fca:8010` for our image — never the ramloader (`0fca:8001`).

## 3. Where things are in this workspace

| need | path |
|---|---|
| safety before any write | `docs/project/SAFETY.md` |
| build/flash/boot/console commands | `docs/project/DEVELOPMENT.md` |
| host prerequisites, toolchains | `docs/project/SETUP.md` |
| board DTS and patched drivers | `kernel/source/`, `local/kernel-tree/` |
| kernel delta + exact config | `kernel/patches/`, `kernel/winchester.config` |
| chainloader source | `boot/chainloader/chainloader_kernel.S.in` |
| tools | `tools/` (see `tools/README.md`) |
| images, sealed IFS, backups | `local/images/` (gitignored) |
| QNX boot set for RE | `local/proprietary/dumped4869ifs/proc/boot/` |
| OMAP4430 TRM (text) | `hardware/swpu231ap.pdf.txt` |
| public/private repositories | `docs/project/REPO-MANAGEMENT.md` |

## 4. Working rules

* **After every console command, dump the log tail and read it.**
* **Write the durable fact into the repository in the same turn.**
* **Do not budget work against a context window**; the operator decides when to
  stop.
* **Static RE only for subagents** (one process owns the USB link).
* **Never present a guess as a fact.**  Mark inferred claims — and note that
  *recalled* knowledge about the outside world (kernel versions, package
  contents, upstream driver internals) counts as a guess: look it up and cite
  the source.  DECISIONS §32, trap 105.
* **The whole-map diff is the highest-yield technique** — and ask the operator
  what the panel shows before building a theory.
* **Update the docs as you go**; `STATE.md` and `KNOWN-ISSUES.md` go stale
  first.
* Publishing is separate (`REPO-MANAGEMENT.md`, `tools/publish_public.sh`).

## 5. Paste-ready kickoff prompt

```
PlayBook (OMAP4430 "winchester") — session 30 (absolute; Linux-era 24)

Workspace: this repository — a git checkout.  The earlier work tree
$HOME/rea-work is READ-ONLY.  Map: docs/README.md.

READ FIRST, in this order: docs/project/SAFETY.md, STATE.md, HANDOFF.md,
KERNEL-PORT.md, ROADMAP.md items 9 and 10, KNOWN-ISSUES.md (traps 9-104,
especially 91-104), DEVELOPMENT.md section 3/3a/4, DECISIONS.md (17-31), reports
180 and 182 (and 176/177/178/179/181), sessions/session-29.md, and
docs/contradictions/fan5702-safe-model-vs-cm3217-race.md.

FIRST TASK THIS SESSION (operator): **port our patch set to
linux-postmarketos-stable 7.3.3-r0**, keeping 6.15.11 as backup/reference.  See
KERNEL-PORT.md for the target verification, per-file risk and the checkpoint
order, and ROADMAP item 10 for what closes it.  Do NOT delete
local/pristine/linux-6.15.11, local/kernel-tree or the 6.15.11 patch — copy the
working tree aside as local/kernel-tree-6.15.11 before starting.

SCOPE: the LINUX bring-up.  The hd3 cut-over, the BB10 port and the PlayBook OS
image work are PARKED by the operator — do not work on them.

PHYSICAL STATE (session 29 end): OUR LINUX IMAGE IS IN REGION 0 — region-0
0xFC32F008 (local/images/kernel-0xFC32F008-cm3217.img) and region-1
local/images/rootfs-region1.ext4 (sha256 0faeadff..., 16 modules including
cm3217.ko).  The 1917 IFS is NOT on media: restore it with
tools/bbusb.py flash --component local/work/ifs-shim-sealed.ifs
--dest-block 0x825 --from-bootrom --password 0000 --apply, then re-read
/dev/rpmb/BOOTCTR_FCTRY (0x32 at the last TabletOS boot, unverified since).
Do NOT restore local/images/ifs0.sealed.bin (2.0.0.4869, trap 84).

WHERE IT STANDS: ROADMAP item 9.  DONE: the I2C4 motion sensors (177), TMP103 +
BQ27541 (178), the FAN5702 LED (179) and the CM3217 light sensor (182: 32-37 lux
dark, 191 lit).  PARKED: the WL1283 — it is on OMAP MMC5 0x480D5000, WL_EN is
GPIO2_22 and WL_IRQ GPIO2_21 (input); the controller probes and clocks and the
card never answers CMD5, no HCI answer on UART2, IRQ never driven, and every
SoC-side register now matches the reference.  Read report 180 §6 before
touching it: the one untested mechanism is enabling the FREF reference-clock
parent (this handoff section 1).  NEXT WORK: WM8994 audio and the Cypress
CY8CTMA3 touch.

Two rules from session 29: ask the reference OS what it maps (pidin mem /
pidin irq) before writing a DT node (DECISIONS 30); and do NOT characterise a
device on TabletOS if a QNX driver owns it — our accesses raced ls-cm3217 and
produced every false CM3217 reading (DECISIONS 31, trap 103).

An agent NEVER writes RPMB (SAFETY.md section 6); the CRNG needs `entropy
<hex>` before any write (trap 87).

BENCH: door = 169.254.0.1 (host: ip addr add 169.254.0.2/16 dev <iface>);
tools/pbssh.sh logs in as ROOT.  Start tools/door-hold.sh with setsid (never
`pgrep -af door-hold`).  On our Linux image there is NO door: use
tools/pbcon.py (the shell prompt is `pb> `), and reboot with
tools/pbcon.py 'reboot -f'.  `pkexec chmod 666 /dev/ttyACM0` after EVERY boot
and the operator must answer the prompt.  Run the USB/flash tools as the USER —
never pkexec.  The device password is 0000, sent once, never retried.
Rebuild the QNX oracle tools after `. tools/env.sh`: pbmem3 = `ntoarmv7-gcc -O2
-o /tmp/pbmem3 tools/memdump3.c`, and /tmp is wiped on reboot.

SAFETY: read SAFETY.md first.  One variable per boot.  To return to Linux after
a TabletOS session, flash BOTH regions in one bootrom session
(`pack -l both.lst -v 1,2 -s -f` then `flash --signed`) — do not flash only
region 1, or the unit boots TabletOS with no console node.  Operator's call when
to stop; the operator also decides when to wrap up.

SUBAGENTS: static RE only, no bench.  Skeleton first, exact paths, an explicit
"stop when answered".

UPDATE THE DOCS as you go and commit: STATE.md, HANDOFF.md, ROADMAP.md,
KNOWN-ISSUES.md, DECISIONS.md and the session note.
```
