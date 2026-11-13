# 209 — Power-domain / CM comparison: the MCLK1 fault is not a domain gate (session 44)

Date: 2026-11-12 (session 44, Linux-era 38).
Bench: the unit was moved twice in this session — first on the Linux image
(captures), then on the minimal 1917 oracle (causal tests), then back on Linux
for the DTS fix.  No NVRAM/MCT write; the RPMB top-up ran only through the
images' own boot path.  The oracle is the report-207/208 image
(`local/work/ifs-shim-sealed.ifs` + the pruned RCFS).

## 0. Headline

**The power-domain and clock-domain state does not gate the codec's MCLK1.**
The only differences between Linux and the working reference are (a) the
`LASTPOWERSTATEENTERED` debug latch in `PM_*_PWSTST[25:24]`, (b) the clock-domain
transition policy (`CLKTRCTRL`) of domains that do not contain the pad, and
(c) CAM/GFX powered in the reference and off in Linux.  Setting **every**
differing CM policy to Linux's value on the live, working oracle leaves FLL1
locked / the freeze detector landing.  Therefore, by the decision tree in
`HANDOFF.md` §0c, the result is **`R0x220 = 0x0002`: the domain is not the gate**
— the fault is below the readable registers (pad/board/PMIC; trap 165).

Separately, `CM_ABE_PLL_REF_CLKSEL` was fixed properly: reparenting DPLL_ABE to
`SYS_CLK` **alone** leaves its M/N programmed for 32 kHz and the DPLL never
locks; adding the rate (`assigned-clock-rates = <98304000>`) makes Linux lock
DPLL_ABE at 98.304 MHz from `SYS_CLK` with zero lock failures (image
`0x7C56BDC0`).  That is an ABE-path correctness fix, **not** the MCLK1 cause
(setting it made no difference to the freeze detector, here and in session 43).

## 1. How to read PWSTST

TRM Table 3-19 / Table 3-416 (`PM_ABE_PWRSTST`):

| bits | field | meaning |
|---|---|---|
| `[1:0]` | `POWERSTATEST` | 0 OFF, 1 RETENTION, 2 ON-INACTIVE, 3 ON-ACTIVE |
| `[2]` | `LOGICSTATEST` | 0 logic OFF, 1 ON |
| `[9:4]` | per-bank `*_MEM_STATEST` | memory bank states |
| `[20]` | `INTRANSITION` | transition in progress |
| `[25:24]` | `LASTPOWERSTATEENTERED` | **debug latch** (W1toSet), not a state |

`[25:24]` explains every "high half" difference below and is **not** a power
state.  The reference sets the latch; Linux leaves the reset value 0.

## 2. Live capture — Linux (idle, kernel `#30`, before any change)

```
PM_MPU        0x4A306304 = 0x000003F7      PM_CORE       0x4A306704 = 0x00003FF7
PM_TESLA      0x4A306404 = 0x00000000      PM_IVAHD      0x4A306F04 = 0x01000541
PM_ABE        0x4A306504 = 0x00000000      PM_CAM        0x4A307004 = 0x00000000
PM_ALWON_CORE 0x4A306604 = 0x00000000      PM_DSS        0x4A307104 = 0x00000037
PM_GFX        0x4A307204 = 0x00000000      PM_L3INIT     0x4A307304 = 0x00000037
PM_L4PER      0x4A307404 = 0x000000F7      PM_CEFUSE     0x4A307604 = 0x00000000
PM_WKUP       0x4A307704 = 0x00000000      PM_EMU        0x4A307904 = 0x00000000
```

CM `CLKSTCTRL` (low 2 bits = `CLKTRCTRL`: 0 NO_SLEEP, 1 SW_SLEEP,
2 SW_WKUP, 3 HW_AUTO — TRM Table 3-12):

```
CM1_MPU   0x4A004300 = 0x00000103   CM1_TESLA 0x4A004400 = 0x00000003
CM1_ABE   0x4A004500 = 0x00000003   CM2_CORE  0x4A008700 = 0x00000103
CM2_IVAHD 0x4A008F00 = 0x00000003   CM2_CAM   0x4A009000 = 0x00000001
CM2_DSS   0x4A009100 = 0x00000702   CM2_GFX   0x4A009200 = 0x00000003
CM2_L3INIT 0x4A009300 = 0x00045303  CM2_L4PER 0x4A009400 = 0x000C0103
CM2_L4SEC 0x4A009580 = 0x00000101   CM2_CEFUSE 0x4A009600 = 0x00000003
CM_WKUP   0x4A307800 = 0x00001903   CM_EMU    0x4A307A00 = 0x00000003
```

`PM_ABE_PWSTST` reads **0 (OFF)** at idle *and during a stream* (measured with
`pbplay 25 440` running: identical 0).  The kernel's own
`/sys/kernel/debug/pm_genpd/pm_genpd_summary` agrees (`prm_abe off-0`).

## 3. Live capture — the 1917 oracle, `io-audio` running

`/base/sbin/io-audio -d omap4abe-wm8994 aif=0,secure_capture -o
enable_buffering`; codec confirmed clocked (`R0x732 = 0x6020`, `R0x200 =
0x0010`, `0x220 = 0x0005`, `0x224 = 0x0C80`, `0x302 = 0x4000`).

```
PM_MPU        0x4A306304 = 0x020003F7      PM_CORE       0x4A306704 = 0x03003FF7
PM_TESLA      0x4A306404 = 0x00000000      PM_IVAHD      0x4A306F04 = 0x00000000
PM_ABE        0x4A306504 = 0x00000000      PM_CAM        0x4A307004 = 0x00000037
PM_ALWON_CORE 0x4A306604 = 0x00000000      PM_DSS        0x4A307104 = 0x00000037
PM_GFX        0x4A307204 = 0x00000037      PM_L3INIT     0x4A307304 = 0x03000037
PM_L4PER      0x4A307404 = 0x030000F7      PM_CEFUSE     0x4A307604 = 0x00000000
PM_WKUP       0x4A307704 = 0x00000000      PM_EMU        0x4A307904 = 0x00000000
```

```
CM1_MPU   0x4A004300 = 0x00000103   CM1_TESLA 0x4A004400 = 0x00000003
CM1_ABE   0x4A004500 = 0x00000001   CM2_CORE  0x4A008700 = 0x00000103
CM2_IVAHD 0x4A008F00 = 0x00000003   CM2_CAM   0x4A009000 = 0x00000702
CM2_DSS   0x4A009100 = 0x00000F02   CM2_GFX   0x4A009200 = 0x00000203
CM2_L3INIT 0x4A009300 = 0x00045302  CM2_L4PER 0x4A009400 = 0x00040102
CM2_L4SEC 0x4A009580 = 0x00000103   CM2_CEFUSE 0x4A009600 = 0x00000003
CM_WKUP   0x4A307800 = 0x00001903   CM_EMU    0x4A307A00 = 0x00000003
```

SCRM/pad/clock-definition registers, identical on both:

```
SCRM AUXCLK1  0x4A30A314 = 0x00020100     (gate on, src sys_clkin, /3)
CORE pad 0x19C low  = 0x0000              (mux 0 = fref_clk2_out; sys_nirq1 = 0x4100)
CM_ABE_PLL_REF_CLKSEL 0x4A30610C = 0        CM_SYS_CLKSEL 0x4A306110 = 7 (38.4 MHz)
```

## 4. The only real differences

1. **`PM_*_PWSTST[25:24]` (`LASTPOWERSTATEENTERED`, a debug latch).**
   Reference: CORE/L4PER/L3INIT = 3, MPU = 2.  Linux: 0 (never written).
   Not a state; not a gate.
2. **CAM and GFX are ON in the reference, OFF in Linux** (`PM_CAM 0x37` vs `0`,
   `PM_GFX 0x37` vs `0`).  Neither domain contains the pad or the SCRM.
3. **`CM1_ABE_CLKSTCTRL`**: reference `SW_SLEEP`, Linux `HW_AUTO`.  Notably the
   reference puts the ABE clock domain to *sleep* while `io-audio` idles —
   i.e. the codec's MCLK1 does **not** depend on the ABE domain at all.
4. **`CM2_L4PER/L3INIT/CAM/DSS/GFX/L4SEC` transition policy**: reference forces
   `SW_WKUP` on L4PER/L3INIT/CAM, Linux uses `HW_AUTO`; L4SEC the reverse.
5. `CM2_CORE_CLKSTCTRL` — **identical** (`0x00000103`), and it is the domain
   that owns the CORE pad and the SCRM.

## 5. The causal test on the working oracle

Baseline freeze detector (thaw `R0x200=0`; `R0x200=0x0001`; write
`R0x220=0x0004`; read back): **`0x220` reads `0x0004`** → MCLK1 present.

Then every difference from §4 was applied to the oracle, in place:

```
CM1_ABE    0x4A004500 <- 0x00000003   (Linux HW_AUTO)
CM2_CAM    0x4A009000 <- 0x00000001   (Linux SW_SLEEP)
CM2_DSS    0x4A009100 <- 0x00000702   (Linux value)
CM2_GFX    0x4A009200 <- 0x00000003   (Linux HW_AUTO)
CM2_L4PER  0x4A009400 <- 0x000C0103   (Linux HW_AUTO)
CM2_L4SEC  0x4A009580 <- 0x00000101   (Linux SW_SLEEP)
CM2_L3INIT 0x4A009300 <- 0x00045303   (Linux HW_AUTO)
```

Readback confirmed `CLKTRCTRL` took effect (read-only `CLKACTIVITY` bits
stayed as hardware reported).  Detector re-run after each step and after the
last: **`R0x220` still reads `0x0004`** → **MCLK1 survives every Linux CM
policy**.  The USB link survived the L3INIT change (live modules keep the
domain awake under `HW_AUTO`).

## 6. The complementary test on Linux

After a cold boot `CM_ABE_PLL_REF_CLKSEL = 1` (`SYS_32K`; see §7).  Baseline
detector: write `R0x220=0x0004`, read back **`0x0000`** → dropped (MCLK1 absent).

Writing the reference's CM policy values to the Linux registers was mostly
reverted by the kernel's own clock-domain framework within the same command
(`CM1_ABE` back to `HW_AUTO`, CAM back to `SW_SLEEP`, L4PER/L3INIT to Linux's
values); only `CM2_L4SEC = 0x103` stuck.  Detector still dropped the write.

Finally the reference's own write order was replayed on Linux — pad mux
`0x19C`: `0x0000 -> 0x0007 -> 0x0000` (a real transition), then
`AUXCLK1 = 0x00000000 -> 0x00020100` — and the detector still dropped the
write.  So neither the register values nor the write order restore it.

## 7. `CM_ABE_PLL_REF_CLKSEL`: what the "fix" actually is

The register is not a stray bootloader value: the OMAP4 clock init forces it,
deliberately (`drivers/clk/ti/clk-44xx.c`):

```c
/* On OMAP4460 the ABE DPLL fails to turn on if in idle low-power state ...
 * Lock the ABE DPLL in any case to avoid issues with audio. */
abe_dpll_ref = clk_get_sys(NULL, "abe_dpll_refclk_mux_ck");
sys_32k_ck   = clk_get_sys(NULL, "sys_32k_ck");
clk_set_parent(abe_dpll_ref, sys_32k_ck);
clk_set_rate(abe_dpll, OMAP4_DPLL_ABE_DEFFREQ);   /* 98 304 000 Hz */
```

The reference runs the same DPLL from `SYS_CLK`.  Measured states:

| image | `CM_ABE_PLL_REF_CLKSEL` | `CM_CLKSEL_DPLL_ABE` | `ST_DPLL_CLK` | lock failures |
|---|---|---|---|---|
| previous Linux (`0x36597040` boot, before change) | 1 (`SYS_32K`) | `0x0082EE00` | 0 | (not checked) |
| parent-only DTS change (`0x5E97FEBE`) | 0 | `0x0082EE00` | 0 | **7** |
| parent **+ rate** change (`0x7C56BDC0`) | 0 | `0x00804018` (M=64, N=24) | **1** | **0** |

Reparenting alone leaves the M/N the clock init computed for 32 kHz, and the
DPLL then cannot lock (seven `dpll_abe_ck failed transition to 'locked'`
during boot).  With `assigned-clock-rates = <98304000>` the framework
recomputes M/N after the parent switch and the DPLL locks at **98.304 MHz from
`SYS_CLK`**.  Verified on the image: codec, McBSP2, the sound card, the touch
and the display all probe; the freeze detector is unchanged (still dropped) —
so this is an ABE-path fix, not the MCLK1 cause.  A warm reboot with the
register forced to 1 returns 0, proving the DTS assignment re-applies at boot.

## 8. What this closes, and what it leaves

* **Closed:** the power-domain/CM hypothesis.  No differing domain gates the
  pad; setting them all to Linux's values on the working reference does not
  reproduce the fault.
* **Left:** the fault is below the readable registers — the pad's output path,
  the board's clock net, or a PMIC/analogue condition (trap 165).  The
  software-only options that remain are the non-reference AIF1CLK design
  (McBSP master/SRG permutation per boot, trap 169) or a board probe, which the
  operator does not have.
* **Positive side-result:** DPLL_ABE now locks at 98.304 MHz from `SYS_CLK` on
  our image, matching the reference's reference clock, with no boot
  regressions.

## 9. Evidence files

* `docs/captures/wm8994-pm-cm-2026-11-12.txt` — the raw captures and the
  causal-test command log.
* `docs/captures/wm8994-oracle-live-2026-11-12.txt` — the session-43 oracle
  capture (still current for the codec values).
* Images: `0x36597040` (previous), `0x5E97FEBE` (parent-only, not kept),
  `0x7C56BDC0` (current, parent+rate).
