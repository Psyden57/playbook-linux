# 213 — ABE feasibility: the serial port is a firmware data path; the McBSP clocks never needed it (session 46)

Date: 2026-11-13 (session 46, Linux-era 40).
Bench: **no bench change**.  The unit is on our Linux image **`0x58F85337`**
(region 0, `0x939A6C` B, kernel `7.3.3` **#40**; console `pb> `), region 1 =
the audio/OSS rootfs `6d92428e…`.  `BOOTCTR_FCTRY` read **`0x20`** from the
boot log (`pbrpmb: topup: 0x20 >= threshold 0x1e, no write`).  No flash, no
NVRAM/MCT/RPMB write.  The work is static: the TI TRM, the **ABE/HAL
Addendum** (new in hand this session), and the shipped 1917 HAL and audio
driver.

## 0. Headline

**An ABE serial port cannot be used without the Audio Engine firmware — but
the McBSP's clocks never needed the ABE.**  The session-45 premise ("the
reference clocks its McBSP through the ABE, mainline has no such path, so the
next step is an ABE bring-up") does not survive the documents and the shipped
binaries.  Four independent facts settle it:

1. **`abe_connect_serial_port` is a data-path call, not a clock call.**  It
   writes the AE's software port table and an ATC descriptor in ABE memory
   and touches no McBSP or PRCM register.  The AE firmware consumes that
   table; the ATC moves samples between the McBSP FIFO and DMEM.  Without
   `abe_load_fw` there is no consumer.
2. **The McBSP clock path is standard hardware and is already in mainline.**
   The SRG divides `CLKS` (the CM1_ABE functional clock `MCBSPx_FCLK`, or the
   `abe_clks` pad) or the interface clock `ABE_ICLK2`, or takes an external
   `CLKX`/`CLKR`; `omap-mcbsp` and `drivers/clk/ti/clk-44xx.c` program every
   one of those options.  The TRM calls the direct peripheral access
   "legacy mode" when the AE is bypassed; that is exactly what mainline does
   and what E5 ran at 48 096 frames/s.
3. **The reference's playback port is a clock *slave* of the codec.**  Its
   codec is AIF1 master (`R0x302 = 0x4000`), all four McBSP clock pads are
   inputs, and the driver's slave path sets only `XRST`/`RRST` — the SRG is
   never started.  `StereoAudioCodecRequestMcbspClk` and
   `StereoAudioCodecRequestABEClk` are **stubs** (`mov r0, #1; bx lr`) in the
   shipped 1917 driver.
4. **The HAL Addendum recommends the opposite of an ABE clock.**  For a codec
   needing 256×Fs it says to use a McBSP as the clock source, *clocked from
   ABE_CLK*, and the start sequence is "connect the AE port, enable the ATC,
   then **start the serial port from its own device driver**".

**Therefore: no ABE bring-up is required for audio.**  The mainline
`omap-mcbsp` + sDMA legacy path is the right architecture, and the remaining
fault is the board-level data/clock pin configuration — most likely the
McBSP1 clock pads' **input buffer** in the E9 build (the committed group is
`0x0000`, while the reference's live pads are `0x0100`).

## 1. Sources

* **`hardware/OMAP4430_4460_4470_ABE_HAL_Addendum.pdf`** (gitignored), TI
  literature **SWPU245F**, Version F, November 2011 — the "HAL Addendum"
  that the public TRM's chapter 12 defers to.  The operator supplied the URL
  (`ti.com/pdfs/wtbu/OMAP4430_4460_4470_PUBLIC_TRM_Addendum_ABE_HAL_vF.pdf`);
  text at `hardware/abe-hal-addendum.txt`.  It is written around the TI
  reference codec (TWL6040, PDM), but the ATC/HAL APIs are codec-agnostic;
  the PlayBook's codec is a serial port, so §§3.2.7 and 4.13.1 apply
  directly.  (TWL6040 is the audio codec of TI's reference design; TWL6030 is
  this board's PMIC — the two are unrelated.)
* `hardware/swpu231ap.pdf.txt` (the OMAP4430 TRM): chapter 12 (ABE), §23.5.4.3
  (McBSP clocks), §23.5.4.9 (SRG), Tables 3-38/3-1062 (CM1_ABE clock muxes).
* `local/work/1917/base/lib/libabe.so.1` (sha256 `1a7762cd…`, 157 892 B) —
  the shipped ABE HAL, decompiled with REA/Ghidra.
* `local/work/1917/base/lib/dll/deva-ctrl-omap4abe-wm8994.so`
  (sha256 `2b346de3…`) — the shipped 1917 audio driver.
* `docs/captures/wm8994-reference-playing-2026-11-11.txt` and
  `docs/captures/wm8994-e5-master-srg-2026-11-13.txt` — the register values.

## 2. What the ABE is, and where the firmware sits

The TRM's chapter 12 is a subsystem overview; the engine detail lives in the
HAL Addendum.  The division of labour is explicit:

* **`ABE = AESS (AE + ATC + memories) + timers + audio peripherals`**
  (McBSP1–3, McASP, DMIC, McPDM, SLIMbus).  Addendum §2.1.1.
* The **AE** executes a processing list that the HAL builds; the AESS wakes
  it on a 96 kHz event.  "The HAL is in charge of the content of this list"
  (§2.1.1).
* The **ATC** is a descriptor-driven data mover: "The ATC decodes its
  corresponding buffer descriptor in DMEM … and executes the transfers"
  (§2.1.2).
* **Physical ports** are the peripherals (DMA, DMIC, McPDM, McBSP, …);
  **AE ports** are the firmware's I/O connections; a **link** connects them
  (List of Abbreviations).  `abe_connect_serial_port` makes that link.
* The TRM adds the crucial escape hatch: "Each peripheral in the ABE can be
  accessed directly by the initiator when the AE is bypassed (**legacy mode**,
  for old software compatibility)."  (Chapter 12 introduction, p. 3007.)

Mainline runs the peripherals in that legacy mode: sDMA ↔ McBSP FIFO, with
no AE port and no ABE firmware.  The peripherals are the same hardware; only
the data mover between the FIFO and memory differs.

## 3. The serial-port connection is a firmware data path

`abe_connect_serial_port(id, format, mcbsp_id)` in the shipped HAL
(VA `0x4a4c`) does exactly this:

```c
memcpy(abe_port + id*0x68, abe_port_init + id*0x68, 0x68);
port[+0x04] = format[0];
port[+0x08] = format[1];
port[+0x1c] = 2;                 /* link type */
port[+0x20] = mcbsp_id << 3;     /* ATC descriptor index */
abe_read_hardware_configuration(...);
port[+0x2c] = abe_dma_port_iter_factor(...);
abe_init_io_tasks(id, ...);
port[+0x00] = 3;                 /* serial-port class */
abe_init_atc(id);
```

* It writes the **software** `abe_port` table (BSS at `0x2ac4c`) and calls
  `abe_init_io_tasks` / `abe_init_atc`.  Those build descriptors in **ABE
  memory** through `abe_block_copy` (which stores into
  `io_base + {0x80000, 0xa0000, 0xc0000, 0xe0000, 0xf1000}`).
* **`abe_init_atc`'s serial-port case is a no-op.**  The `switch` on the port
  class handles DMA/CBPr, ping-pong and IRQ ports; `case 3` is `break`.  The
  serial path is executed by the **AE firmware's I/O tasks**, not by a
  hardware register the HAL writes.
* **`abe_load_fw`** (`0x59d0`) calls `abe_load_fw_param(abe_firmware_array)`,
  which copies the firmware image into the ABE banks.  There is no
  equivalent "connect without firmware".
* The HAL Addendum's start/stop sequence (§4.13.1) is explicit: "The AE
  logical port is programmed with `abe_connect_serial_port()` … 1) start
  AESS ports [enable_data_transfer], 2) wait 250 µs, 3) **start the serial
  ports: call the device driver of … McBSP1/2/3**."

So the ABE serial port is a **firmware-mediated data path**.  It is not a
clock generator, and it cannot be brought up by programming a McBSP from
Linux.  The question "can an ABE serial port be pointed at a McBSP without
the DSP firmware" therefore answers **no** for the data path.

**But the data path is optional.**  The TRM's legacy mode is the same McBSP
FIFO driven by sDMA; it needs no AE, no firmware and no `mcbsp_id` binding.
That is the path `omap-mcbsp` implements and the path E5 exercised.

## 4. The McBSP clocks are not part of that path

The TRM's McBSP clocking is self-contained (chapter 23):

* **Functional clock / CLKS:** "The CLKS signal of McBSP1 is linked to an
  internal clock (`MCBSP1_FCLK`) provided by the PRCM module.  The CLKS
  signal can also be linked to an external signal through the **`abe_clks`**
  pin."  (§23.5.4.3.1; the same text for McBSP2/3, with `CLKS` or `CLKX` as
  the functional-clock source.)
* **The mux:** `CM1_ABE_MCBSPx_CLKCTRL[25:24]` selects internal / `CLKS` pad
  / SLIMBUS pad; `[27:26]` selects the internal source (24 MHz from
  DPLL_ABE / `ABE_SYSCLK` / 24 MHz from DPLL_PER).  (Table 3-38 and the
  register description, Tables 3-1062/3-1064.)
* **The SRG:** `CLKSRG` is selected by `PCR[7] SCLKME` and `SRGR2[13]
  CLKSM` from the interface clock (`McBSPi_ICLK` = `ABE_ICLK2`), `CLKS`
  (PRCM functional clock or `abe_clks`), or an external `CLKX`/`CLKR`.
  (§23.5.4.9, Figure 23-144.)  `CLKSM = 1` is the interface clock — the
  49.152 MHz `ocp_abe_iclk` session 45 measured — and `CLKSM = 0` with
  `SCLKME = 0` is `CLKS`, whose PRCM source is the 12.288 MHz
  `abe_24m_fclk` (or the pad, if selected).
* **Mainline already programs all of it:** `omap-mcbsp.c`'s
  `omap_mcbsp_dai_set_dai_sysclk` maps `OMAP_MCBSP_SYSCLK_CLK` → `CLKSM = 1`,
  `OMAP_MCBSP_SYSCLK_CLKS_FCLK` → `CLKSM = 0` + `prcm_fck`, and
  `OMAP_MCBSP_SYSCLK_CLKS_EXT` → `CLKSM = 0` + `pad_fck`; `clk-44xx.c`
  aliases `prcm_fck` to the internal `abe-clkctrl` mux and `pad_fck` to
  `pad_clks_ck`.  None of this is an AE/HAL operation.
* **The HAL Addendum agrees:** "When OMAP is connected to an other audio
  Codec needing a 256xFs input main clock, the recommendation is to use one
  McBSP delivering the 256xFs.  The McBSP will be clocked from ABE_CLK
  (24.576MHz = 512xFs)."  (§3.9.)  That is the CM1_ABE clock manager, not the
  DSP firmware.

The distinction that matters: **ABE_CLK / DPLL_ABE / CM1_ABE are PRCM clock
hardware** (mainline's clock framework), while **AE / ATC / DMEM are the
firmware engine** (the HAL).  Session 45's "ABE clock" and "ABE firmware"
were conflated.

## 5. What the reference actually does

Read from the shipped driver and the live captures:

* **The codec is the AIF1 master:** `R0x302 = 0x4000` (AIF1_MSTR = 1) on the
  idle and the playing reference; the DAC is fed from AIF1
  (`R0x601/0x602 = 1` = AIF1DAC1), and AIF1 is in DSP_A (`R0x300 = 0xc018`).
  **AIF2 is a slave** (`R0x304 = 0x0040`, `AIF2_MSTR` bit 14 clear), so it
  does not generate the clock either.
* **The McBSP clock pads are inputs on the reference while it plays:**
  `abe_mcbsp2_clkx` (pad `0x0f6`) = `0x0100`, `abe_mcbsp2_fsx` (`0x0fc`) =
  `0x0100`, `abe_mcbsp1_clkx` (`0x0fe`) = `0x0100`, `abe_mcbsp1_fsx`
  (`0x104`) = `0x0100`; the data is `abe_mcbsp1_dx` (`0x102`) = `0x0000`
  output and `abe_mcbsp1_dr` (`0x100`) = `0x1108` input.  Report 212 §10.
* **The driver's slave path never starts the SRG:**
  `mcbsp_update_port_clock_rate` (VA `0x25158`) clears `SPCR2` bits
  `0x80`/`0x40`/`0x1` and `SPCR1[0]`, writes `SRGR2 = (FPER-1) | 0x1000`
  (`FSGM = 1`, **`CLKSM = 0`**), then sets only `SPCR2 |= 1` (`XRST`) and
  `SPCR1 |= 1` (`RRST`).  In master mode it additionally sets `GRST` and
  `FRST`; the reference's port is not in that mode.  So the bit/frame clocks
  arrive on `CLKX`/`FSX` from the codec.
* **The clock-request functions are stubs:**
  `StereoAudioCodecRequestMcbspClk` (VA `0x6e94`) and
  `StereoAudioCodecRequestABEClk` (`0x6e9c`) are each `mov r0, #1; bx lr`.
  There is no ABE clock request in this driver build.
* **The functional-clock source is a per-port table field, not the AE:**
  `mcbsp_poweron_port_int` (VA `0x25608`) sets
  `CM1_ABE_MCBSPx_CLKCTRL[1:0] = 2`, clears `[27:26]` (internal source =
  24 MHz from DPLL_ABE), then writes `[25:24] = port[0x120]` (0 = internal,
  1 = `CLKS` pad).  That is the same CM1_ABE mux mainline drives.  The
  `mcbsp_id`/`port[0x120]` values are per-board table data, which is why they
  must be read live (the still-open step 2).
* **`mcbsp_initialize` (VA `0x25848`)** mmaps `0x49022000`/`0x49024000`/
  `0x49026000` and the CM1_ABE clock-control register; it never touches the
  ABE engine.

So the reference's working playback is a **codec-master I2S/DSP_A link**:
the codec drives BCLK/LRCLK, the McBSP is a consumer, and the four clock
pads' input configuration is consistent with the same BCLK/LRCLK net feeding
both McBSP engines.  The ABE only mixes/routes inside the SoC; it is not in
the pin-level clock or data path.

## 6. E9, re-read

E9 was built on the session-45 reading ("McBSP1 is clocked through the ABE",
"McBSP1's clock pins are not on McBSP2's nets").  With the documents in hand,
that reading is unnecessary, and the E9 experiment has a cheaper explanation:

* The committed board tree's `mcbsp1_pins` group is
  `clkx = 0x0000`, `dr = 0x0100`, `dx = 0x0000`, `fsx = 0x0000`.  A **clock
  consumer** needs the pad's **input buffer enabled** — the reference's live
  values are `0x0100`.  With `0x0000` the McBSP1 `CLKX`/`FSX` inputs are
  disabled, so no frame sync can reach the shifter even if the net is
  driven.  That is exactly the observed `SPCR2 = 0x0235` (`XRDY = 0`,
  `XEMPTY = 1`).
* The E9 build's pad values were not recorded in the report or the note, and
  its source was not committed (the tree is the E5 restore), so this is the
  first thing to check, not a proven cause.  But it is a **one-field fix**
  and it must be right before any conclusion about the nets.
* E6's "McBSP1 as clock master does not lock the codec" remains a real
  observation, but it is a different direction (McBSP1 output → codec
  input) and may itself have been confounded by the codec still being in
  master mode (`R0x302 = 0x4000`) or by the pad output/input direction.  It
  does not establish that the *input* net is separate.

## 7. The next experiment (cheap, one boot, no oracle)

**A corrected E9 — the same two links, with the consumer's pads actually
configured as inputs:**

1. Link 1 unchanged from E5: `mcbsp2` + `wm8994-aif1`, `CBC_CFC`, McBSP2 the
   bit/frame provider, codec AIF1 slave and FLL1 locked from BCLK.
2. Link 2: `mcbsp1` + a codec DAI that negotiates S16 (the E9 choice,
   `wm8994-aif2`, is fine as an ASoC stand-in; the physical data still goes
   out `abe_mcbsp1_dx` into the codec's AIF1 DACDAT), link `CBP_CFP` so the
   CPU DAI is a consumer, and the second PCM opened at the card's rate
   (traps 195/196).
3. **Change `mcbsp1_pins` `clkx`/`fsx` from `0x0000` to `0x0100`** (input
   enable), matching the reference; leave `dr = 0x0100`, `dx = 0x0000`.
4. Judge it the way trap 197 prescribes: a **stream**, not `XRDY`.  If
   McBSP1's `SPCR2` leaves `0x0235` and `XBUFFSTAT` toggles (or the write
   survives past five periods), the net is shared and the data path is
   complete.  If it still stalls, the net is genuinely separate and the
   remaining route is the codec-as-master (MCLK1) or an external clock.

This is a region-0 change (DTS) plus a region-1 change (the machine
driver), i.e. the `both.lst` recipe; the module-only part keeps the CRC
(trap 190).  It is the decisive test the ABE theory pre-empted.

**Only if that fails** is a live oracle measurement needed: boot the oracle,
play, and read the codec's `R0x300`–`R0x307` and the four pad registers, and
optionally probe the net.  The static part already tells us what to expect
(all four pads input; AIF1 master; AIF2 slave).

## 8. What this changes

* **The ABE bring-up project is closed as unnecessary.**  Do not port
  `libabe`, do not load `abe_firmware_array`, do not implement an
  `abe_connect_serial_port` equivalent.  The firmware blob and HAL remain in
  hand if a future low-power/mixing use-case ever wants the AE, but that is
  a separate, optional feature, not the audio path.
* **The ABE/`omap-abe-twl6040` absence is not the blocker.**  Mainline's
  legacy `omap-mcbsp` is the correct path; E5's 48 kHz transport is the
  proof.
* **The remaining fault is board-level:** which clock pads the codec drives,
  and the pad input-enable/direction of the consumer.  The E9 stall is
  consistent with the `0x0000` consumer pads.
* **The `abe_clks` pad is not required.**  It is only one selectable source
  of `CLKS`; the internal `MCBSPx_FCLK` (12.288 MHz at the current
  DPLL_ABE rate) is the other, and mainline can select either.
* **A useful alternative clocking mode is now on the record:** `CLKSM = 0`
  + `OMAP_MCBSP_SYSCLK_CLKS_FCLK` divides the 12.288 MHz functional clock
  (`CLKGDV = 8` for 48 kHz), which is the reference's `CLKSM = 0` mode and
  avoids the 49.152 MHz interface-clock division of trap 188.  E5's
  `CLKSM = 1`/`CLKGDV = 32` remains valid; this is a second option to test
  if the clock path ever needs simplification.

## 9. Evidence

* `hardware/abe-hal-addendum.txt` — the extracted SWPU245F text; the
  gitignored PDF is `hardware/OMAP4430_4460_4470_ABE_HAL_Addendum.pdf`.
* `docs/captures/wm8994-reference-playing-2026-11-11.txt` — `R0x300/0x302/
  0x304/0x601/0x602/0x300`.
* `docs/captures/wm8994-e5-master-srg-2026-11-13.txt` — the E5 clock tree and
  the 48 096/s measurement.
* REA/Ghidra records: `abe_connect_serial_port` (`0x4a4c`), `abe_init_atc`
  (`0x8a34`), `abe_load_fw` (`0x59d0`), `abe_load_fw_param` (`0x3574`),
  `abe_block_copy` (`0x9714`) in `libabe.so.1`;
  `mcbsp_update_port_clock_rate` (`0x25158`),
  `mcbsp_poweron_port_int` (`0x25608`), `mcbsp_initialize` (`0x25848`),
  `StereoAudioCodecRequestMcbspClk` (`0x6e94`) in
  `deva-ctrl-omap4abe-wm8994.so`.
* TRM: chapter 12 introduction (legacy mode), §23.5.4.3, §23.5.4.9,
  Tables 3-38 / 3-1062 / 3-1064.
* Bench: `uname -a` = `7.3.3 #40`; `/init` log `0x20 >= threshold 0x1e, no
  write`.

**Later in the same session** the bench work (report 214) changed the DTS and
the two drivers; the patch was re-exported to 54 files / 8,440 lines, sha256
`fd2774fb…`.  This report's "no source changed" statements describe the
feasibility phase only.
