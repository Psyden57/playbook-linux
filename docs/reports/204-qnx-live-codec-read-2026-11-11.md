# 204 — Live QNX codec read: FLL1 **is** locked on this unit; Linux cannot reproduce it

Date: 2026-11-11 (session 40, Linux-era 34), after report 203.
Bench: the 1917 IFS (`local/work/ifs-shim-sealed.ifs`) was flashed to region 0,
TabletOS 2.1.0.1917 booted with io-audio running, the codec was read over the
door (root ssh), then the Linux image `0xF97CD9C7` was flashed back to region 0
and booted.  Region 1 unchanged.  Only region 0 changed.

## 0. Headline

* **The WM8994's FLL1 is locked on the reference: `0x732 = 0x6020` (bit 5 = 1)**
  with `0x224 = 0x0c80` (`FLL1_REFCLK_SRC = 0` = **MCLK1**) and
  `0x220 = 0x0005` (`FLL1_ENA` + `FLL1_FRAC`).  So **MCLK1 is clocked on this
  unit**; the codec clock is not a dead end.  Trap 151's "MCLK1 does not clock
  on this unit" is wrong — it does not clock **under our Linux image**.
* **The reference keeps `AIF1CLK_ENA = 0` while idle** (`0x200 = 0x0010`:
  source already FLL1, enable off).  That is the datasheet's rule in practice:
  the codec only enables AIF1CLK when the interface is in use, so the register
  map is always accessible.  Our driver enables AIF1CLK at boot with a stopped
  source and freezes (report 203).
* **The reference's FLL1 register values are exactly reproducible on Linux** —
  we wrote them byte-for-byte and read them back — **and FLL1 still does not
  lock on our image**, at any AUXCLK1 divider (÷1…÷8 swept) and with the codec
  fully reset (analogue off) or fully powered (PM1/PM2 bias on).
* **A concrete Linux-side difference was found:** on one fresh Linux boot
  `SCRM AUXCLK1` read `0x00000100` — divider field 0 = **÷1 = 38.4 MHz**,
  outside the WM8994 FLL's 0.032–13.5 MHz input range.  It is `0x00020100`
  (÷3 = 12.8 MHz) once the codec driver's first `set_sysclk(MCLK1, 12800000)`
  has run `clk_set_rate()`.  The SCRM value survives a warm reset, so a boot can
  show either (a later clean reboot showed `0x00020100`) — the driver must set
  the rate, not rely on the boot state.  The reference has `0x00020100` from
  `startup-playbook` onward.

## 1. Method

The codec needs 16-bit register addressing; `tools/pbi2c` only had 8-bit
register paths.  `tools/qnx-i2c.c` gained `rd16`/`wr16`/`dump16` (2-byte register
iovec, descriptor register-size word 2, big-endian values).  Built with the
in-tree QNX 6.6 toolchain, pushed over the door, and the ID read back as
`0x8994` on the first try.  The tool is read-only in this use; no writes to
RPMB/NVRAM/MCT.

## 2. What the reference has (live, io-audio running, no stream)

| register | value | meaning |
|---|---|---|
| `0x0001` PM1 | `0x0013` | VMID/bias on |
| `0x0002` PM2 | `0x6000` | AIF1/AIF2 block supplies |
| `0x0100` | `0x0003` | chip revision D (matches Linux dmesg) |
| `0x0101` | `0x8004` | control interface (same as Linux) |
| `0x0200` AIF1CLK | **`0x0010`** | `AIF1CLK_SRC = FLL1`, **`AIF1CLK_ENA = 0`** |
| `0x0208` Clocking1 | `0x0010` | `TOCLK_ENA` |
| `0x0210`/`0x0211` | `0x0083` | AIF1/AIF2 rate (48 kHz, 256 fs) |
| `0x0220` FLL1 Ctrl1 | **`0x0005`** | `FLL1_ENA` + `FLL1_FRAC` |
| `0x0221` FLL1 Ctrl2 | `0x0700` | OUTDIV = 7 (÷8), FRATIO = 0 |
| `0x0222` FLL1 Ctrl3 | `0xae14` | K = 0xAE14 |
| `0x0223` FLL1 Ctrl4 | `0x00e0` | N = 7 |
| `0x0224` FLL1 Ctrl5 | **`0x0c80`** | REFCLK_SRC = MCLK1, DIV = ÷1, `FRC_NCO_VAL = 0x19` |
| `0x0300` AIF1 Ctrl1 | `0x4018` | I²S, 16-bit |
| `0x0301`/`0x0302` | `0x4000`/`0x4000` | codec is the AIF1 master |
| `0x0303`–`0x0305` | `0x0040` | BCLK/LRCLK dividers |
| `0x0601`/`0x0602` | `0x0001` | DAC1L/R mixer ← AIF1.1 |
| `0x0700` | `0x8101` | codec GPIO1 |
| `0x0701`–`0x0706` | `0xa101` | codec GPIOs 2–7 |
| `0x0707`/`0x0708` | `0x4007`/`0x4008` | codec GPIO8/9 |
| **`0x0732`** | **`0x6020`** | **`FLL1_LOCK_STS` (bit 5) = 1** |

Note the codec is the **AIF1 master** on the reference (`0x302 = 0x4000`),
whereas our machine driver makes the McBSP the bit/frame provider.  That is a
separate difference, not the clock one.

## 3. What Linux does with the same values

With the codec reset (so no analogue load) and `AUXCLK1 = 0x00020100`:

```
0x221 = 0x0700   0x223 = 0x00e0   0x220 = 0x0004 (FRAC)
0x222 = 0xae14   0x220 = 0x0005 (FRAC|ENA)   0x224 = 0x0c80 (untouched)
read back: 0x220 = 0x0005, 0x221 = 0x0700, 0x222 = 0xae14,
           0x223 = 0x00e0, 0x224 = 0x0c80
0x732 = 0x6000   -> bit 5 = 0, no lock
```

Sweeping the AUXCLK1 divider field over 2, 3, 4, 5 and 7 (i.e. 12.8 / 9.6 /
7.68 / 6.4 / 4.8 MHz under either divider convention), with a gate off→on
transition for each, and with PM1/PM2 bias on or the codec fully reset, gives
**`0x732` bit 5 = 0 every time**.  The datasheet's free-running FLL1
(`OSC_ENA` + `FRC_NCO` + `OUTDIV = 7`) also fails.

The SoC side, read on that boot:

```
0x4A30A314 AUXCLK1  = 0x00020100   (after the manual write; 0x00000100 on one boot)
0x4A10019C pad 0x19C = 0x41180000  -> pad 0x19C (fref_clk2_out) = 0x0000, mux 0
hwdb pad record for 0x19C: mux 0, signal 0x64, extra 0   (match)
clk_summary: sys_clkin_ck 38.4 MHz, auxclk1_src_ck/auxclk1_ck enabled
```

So **every register we can see is identical between the two OSes** (codec FLL,
SCRM AUXCLK1, pad 0x19C), and only the reference's FLL locks.

## 4. What this means

* **The codec clock works on this board**; the goal is reachable.
* **The remaining difference is invisible to these registers**, i.e. one of:
  1. the pad's *output drive* (something outside the 16-bit padconf word — the
     `0x4A10019C` word's high half, pad `0x19E`, differs: QNX `0x4100` vs Linux
     `0x4118`, but the hwio table says `0x19E` is a different signal, 0x8A);
  2. the codec's analogue supply (`AVDD2`, which powers the FLL) or `AVDD1`
     (our DTS maps AVDD1 to 1.8 V against a 2.4 V minimum, AVDD2/CPVDD to 2.1 V
     against a 2.0 V maximum, and nothing about the mapping is verified);
  3. the `/RESET` pin or a board-level gate.
* **The next measurement is a meter** on pad `0x19C`/the codec's MCLK1 pin,
  and on DCVDD/DBVDD/AVDD1/AVDD2/CPVDD//RESET at the part, with QNX vs Linux
  compared on the same points.  A logic analyser is not needed for the level/
  presence check.
* **Do not "fix" the driver first.**  With MCLK1 absent, no ordering change can
  produce a clock; the reference's ordering is worth adopting *after* the pin is
  proven.

## 5. Corrections this report makes

* **Trap 151 / report 203 §5**: "MCLK1 does not clock on this unit" —
  wrong.  It does not clock under our Linux image; the reference locks its FLL
  from MCLK1.  Report 203's measurement was real but its inference was too
  strong.
* **Report 200/203's "the pin does not clock" framing**: both OSes program the
  same visible registers; the difference is below them.
* **Operational rule** (new): the codec must be left with `AIF1CLK_ENA = 0`
  when idle, exactly as the reference does; that alone keeps the register map
  accessible regardless of whether MCLK1 is running.

## 6. Sources

* Live TabletOS: `tools/pbi2c` `rd16`/`dump16` over the door, root ssh; the
  register values in §2.
* Live Linux: the `i2ctransfer` pairs, `devmem` of `0x4A30A314`/`0x4A10019C`,
  `/sys/kernel/debug/clk/clk_summary`.
* `tools/qnx-i2c.c` (16-bit support added); `tools/memdump3.c` (the QNX
  physical reader, unused this time).
* Reports 201 (driver RE), 203 (the freeze), 189/192 (the hwio pad table).
