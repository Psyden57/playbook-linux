# Session 40 (absolute) — the WM8994 write blocker is a clocking rule; the reference's codec clock and start-up RE

> Numbering: absolute `40`; the reports call this Linux-era **session 34**
> (`N − 6`).  See `NUMBERING.md`.

Date / span: 2026-11-11.
Unit state at the end: **on our Linux image `0xF97CD9C7`** in region 0
(`0x9396B4`, kernel `7.3.3`, CRC `0xF97CD9C7` recomputed from
`boot/chainloader/kernel.img` this session), region 1 = the audio/OSS rootfs
`968d2f20…` (`local/images/rootfs-region1-audio-oss.ext4`).  Last boot:
`faults=0`, `pbrpmb: topup: BOOTCTR_FCTRY … = 31000000`, `0x31 >= threshold
0x1e, no write`.  The unit is left on our image; the 1917 IFS
(`local/work/ifs-shim-sealed.ifs`, block `0x825`) is the way back.

## Goal of this session

(1) Characterise the WM8994 register-write blocker from report 200; (2) configure
the FLL after DAPM power-up (`.trigger(START)`); (3) **after the operator's
correction, stop bench-poking and answer the questions from the reference OS** —
static RE of the 1917 binaries, a live QNX codec read, and a reconstruction of
`startup-playbook`.

## Confirmed (measured, read in a file, or seen on the bench)

### The register freeze

* **The codec disables control-register access while `AIF1CLK_ENA = 1` with a
  stopped clock source.**  Measured: `R0x200 = 0x0000` thaws it within one
  write; a software reset (`R0x0000 = 0`) thaws it; and `R0x200 = 0x0001`
  (ENA, SRC = MCLK1) from a thawed codec freezes the very next write.  Reads
  keep answering, writes return success and are dropped.  (`docs/reports/203-…`
  §1–3; datasheet Rev 4.6 "Control Interface Clocking".)
* **It is not time- or PM-driven:** after a thaw, writes still landed at
  t = 0, 5, 15, 35 and 65 s of idle (`docs/reports/203-…` §1).
* **The chip is in that state from boot**, because the DAPM `AIF1CLK` supply
  powers with the `rcS` playback path while `AIF1CLK_SRC` is still MCLK1.
  The regmap trace at a clean `pbplay 5 0` shows 28 driver writes starting
  `200=1`; hardware reads during the stream are `0x220 = 0`, `0x221 = 0`,
  `0x223 = 0`, `0x224 = 0x0c80`, `0x732 = 0x6000` while the driver's cache holds
  `0220:0001`, `0221:0700`, `0223:0800`, `0224:0c83`.

### The reference's codec state (live TabletOS)

* **FLL1 is locked on the reference**: `0x0732 = 0x6020` (bit 5 = 1),
  `0x0220 = 0x0005` (`FLL1_ENA|FLL1_FRAC`), `0x0221 = 0x0700`,
  `0x0222 = 0xae14`, `0x0223 = 0x00e0`, `0x0224 = 0x0c80`
  (`REFCLK_SRC = MCLK1`, `FRC_NCO_VAL = 0x19`) —
  `docs/reports/204-…` §2, read over the door with root ssh.
* **The reference keeps `AIF1CLK_ENA = 0` while idle**: `0x0200 = 0x0010`
  (source already FLL1, enable off), `0x0208 = 0x0010`, `0x0210/0x0211 = 0x0083`,
  `0x0300 = 0x4018`, `0x0301/0x0302 = 0x4000` (codec is the AIF1 **master**),
  `0x0303–0x0305 = 0x0040`, `0x0601/0x0602 = 0x0001`, PM1 `0x0013`,
  PM2 `0x6000`, `0x0700 = 0x8101`, `0x0701–0x0706 = 0xa101`,
  `0x0707/0x0708 = 0x4007/0x4008` (same report).
* **The codec is a WM8994 revision D** (`dmesg`: `wm8994 3-001a: WM8994
  revision D CUST_ID 00`), ID `0x8994`.

### Linux cannot reproduce the lock

* With the codec reset and `AUXCLK1 = 0x00020100`, writing the reference's exact
  FLL bytes (`0x221=0x0700`, `0x223=0x00e0`, `0x220=0x0004`, `0x222=0xae14`,
  then `0x220=0x0005`; `0x224` untouched at `0x0c80`) reads back correctly and
  leaves `0x732 = 0x6000` — **no lock** (`docs/reports/204-…` §3).
* Sweeping the `AUXCLK1` divider field over 2, 3, 4, 5, 7 (12.8 / 9.6 / 7.68 /
  6.4 / 4.8 MHz under either divider convention), with a gate off→on transition
  for each, and with PM1/PM2 bias on or the codec fully reset: **no lock** every
  time (same section).
* The datasheet's free-running FLL1 (`OSC_ENA` + `FRC_NCO` + `OUTDIV = 7`) also
  produces no lock (report 203 §5).
* **A boot can leave `AUXCLK1 = 0x00000100`** (divider 0 = ÷1 = 38.4 MHz, gate
  on) — observed once — **and `0x00020100`** (÷3 = 12.8 MHz) on a later clean
  reboot; the SCRM value survives a warm reset.  (`devmem 0x4A30A314 32`;
  `0x4A10019C = 0x41180000` → pad `0x19C` = `0x0000`, mux 0, `fref_clk2_out`.)

### The reference's own start-up binary (`startup-playbook`)

* **It programs `AUXCLK1` itself**, twice: `0x80100908` writes `0x00010000`
  (divider 1, gate off) and `0x80107980` writes `0x00020100` (source
  `sys_clkin`, ÷3 = 12.8 MHz, gate on); `0x80107998` repeats the latter
  (`docs/reference/startup-playbook/clocks-and-wdt.md` §1, instruction-level).
* **It sets WDT2 to the 58.6 s window**: `WSPR=0xAAAA`, poll `WWPS`,
  `WSPR=0x5555`, `WCLR=0`, `WLDR=0xFFE2B400`, `WTGR=0xFFFFFFFF`,
  `WCRR=0xFFE2B400`, `WSPR=0xBBBB`, `WSPR=0x4444`, polling `WWPS` throughout
  (`clocks-and-wdt.md` §3) — the same policy the kernel already applies.
* **Its GPIO table is 75 records at `0x80122750`** (32 B each): gpio 53
  in/falling signal `0x59` (WL_IRQ), **gpio 54 out, level 0** signal `0x58`
  (WL_EN), gpio 55 out/0 signal `0x5d` (BT_EN), gpio 35 in/falling signal
  `0x16` (touch IRQ); the `19`/`161`/`23`/`24`/`25`/`26` rows are
  silicon-revision-gated through `+0x14`/`+0x15` (`gpio-pins.md` §1).
* **The WLAN/BT pin pair is chosen by board type**: board type == 4 → WLAN
  151/152 or 151/163 (by a sub-variant < 4) and BT 171; otherwise **WLAN 54/53
  and BT 55**.  The strings are pre-formatted log lines selected through a
  PIC-relative table; the branch is at `0x8010219c`/`0x80102358`
  (`gpio-pins.md` §2).
* **The board/silicon identity comes from manufacturing data**
  (`Reading MFG data...`, `Unable to determine board type from productID …
  Setting to Winchester`, `Board type = %s`) with the name tables at
  `0x8012baa0`–`0x8012bb34` (`README.md`).

### Bench and tooling facts

* `pbi2c` can read a 16-bit-register device on QNX: `rd16`/`dump16`/`wr16` in
  `tools/qnx-i2c.c`; the ID read back `0x8994` on the first try
  (`docs/reports/204-…` §1).
* `devmem 0x4805B038` (GPIO5 `DATAIN`) produces `Unhandled fault: external
  abort on non-linefetch` + `L3 Custom Error … L4PER2` and taints the kernel
  (`W`); the unit stays up.  Eight reads, eight faults (trap 158).
* The IFS round trip was region 0 only: `ifs-shim-sealed.ifs` at block `0x825`
  (489 frames, `COMPLETE`), later `boot/chainloader/kernel.img` (597 frames,
  `COMPLETE`); the bootrom window and `--password 0000` worked as documented.
* `tools/pbssh.sh` logged in as root (`uid:0 euid:0`) after `door-hold.sh`
  connected; the door came up about a minute after TabletOS booted.
* **The touch did not boot after the reboot**: `playbook-munin 1-0065: touch
  did not boot: -110` at 57–58 s; `i2cdetect -y -r 1 0x65 0x65` shows `UU`,
  and `i2ctransfer -f -y 1 w1@0x65 …` returns `Remote I/O error` (NAK).
* The 1917 FLL1 decode is unchanged from session 39: `N = 7` at `[14:5]`
  (`0x00e0`), `OUTDIV = 7` at `[13:8]`, `K = 0xae14`, `FRAC` = bit 2
  (`docs/reports/201-…`).

## Inferred (evidence, and how strong)

* **Report 200's "write blocker" is fully explained by the AIF1CLK rule.**
  *Strong* — three independent measurements plus the datasheet sentence.
* **MCLK1 is clocked on this board, and the codec clock works.**  *Strong* —
  the reference's FLL is locked with `REFCLK_SRC = MCLK1` on the same board.
* **The remaining Linux difference is below the registers we can read** (pad
  drive, a supply, `/RESET`).  *Moderate* — every register compared matches (the
  codec FLL file, `AUXCLK1`, pad `0x19C`), and the same FLL bytes behave
  differently.
* **The DTS supply mapping is probably wrong.**  *Moderate* — the datasheet's
  recommended ranges are AVDD1 2.4–3.3 V, AVDD2/CPVDD 1.71–2.0 V, SPKVDD
  2.7–5.5 V; our DTS maps AVDD1 to 1.8 V and AVDD2/CPVDD/SPKVDD to 2.1 V, and
  nothing about the mapping has been verified.
* **A `.trigger(START)`-only driver change cannot work.**  *Strong* — the ASoC
  order is `.prepare` (link/component/DAI) → DAPM stream event (sets
  `AIF1CLK_ENA`) → `.trigger(START)` (starts BCLK), so the codec clock is
  enabled one step before a live reference exists (`soc-pcm.c`
  `__soc_pcm_prepare`).
* **The touch failure is the missing power/reset pulse.**  *Moderate* — the
  controller NAKs at `0x65` after a warm reboot and the driver does not drive
  GPIO1_14/GPIO4_8; it worked in session 38 only because the part was already up.
* **The reference does not raise WL_EN at boot** (the pin is configured but its
  level is 0).  *Moderate* — the table's level 0 for gpio 54, plus the 1917 WLAN
  driver's own `power(1); delay(120)`.

## Dead ends and wrong assumptions (what was believed, and what killed it)

* **"The chip stops accepting register writes after boot."**  Killed by the
  thaws: `R0x200 = 0` restored writes within one write, and the freeze followed
  the *source state*, not the boot (report 203 §1–3).
* **"The regmap cache is why the FLL config is lost."**  Killed by writing the
  FLL bytes directly over I²C: the chip drops them too while frozen
  (report 203 §3).
* **"The DAPM `BIAS_OFF` cycle is the prime suspect."**  Killed by the
  controlled test: the trigger is `AIF1CLK_ENA` with a stopped source, at boot
  and at every stream, not a bias-down event.
* **"MCLK1 is dead on this unit"** (session 39, report 203 §5).  Killed by the
  live QNX read: `0x732 = 0x6020` with `REFCLK_SRC = MCLK1` (report 204).
  Corrected to "does not clock **under our image**" (trap 151, report 203 §0).
* **"The free-running FLL will clock the codec without MCLK1."**  Killed by the
  measurement: `OSC_ENA` + `FRC_NCO` + `OUTDIV = 7` leaves `0x732 = 0x6000`, and
  selecting it as AIF1CLK freezes the map (report 203 §5).
* **"Writing the whole `0x224` register is harmless."**  Killed by the
  reference's read: it has `0x0c80` (`FRC_NCO_VAL = 0x19`) and the 1917 code
  masks to bits 4:0; our `0x224 = 0x0000` cleared the field.  Same class:
  `0x223 = 0x0008` looked "ignored" because `N` is `[14:5]` (trap 157).
* **"A single register read-back is a valid write test."**  Killed by the
  reserved-bit cases (`R0x1c` bit 7, `R0x36` bits 3:2, `R0x18` bit 0).  The
  reliable probe is `R0x601` bit 5 (`0x0020`) after a thaw (trap 159).
* **"Two early tests showed the driver's writes are dropped."**  Killed by
  realising the codec was already frozen from an earlier experiment; those tests
  proved nothing (trap 159).
* **"The chip was frozen at the first prompt because the driver freezes it
  late."**  Actually it is frozen from the boot-time DAPM power-up; the later
  writes are simply the visible consequence.
* **"`startup-playbook` does not touch AUXCLK1"** (my first conclusion, from
  finding no literal `0x4A30A314` in the file).  Killed by decoding the
  `movw`/`movt` pairs: the write is there at `0x80107980` (`0x00020100`) and
  `0x80100908` (`0x00010000`).
* **"The `WLAN_EN_GPIO=151` pairs are the missing Winchester pins."**  Killed
  by the selection code: board type 4 selects them; Winchester is not type 4, so
  **54/53 (and BT 55)** is the correct pair — nothing was missing.
* **"Loading/unloading the sound modules triggers the freeze."**  Killed by
  testing across a module reload: the state followed the AIF1CLK enable, not the
  module load.
* **"`pkexec` needs the operator each boot."**  The operator corrected this: it
  is ours to run; done twice this session without a prompt.

## Unknown / open questions

* **Why does the FLL not lock under Linux with identical register values?**
  *Would settle it:* measure pad `0x19C`/the codec's MCLK1 pin and
  DCVDD/DBVDD/AVDD1/AVDD2/CPVDD/`/RESET`, QNX vs Linux on the same points.  Or
  prove the reference applies a pad configuration we do not (the `hwio` pad
  resolver).
* **Which rails actually feed the codec's AVDD1/AVDD2/CPVDD/SPKVDD?**
  *Would settle it:* a board view, or the TWL register state on both OSes at the
  same time.
* **What is the board-type enum value used by `startup-playbook`, and what does
  the MFG record contain?**  *Would settle it:* the caller of `Board type = %s`
  (`0x8012ba8c`) and the MFG read path.
* **Does the start-up apply the signal-group pad table itself, or only select it
  for `hwio-omap4.so`?**  *Would settle it:* cross-referencing the table's fields
  with the `hwio` pad-id resolver (report 189 §2.1) and the live pad values.
* **What is `R0x220` bit 2, and what does `0x4A30A104 = 0x00160016` mean?**
  *Would settle it:* a later datasheet revision, or a scope on the FLL during
  the reference's start-up.
* **Why does pad `0x19E` read `0x4100` on QNX and `0x4118` on Linux?**
  *Would settle it:* identify the pad (a different signal from `0x19C`) and
  decide whether it matters.
* **Can the touch be recovered without a battery pull?**  *Would settle it:* a
  GPIO1_14/GPIO4_8 pulse from Linux (the driver does not do it), or a cold boot.
* **Item 9 step 4 (WL1283)** remains chip-side/software-exhausted (report 196);
  unchanged by this session.

## Superseded / uncertain

* **"MCLK1 does not clock on this unit"** (report 200, report 203 §5, trap 151
  as written in session 39).  Evidence then: FLL1 never locked from MCLK1 on
  Linux.  Now: **report 204 is current** — the reference locks FLL1 from MCLK1,
  so the statement is only true for our image.  Trap 151 was corrected in place;
  report 203 carries a pointer to 204.
* **"The register write path itself is broken or blocked."**  Superseded by
  report 203: it is the documented AIF1CLK rule; the bus and the register file
  are fine.
* **"`startup-playbook` … AUXCLK1 = 0x00020100 (0x7980)"** as an unverified
  session-39 note — now **confirmed** at instruction level (not superseded).
* **Uncertain:** the `5` and `7` written to `PRM 0x4A306110` (`0x80100884`,
  `0x8010786c`) — not decoded in this session.

## Artefacts produced

* **Reports:** `docs/reports/201-qnx-wm8994-clocking-re-2026-11-11.md`
  (static RE of `deva-ctrl-omap4abe-wm8994.so`),
  `docs/reports/203-wm8994-write-freeze-2026-11-11.md` (the freeze measurement),
  `docs/reports/204-qnx-live-codec-read-2026-11-11.md` (the live reference read
  and the comparison).
* **New documentation folder:** `docs/reference/startup-playbook/`
  (`README.md`, `gpio-pins.md`, `pads.md`, `clocks-and-wdt.md`, `mmio-map.md`).
* **Tool:** `tools/qnx-i2c.c` extended with `rd16`/`wr16`/`dump16`; the tracked
  binary `tools/pbi2c` rebuilt (10 429 → 11 301 bytes).
* **Docs updated:** `docs/README.md` (new `reference/` section),
  `docs/project/STATE.md`, `HANDOFF.md`, `KNOWN-ISSUES.md` (traps 155–160),
  `DECISIONS.md` (§50, §51), `ROADMAP.md` (item 9 step 5), this note.
* **No kernel, rootfs or image change.**  The kernel tree and the patch are
  untouched (53 files / 7 773 lines, sha256 `434d06c3…`); region 0 stays
  `0xF97CD9C7`, region 1 `968d2f20…`.
* **Commits:** `e0bf37d` (the freeze diagnosis), `5317143` (the QNX read +
  `pbi2c` + reports 201/203/204), `f6034be` (the AUXCLK1 nondeterminism
  correction), `5e35d30` (the `startup-playbook` folder), `8a31544` (the pad
  table), and this wrap-up.
