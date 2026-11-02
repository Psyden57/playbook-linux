# 132 — The QNX DSI register oracle, the latched TA value, and the first image of ours on the panel

**Session 15 (absolute), Linux-era session 9.  2026-10-29.**
Images under test: `0xFCC09B7B`, `0x5934491B`, `0xD670FA8C`, `0x8C82DD62`
(all in `local/images/`).  New tool: `tools/qnx-dsiread.c` → `tools/pbdsi`.

This session answered the question report 131 §4g left open, disproved the
central inference of report 131 §4f, found and fixed a real bug in our bridge
programming, and **put our own framebuffer on the panel for the first time**.
The panel is not yet stable — it shows a corrupted frame — but the whole chain
DSI → TC358764 → LVDS → TCON → panel is now demonstrably live.

---

## 1. Summary

| question | answer |
|---|---|
| Do our bridge reads get reliable? | **Yes.** `PPI_TX_RX_TA (0x013C)` decides it, and the value is **latched by `PPI_STARTPPI (0x0104)`**. Boot with `4` → 4/8 at boot and 8/16 sampled; boot with `0x00040006` → 8/8 and 16/16. Changing `0x013C` after boot does **not** move the rate. |
| Is the VC re-arm the fix (trap 37)? | **No.** At `TA = 4`, `t0214` (with re-arm) and `u0214` (without) both give 8/16. The re-arm is still needed so a failed read does not leave `VC_EN` cleared, but it is not what makes reads work. |
| Is `LANESTATUS0 = 0` evidence of a dead HS link? | **No — it is worthless as an indicator.** On a *working* TabletOS display, read over DSI with our own tool: `0x0214 = 0x00000000`, `0x0218 = 0x0000001D`, `0x0500 = 0x00000000` — the same values as ours. Report 131 §4f's clock-lane inference is withdrawn. |
| Was our bridge configuration right? | **No.** `VP_HTIM1/HTIM2` were `0x78000001`/`0x76000400`; the reference writes `0x00780001`/`0x00760400`. The porches (bits 24:16) were in the reserved top byte, so the bridge stored **HBP = HFP = 0**. Report 131's "upper half not stored" was our own constant, not hardware. |
| Does the panel show our content now? | **Yes, for the first time**: after the timing fix **and** the Hitachi TCON table, a magenta band at the top and then glitchy alternating black/white lines. Not an acceptable image yet. |

Two facts from the operator frame the whole session:

* **The spare's own TabletOS displays correctly.** So on this exact unit the
  DSI clock lane, the TC358764, the LVDS link, the TCON and the panel are all
  good. Everything that remains is our host-side software.
* There is **no scope** and no test point, as documented.

---

## 2. The oracle: reading the bridge from QNX over DSI

Report 131 §6 listed the missing TC358764 register map as the blocker.
`tools/qnx-dsiread.c` (built with the QNX 6.6 toolchain to `tools/pbdsi`,
`docs/project/SETUP.md` §2) sidesteps it: it maps the OMAP DSI protocol engine
at `0x58004000` with `mmap_device_memory()` and performs a MIPI **generic read
request, 2 parameters** (DT `0x24`, packet VC 0) on **hardware VC 1**, then
enables `BTA_EN`, waits for `DSI_VC_IRQSTATUS.BTA`, and pops the response out of
the RX FIFO.  It is a read-only tool: it never writes a bridge register.

This is the same transport the Linux console's `pb_reg` read uses, and the same
VC/packet-VC split QNX uses for its own bridge writes
(`winch_lcdctl 0x13ce0`, report 128).  The sanity check that the mapping is
right is built in: it prints `DSI_CTRL = 0x00EAEF9D`, `VC_CTRL(0) =
0x20808791`, `VC_CTRL(1) = 0x20009D81`, which are the reference values already
on record (reports 118/127).

Facts learned building it, worth keeping:

* the response is a **generic long read response, DT `0x1A`** (not the DCS
  `0x21`), word count 4, little-endian, followed by a 2-byte checksum;
* `PPI_TX_RX_TA = 4` (the reference's own value) makes **half** the reads fail
  with an ACK/error report `0x0003` (SoT error + SoT sync error) — the same
  50 % phenomenon measured on our side (§3).  The tool retries, and successful
  samples agree with each other, so the captured values are trustworthy;
* do **not** wait for `VC_TX_FIFO_EMPTY` (bit 5) before a read: QNX leaves it
  clear on the command VC, and waiting for it burns the poll loop on every read
  (measured: ~1 s per read, minutes for a dump).  The Linux writer only tests
  "FIFO not full".

Full-map capture on the working display (353 registers, `0x0000…0x0580`, every
4 bytes) → `docs/reports/data/qnx-bridge-map-2026-10-29.txt`.  Non-zero
registers:

```
0004=00000002 0020=0000a002 0024=0000a002 0028=0000a002 002c=0000a002
0030=0000a002 003c=00000001 0108=00000001 0110=0000208e 0114=00000004
0130=00000010 0134=0000001f 013c=00000004 0164..0170=00000004
017c=00000010 01e4=00000155 0208=00000001 0210=0000001f 0218=0000001d
0220=80400003 0224=f07fafff 0270=80000302 0300=c08000d9 0404=00000003
0450=03f00120 0454=00780001 0458=00760400 045c=000a0001 0460=000c0258
0480..0498 = the seven LV_MX words 049c=00000001 04a0=0004c02d 0520=00000005
0524=00000005 0528=00000005 0540=00800180 0580=00006500
```

Note `0464` (`VP_VFUEN`) is **absent** — it reads `0` after QNX writes `1`.

---

## 3. `PPI_TX_RX_TA` — the answer to report 131 §4g, and a correction to §4a

Report 131 §4g proposed raising `PPI_TX_RX_TA (0x013C)` from `4` to
`0x00040006` because our bridge reads failed intermittently with `-EIO`.  This
session built the A/B properly.  Three boots, same code except the constant:

| boot | `0x013C` written by the sequence | boot-time `pb late bridge` | console `t0214` | console `t0500` |
|---|---|---|---|---|
| `0xFCC09B7B` | `0x00040006` | **8/8** every register | **16/16** | 16/16 |
| `0x5934491B` | `0x00000004` | **4/8** every register | **8/16** | 8/16 |
| `0xD670FA8C` | `0x00040006` | **8/8** every register | — | — |

And in the `4` boot, writing `0x40006` **live** did not help (`t0214` stayed
8/16 twice), while in the `0x40006` boot writing `4` live did not hurt
(16/16).  **The value is sampled when `PPI_STARTPPI (0x0104) = 1` runs**, not
read continuously, so what matters is the constant in `pb_bridge_seq`, eight
entries before `STARTPPI`.

Also measured in the same boot (`0x5934491B`, `TA = 4`):

```
pb reg: t 0214 ok=8/16 fail=8      (re-arm before every attempt)
pb reg: u 0214 ok=8/16 fail=8      (no re-arm)
pb reg: u 0214 ok=9/16 fail=7
pb reg: t 0500 ok=8/16 fail=8
pb reg: u 0500 ok=8/16 fail=8
```

so the VC re-arm (trap 37) is **not** what governs success.  Trap 37 stays as
written — a failed read really does clear `VC_EN`, so a probe must still
re-arm before the *writes* it is measuring — but it is not the explanation for
the `-EIO` storm.

`0x013C = 0x00040006` is kept in the tree.  The reference's `4` works for QNX
only because **QNX never reads the bridge**.

Consequence: `LANESTATUS0 = 0` is now a reliable measurement (§1) — and §4
shows it means nothing.

---

## 4. Report 131 §4f is withdrawn: those registers are not fault indicators

Report 131 concluded "the fault looks like the clock lane" from
`DSI_LANESTATUS0 = 0`, `SYSSTAT = 0` and "the panel ignores `LV_CFG`".  The
oracle reads the same registers on a display that provably works:

| register | TabletOS (working display) | ours (`0xD670FA8C`) |
|---|---|---|
| `0214` DSI_LANESTATUS0 | **0x00000000** | 0x00000000 |
| `0218` DSI_LANESTATUS1 | 0x0000001D | 0x0000001D |
| `0500` SYSSTAT | **0x00000000** | 0x00000000 |
| `049C` LV_CFG | 0x00000001 | 0x00000001 |
| `0220` DSI_INTSTATUS | 0x80400003 | 0x10400880 |
| `0300` DSIERRCNT | 0xC08000D9 (varies per read) | 0xC0800000 |
| `0230` | 0xFFFFFFFF | 0xFFFFFFFF |

So `LANESTATUS0` and `SYSSTAT` read **zero on a perfectly working link**, and
`0x0230` reads all-ones on a working chip (we had wondered whether
`0xFFFFFFFF` meant "unimplemented"; it is simply the value).  Everything report
131 §4b/§4f built on those two registers is inference about undecoded bits and
is withdrawn.  The "the panel ignores `LV_CFG`" observation survives, but it
does not discriminate — on a working system the same toggle would not be
visible either.

---

## 5. The bug: `VP_HTIM1` / `VP_HTIM2` porches in the reserved top byte

The whole-map diff, taken with reliable reads (`0xD670FA8C`, before the fix):

```
0454: qnx=00780001  linux=00000001
0458: qnx=00760400  linux=00000400
```

The bridge's video-path timing is
`VP_HTIM1 = HBP[24:16] | HPW[8:0]`, `VP_HTIM2 = HFP[24:16] | HACT[10:0]`
(`tc358764.c`'s own field macros).  The reference's values are **HBP 120, HPW 1,
HFP 118, HACT 1024** (`winch_lcdctl 0x13f78/0x13f88`, report 130 §3.3).  Our
sequence carried `0x78000001` and `0x76000400` — the porch counts shifted 8 bits
too high, into `[31:24]`, which the bridge does not store.  Readback was
therefore `0x00000001` / `0x00000400`, i.e. **HBP = 0 and HFP = 0**.

Report 131 §3 recorded exactly this readback and concluded "upper half not
stored … probably a 16-bit register pair; recorded as observation, not as a
fault".  It was our constant, and it was a fault.

Fixed in `kernel/source/dsi.c` (`pb_bridge_seq`) and
`kernel/source/tc358764.c` (`tc358764_init`).  After the fix (`0x8C82DD62`):

```
pb reg: r 0454 -> 0 00780001
pb reg: r 0458 -> 0 00760400
pb reg: r 0464 -> 0 00000000      <- VFUEN now self-clears, exactly as on QNX
pb reg: r 0450 -> 0 03f00120
```

`VP_VFUEN` reading `0` — where it read `1` before — is the independent
confirmation that the video-path block now latches a frame update.

---

## 6. Whole-map diff after the fix

With `0x8C82DD62` (porches fixed) the 353-register map differs from the
TabletOS capture in **four** registers:

```
013c: qnx=00000004 linux=00040006    deliberate (§3) — read reliability
0220: qnx=80400003 linux=10400880    live status, undecoded
0270: qnx=80000302 linux=c000041a    live status, undecoded
0300: qnx=c08000d9 linux=c0800000    live counter, varies on both sides
```

Every **configuration** register of the TC358764 is now identical to a working
system, including the LVDS porches, the seven `LV_MX` mapping words, `LV_PHY0`,
`LV_CFG`, `VP_CTRL`, `VP_VTIM1/2`, `SYS_RST = 4`, `GPIOC/GPIOO`, the PPI timing
words, the lane enables and `IDREG = 0x00006500`.

Host side on the same boot (`peek`): `DSI_CTRL = 0x00EAEF9D`,
`CLK_CTRL = 0xA0346005`, `TIMING1 = 0x79190999`, `TIMING2 = 0x7FFF7FFF`,
`VM_TIMING1 = 0x01079074`, `VM_TIMING2 = 0x04010C0C`,
`VM_TIMING3 = 0x03F00258`, `CLK_TIMING = 0x00000E0B`,
`VM_TIMING7 = 0x000000C8`, `VM_TIMING8 = 0x000B0009`,
`VC_CTRL(0) = 0x208087B3` (bit 1/5 deltas as before), `VC_CTRL(1) = 0x20009D81` —
all the reference's values.

---

## 7. The first image of ours on the panel

With `0x8C82DD62`, the framebuffer painted magenta (`fb ff00ff`) and the bridge
programmed, the panel was still black.  Sending the reference's 9-word Hitachi
TCON table (§8) then produced, in the operator's words:

> "*Very glitchy image on the screen now! I see about a 1 cm wide horizontal
> magenta band on the top, then a lot of alternating horizontal black and white
> lines, shifting around a bit, and some 'static', very glitchy looking.*"

This is the **first time our framebuffer content has ever been on the panel**
(the magenta band is ours; the session-13 "partial image" was not — report
129 §5).  The whole chain is therefore live: DISPC → DSI HS video → TC358764 →
LVDS → TCON → panel.  What is wrong is the *framing* after the first lines.

The TCON table is required: the reference sends it as part of its own bring-up
(`0x1c580`, called from the sequencer at `0x193c4`, i.e. **after** the bridge
programming and the HS start), while our kernel only offers it as the console
command `spi tcon`/`spi tconbe`.  An image built with the table in the boot path
is the obvious next experiment.

---

## 8. The TCON byte order: both orders drive the panel, differently

With the framebuffer magenta and the bridge correctly programmed:

| order | result (operator) |
|---|---|
| **high-first** (`spi tconbe`, the reference's) | magenta band on top, then alternating **black/white** lines |
| **low-first** (`spi tcon`) | *"the same sort of pattern … but **green** instead of magenta, and the **white and black swapped** places"* |

So the byte order is not "works vs does not work": it flips the colour and
inverts the field — magenta↔green is a red/blue swap, black↔white an inversion.
The reference sends high byte first (`winch_lcdctl 0x1c51c/0x1c520`, report 123
§2), so **high-first is the order to keep**; low-first should be treated as
evidence that some polarity/inversion bit of the TCON is being set by the wrong
half of the pair.

The contradiction file (`docs/contradictions/111-vs-123-tcon-byte-order.md`) is
updated with this: the *functional* question is settled in favour of the static
RE (high-first), and the remaining glitch is not a byte-order symptom.

---

## 9. Bench cycle

| # | action | result |
|---|---|---|
| — | `BOOTCTR_FCTRY` read, IFS hash verified | `0x32`, `c72b0530…` |
| 1 | flash `0xFCC09B7B` (TA 0x40006, reliability probes) | 8/8 boot reads, A/B done |
| 2 | restore sealed IFS (for the oracle) | hash verified |
| 3 | `tools/pbdsi` on TabletOS | 353-register map captured; §4 result |
| 4 | flash `0x5934491B` (TA 4, `d`/`u`/`v` ops, new `/init`) | 4/8 boot reads; §3 A/B |
| 5 | flash `0xD670FA8C` (TA 0x40006) | 8/8; clean full map |
| 6 | flash `0x8C82DD62` (porch fix) | 16/16-style reads; §5–§8 results |
| 7 | restore sealed IFS, verify | `c72b0530…` MATCH, `BOOTCTR_FCTRY = 0x32` |

Four image flashes and two IFS restores; `BOOTCTR_FCTRY` started and ended at
`0x32` (each TabletOS boot tops it up).

---

## 10. Tooling and tree changes

* **`tools/qnx-dsiread.c` → `tools/pbdsi`** — the DSI read oracle (§2).  Built
  with the QNX 6.6 toolchain; not kept on the unit (`/tmp` is wiped by a reboot),
  so push it each time (`tools/pbssh.sh 'cat > /tmp/pbdsi' < tools/pbdsi`).
* **`/init` no longer auto-reboots at 180 s.**  PID 1 now reboots only when the
  console asks (operator's call: the timer was a pre-console way to get the log
  back, and every iteration spent a `BOOTCTR_FCTRY` unit — trap 33).  With the
  ACM console up, reboots are deliberate: console `reboot`, or the power button.
* **Build-tree `.config` bug (this cost the session's init change a build).**
  `local/kernel-tree/.config` still said
  `CONFIG_INITRAMFS_SOURCE="/home/psyden/rea-work/bootdumps-2026-09-11/analysis/initramfs/root"`,
  while `kernel/winchester.config` says `initramfs/root` here.  Every image
  built in this workspace was therefore embedding **rea-work's older `/init`**
  (455 284 B, 27 Oct) — which is why the console command `v` was "broken", why
  the 180 s timer was still there, and why init changes never took effect.  The
  build tree's `.config` is corrected, and `DEVELOPMENT.md` §3 now says to check
  it.  Verified: the rebuilt cpio contains this repo's `/init` (590 560 B) and
  the string `no auto-reboot`.
* **`pb_reg` console hook** gained `d<from>:<to>` (dump every 32-bit bridge
  register in a range to kmsg, 8 per line), `t<addr>` (16 reads, re-arming the VC
  each time) and `u<addr>` (16 reads, no re-arm).  The documented `v` op was
  **unreachable** — `sscanf("%c%x:%x", …)` returns 1 for a bare `v` and the code
  rejected `n < 2`; fixed.
* **Kernel patch re-exported**: `kernel/clean-kernel-6.15.11-changes.patch` and
  `kernel/patches/0001-playbook-winchester-6.15.11.patch`, at this point
  **4 140 lines / 31 files**, verified by applying to a fresh copy of pristine
  6.15.11 and comparing all 31 files byte-identically.  (Re-exported again later
  in the session when `dss.c` changed — the final state is **4 169 lines / 32
  files**; report 133 §3.)

---

## 11. What is left: the frame is live but badly framed

The remaining symptom is a data/timing corruption after the first lines, with
every bridge and DSI register matching a working system.  Things this session
established that bear on it:

1. **The bridge latches frames** (`VFUEN` self-clears), so the DSI video stream
   is arriving.
2. **The output is not noise**: the band's colour follows the framebuffer, and
   the byte order flips it (magenta↔green, black↔white).  Data is being decoded,
   then lost.
3. A **DISPC whole-map diff** was taken (`0x8C82DD62` vs TabletOS,
   `153` words over `0x1000…0x181C`; 41 differ).  Decoded against the TRM, the
   only functional deltas outside the overlay/framebuffer bank are:
   * `DISPC_CONTROL1 (0x1040)`: QNX `0x02000309`, ours `0x00018309` —
     `TFTDATALINES[9:8] = 3` on both, `LCDENABLE = 1` on both; the deltas are
     `GPOUT0/GPOUT1[16:15] = 1` on ours vs `0` on QNX, and a TDM-only bit
     (`[26:25]`).  `GPOUT0/1` are the only functional candidate here and are
     unverified wiring-wise.
   * `DISPC_CONFIG1 (0x1044)`: QNX `0x00000004`, ours `0x0000020C` — ours sets
     bits 3 and 9 additionally.
   * `DISPC_LINE_NUMBER (0x1060)`: QNX `0x00000257` (599), **ours `0`** —
     suspicious, since `TIMING_V` itself matches; worth one look.
   * the rest are overlay attribute/size/base registers and status
     (`VID`/`GFX` bank), which are expected to differ with a different UI.
4. **The next single experiment** is to send the Hitachi TCON table from the
   kernel's display path (after the bridge programming, as the reference does at
   `0x193c4`) instead of by hand, and then to re-check the frame.  If the
   glitch survives, the candidate list from §11.3 is where to look.

Do **not** re-test: `PPI_TX_RX_TA` values (§3 settles it), the VC re-arm as a
read fix, `LANESTATUS0`/`SYSSTAT` as indicators (§4), the LVDS mapping, the
backlight, the DSI/PLL/PHY words, or the PMIC rails.

---

## 12. Corrections to earlier reports

| where | what was wrong | now |
|---|---|---|
| 131 §4a/§4b/§4f | `LANESTATUS0 = 0` + `SYSSTAT = 0` were read as "no HS RX / LVDS PLL unlocked", and §4f concluded the fault was the clock lane | They read the same on a **working** TabletOS display.  Inference withdrawn (§4). |
| 131 §4g | "raise `PPI_TX_RX_TA` and see if the `-5`s stop" — left open | Confirmed, with the mechanism: the value is latched at `PPI_STARTPPI`; `4` → 50 % failures, `0x40006` → none (§3). |
| 131 §3 | "`VP_HTIM1/2` upper half not stored — probably a 16-bit register pair; observation, not a fault" | Our constants had the porches in the wrong byte (`0x78000001` vs `0x00780001`).  Fixed; readback now matches QNX (§5). |
| 131 §4a | "the bridge writes land, HS video does not arrive" | The bridge now latches frames (`VFUEN` = 0) and the panel shows our content (§7). |
| 129 §7e / 131 §4f | "`0x0230` reads `0xFFFFFFFF`, possibly unimplemented" | It reads `0xFFFFFFFF` on a working chip too (§4). |
| 130 §3.3 | — (the reference values were right) | Our tree's constants disagreed with it; the values in 130 are the ones now used. |
| contradiction 111-vs-123 | byte order "works vs does not" | Both orders drive the panel; high-first is the reference's and gives the non-inverted field (§8). |
