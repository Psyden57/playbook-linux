# Session 42 (absolute) — E1: clock roles inverted; the transmit path proven; MCLK1 is the one blocker

> Numbering: absolute `42` (see `NUMBERING.md`).  This is Linux-era **session
> 36**, and report 206 is the technical record.

Date / span: 2026-11-12 (single-day session).
Unit state at the end: **on our Linux image `0x36597040`** in region 0, region
1 = `b20302dc…`; the codec was left quiesced; `BOOTCTR_FCTRY` was not read
(the chroot has no `/dev/rpmb`; the initramfs `/init` top-up ran as designed).

## Goal of this session

Run the handoff's cheapest experiment, **E1**: make the McBSP a clock
consumer and the codec the AIF1 master — the reference's clock roles — on
both sides at once, and read back `SPCR2` to see whether the transmit FIFO
finally drains.

## Confirmed (measured, read in a file, or seen on the bench)

* **E1 landed and is verified.**  After the two-region flash
  (region 0 `0x36597040`, region 1 `b20302dc…`): `PCR0 = 0x000f`
  (`CLKXM`/`FSXM` clear — consumer mode; was `0x0f0f`), `SPCR2 = 0x0235`
  (`GRST`/`FRST` never set — the SRG is out of the path; was `0x02f5`), codec
  `R0x302 = 0x4000` (AIF1 master, byte-identical to the reference), pads
  `clkx`/`fsx` = `0x0100` inputs (= the reference's live values).
* **The FLL1 register file is byte-identical to the working reference**
  (`0x220 = 0x0005`, `0x221 = 0x0700`, `0x222 = 0xAE14`, `0x223 = 0x00E0`,
  `0x224 = 0x0C80`) **and still does not lock** (`R0x732 = 0x6000`).
  Reproduced with the reference's own enable timing (50/100 ms delays) and
  with 2–3 s settles across repeated enable cycles.
* **`AUXCLK1 = 0x00020100` and pad `0x19C` low = `0x0000`** re-read this boot:
  the DTS fix holds and the pad is right.
* **The MCLK1 pin does not clock.**  With the FLL bypassed and `AIF1CLK`
  sourced directly from MCLK1 (`R0x200 = 0x0001`, no FLL), the stream dies
  exactly like the FLL case.  This is the new discriminator that moves the
  fault from "the FLL is unreliable" (report 205 §5h) to "the clock is not at
  the pin".
* **The transmit path works in consumer mode.**  With the free-running NCO
  forced mid-stream (datasheet Rev 4.6 p. 206 recipe: `OSC_ENA = 1`,
  `OUTDIV = ÷8`, `FRC_NCO = 1`, `FRC_NCO_VAL = 0x19` ≈ 12 MHz — `0x19` is the
  register's own default), `pbplay 20 440` ran the full 20 seconds and exited
  cleanly.  Every previous run died with `Input/output error` after five
  4096-frame periods (~1 s).  pbplay's survival is the transmit-path proof
  (the OSS write only keeps succeeding when the ring drains).
* **The kernel patch was re-exported**: 8 095 lines / 54 files, sha256
  `55035aac…`, verified 0 rejects / 54/54 byte-identical (driven from the
  session-41 file list; no new sources).
* **Touch survived this boot** (warm reset keeps the Munin controller up).
  Consistent with the handoff §0d: only cold boots need the GPIO1_14/GPIO4_8
  pulse.
* **At wrap-up the unit answered on the console with kernel 7.3.3**
  (live `uname -r` read, `ttyACM0` present), still on image `0x36597040` with
  the codec quiesced — the bench state the next session inherits.

## Inferred (reasoning from evidence, not directly measured)

* **The session-41 XRDY stall is a property of the McBSP-master/SRG
  configuration.**  *Strong* — the same hardware streams continuously once the
  SRG is out of the path and the shifter is externally clocked; the sDMA ran
  at exactly the right rate in both configurations.
* **The remaining blocker is singular: a live AIF1CLK.**  *Strong* — the codec
  chain is proven end to end; only its clock is missing.
* **What remains on MCLK1 is below the registers.**  *Strong, with a caveat* —
  every readable SoC and codec value matches the working reference, and the
  ÷1…÷8 sweep (session 41) plus today's direct-MCLK1 test bracket it.  The
  caveat: session 41's one successful MCLK1 lock contradicts "always dead", so
  a marginal/boot-dependent clock is possible.

## Dead ends and wrong assumptions

* **"Inject the NCO after the stream starts and it will just work."**  The
  first mid-stream injection half-landed (`R0x224` stuck, `R0x220` did not) —
  the codec's freeze rule applies to raw `i2ctransfer` writes too (trap 167),
  and the driver's next `set_pll()` clears `FRC_NCO` anyway (trap 168).  The
  controlled version (thaw → configure → re-enable AIF1CLK) is what worked.
* **"`for r in 732 …; $((r>>8))` reads register 0x732."**  It reads 0x2DC:
  the shell treats `732` as decimal.  Every register read in that loop was of
  the wrong address (trap 166).  The "all registers read zero" scare was this.
* **The AIF1CLK direct-from-MCLK1 test is out of spec (12.8 > 12.5 MHz).**
  Deliberate, single-boot, and recorded as a test, not a configuration.

## Unknown / open questions

* **Is MCLK1 dead at the pad, or marginal?**  *Would settle it:* a
  scope/meter on `fref_clk2_out`/MCLK1 — blocked by trap 165 (no probe, no
  boardview, no pad knowledge).  Session 41's single lock says "marginal" is
  still live.
* **Why does the SRG not drive the shifter in master mode?**  *Would settle
  it:* SRG register permutations (one per boot) with the steady-state dump,
  or E2's capture stream.
* **`abe-clkctrl:0030:0: failed to disable`** appeared once at stream teardown
  (348 s).  Benign-looking, unexplained.
* **What is the reference's own mid-stream McBSP configuration?**  Still
  unread (E3), but no longer decisive.

## Next steps (operator's call)

1. **MCLK1 at the hardware layer** — needs a probe; blocked by trap 165.
2. **A non-reference AIF1CLK design** — any live clock routed to the codec;
   no reference precedent.
3. **The master-mode SRG stall** — now a much narrower question (SRG →
   shifter distribution), attackable one register permutation per boot with
   the steady-state dump and/or E2's capture stream.
4. **Touch cold-boot power/reset (GPIO1_14/GPIO4_8)** and the
   `wl128x-nvs.bin` path are unchanged backlog items.

## Superseded / uncertain

* **"The McBSP transmit path on this silicon stalls after start"** (report 205
  §5l/§5o, session 41 — "the sDMA feed and the buffer→shifter copy both stop").
  The measurements of 205 remain valid, but the implied "this silicon's
  transmit path is suspect" is superseded: report 206 shows the same hardware
  streaming 20 s continuously in consumer mode.  The stall is a property of
  the **master/SRG configuration**.
* **"The FLL lock from MCLK1 is merely unreliable/intermittent"** (session 41
  §5h).  Superseded in precision by session 42: AIF1CLK sourced directly from
  MCLK1 with no FLL is also dead, which points at the pin, not the FLL — but
  session 41's one measured lock stands, so the question is open in
  `docs/contradictions/mclk1-locks-vs-not-under-linux.md`, not resolved.
* **"E1 = `SND_SOC_DAIFMT_BC_FC` + `set_sysclk(cpu_dai, …, CLOCK_IN)`"**
  (session-41 handoff §0c).  Superseded by the implementation: the link format
  is `CBP_CFP` (codec perspective; the ASoC core flips it for the CPU DAI, so
  `omap-mcbsp`'s `set_dai_fmt` takes its own `BC_FC` branch), and the CPU-side
  `set_sysclk`/`set_clkdiv` calls are dropped entirely — with the SRG out of
  the path there is nothing for them to configure.
* **"The driver keeps the BCLK-referenced FLL"** (DECISIONS §52, session 41).
  Superseded for the current design by DECISIONS §53: with the codec as AIF1
  master a BCLK-referenced FLL is circular, so `hw_params` locks FLL1 from
  MCLK1 — which is exactly why the MCLK1 question is now load-bearing.
* **"The patch was not re-exported — trap 161"** (the session-42 handoff this
  session started from).  Superseded at this session's wrap-up: re-exported
  and verified (`55035aac…`).

## Artefacts produced

* **Report 206** — `docs/reports/206-wm8994-e1-clock-roles-2026-11-12.md`.
* **Board DTS** — `omap4-winchester.dts`: `mcbsp2_pins` `clkx`/`fsx` →
  `PIN_INPUT` (consumer-mode pads), comments updated to the codec-as-master
  design.
* **Machine driver** — `sound/soc/ti/playbook-wm8994.c`: `CBP_CFP` link
  format, FLL1 from MCLK1, SRG calls dropped, unused defines removed.
* **Images** — region 0 **`0x36597040`** (kernel 7.3.3, `0x93A2EC`); region 1
  `local/images/rootfs-region1-audio-oss.ext4`, sha256 `b20302dc…` (27
  modules).
* **Kernel patch re-exported** — `kernel/patches/
  0001-playbook-winchester-7.3.3.patch`, 8 095 lines / 54 files, sha256
  `55035aac…`, verified 0 rejects / 54/54 byte-identical.
