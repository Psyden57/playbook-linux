# Contradictions

Each file here records a *disagreement between sources*: the conflicting claims,
the evidence for each, what is still uncertain, and what would settle it.

The rule for the whole repository is: **do not silently resolve a contradiction.**
A contradiction is closed by adding evidence (usually a measurement), not by
picking the claim that reads better.  When it *is* closed, keep the file and add
a "Resolved" section saying how and when — the history is the point.

Every file in this directory is indexed below (`README.md` is this index, not an
entry).  Status is one of **open**, **resolved** or **superseded**; the one-line
note after it is what a reader needs before opening the file.

| file | status |
|---|---|
| `100-bb10mt-flash-path2-vs-operator-flashes.md` | **resolved 2026-11-01** — bb10mt's container carries the destination in the **V1 `qcfp`** record (`qcfm.pas:559`, `Delta + BlockIndex`), so an IFS container addresses `0x815 + 16 = 0x825` and never needed path 2; the earlier "offset mapped to 0 / write lands at `0x815`" reading was a bad measurement (packed from an `unpack`-rewritten `.lst`) and is retracted in-file.  `tools/bbusb.py pack` reproduces bb10mt's output byte-for-byte at `Delta` 16 and 192.  Report 164 then closed the other half: `0x39` is the **CREAD opcode**, not an MCT partition type, and partition resolution uses a fixed table — so path 2 genuinely cannot resolve (report 100 was right) and path 1 is the only write path.  Residual: which MCT the loader parses |
| `66-0xde-security-vs-bootmode.md` | **resolved (protocol) 2026-10-26; count source open** — `0xDE` is `BOOT_MODE` (`{status,mode,count}`), not a generic security/RPMB query.  Whether the `count` field *is* the RPMB `BOOTCTR_FCTRY`, and what the action-0 sentinel means, are not settled |
| `71-bootctr-stage-decrement-vs-provision-only.md` | **resolved (session 20)** — `71` §7b said the stage never decrements; `89`/`95`/`116` show it does, and reports `150`/`154` give the instruction-level mechanism (`0x8000912E`, the ARM dispatcher re-seal, and a **fatal** `0x2F` blink if the write fails).  `FACTORY-PATH.md` §2's "no write path" is superseded too |
| `85-ab-fallback-vs-factory-region0.md` | **resolved** — `85` planned both A/B regions CRC-valid; the factory path is region 0 only, once, and never reads `0x2074` |
| `104-mmc2-wall-vs-wdt2-vfs.md` | **resolved** — the ~1.5 s MMC2 "wall" was the WDT2 deadline; the eMMC blocker was `ocr_avail = 0`, fixed by `vemmc` |
| `111-vs-122-bridge-programming.md` | **resolved 2026-10-28, residual closed 2026-10-29** — the reference DOES program the TC358764 (`winch_lcdctl 0x13e50`, report 125); 121 §12/122's "nothing programs it" was wrong.  The "register port unreachable" residual was closed by `PPI_TX_RX_TA = 0x00040006` (report 132 §3): `IDREG = 0x00006500` and the whole map was read |
| `111-vs-123-tcon-byte-order.md` | **resolved 2026-10-29** — high byte first (the static-RE answer): with the bridge correctly programmed both orders drive the panel, but low-first swaps magenta↔green and inverts black/white (report 132 §8).  The follow-on frame glitch was fixed by report 133 |
| `118-vs-121-dsi-link-rate.md` | **resolved 2026-10-27** — 118 §8.2 was wrong; the reference runs four data lanes, not two |
| `bootctr-drain-vs-session10-readings.md` | **resolved for the rate (session 22); session-10 anomaly open** — one factory boot spends one count and an OS boot tops it back up (`50 → 49 → 47 → 49 → 50` measured); why session 10 read a static `0x32` is still unexplained |
| `expired-counter-nuke-vs-retry-halt.md` | **open** — `0xFFFFFFFF` is selector 2; normal-path nuke vs retry-branch halt depends on the expired unit's `0x2801`, which was not read, and there is no sacrificial unit left |
| `fan5702-safe-model-vs-cm3217-race.md` | **open as a rule, resolved for the CM3217** — readings taken from TabletOS of a device whose QNX driver is running are unreliable (our I2C accesses interleave with `ls-cm3217`); the same part reads a clean 32 → 191 lux from Linux.  DECISIONS §31, traps 103/104 (report 182) |
| `fan5702-dimming-register-bits.md` | **open** — the datasheet-derived `0b11xxxxxx` dimming write vs the bootloader/QNX `0x36`; the on/off path (GENERAL `0x10`) is unaffected (report 179) |
| `fref-clk2-pad-reference-decode.md` | **resolved 2026-11-08 (session 32, report 185)** — a fresh read-only dump with Wi-Fi up gives CORE `0x4A10019C = 0x41000000` (pad0 mux 0 `fref_clk2_out`) and SCRM `AUXCLK1 = 0x00020100` (12.8 MHz enabled); the session-29 "mux 3" decode was a **WKUP**-pad result (`0x4A31E058` pad0) generalised to the CORE pad.  The FREF2 output is matched and not causal for CMD5 |
| `gpio2-24-resx-vs-datasheet.md` | **open** — report 122 calls `GPIO2_24` the bridge RESX (medium), but the reference reads that bit LOW during a working display while the datasheet says RESX is low-active; the pad's input path is disabled, so the `DATAIN` bit may be meaningless.  Not blocking: the display works |
| `ldr01-0x10000-header.md` | **resolved — there is no header** — report 164's corrected addendum and the session-23 handoff give `file_offset = VA − 0x80100000`; session 22's note still records the `0x10000` belief and is left as written (evidence, not product) |
| `open-dispc-dsi-timing-fields.md` | **resolved (falsified) 2026-10-28** — all four fields and the QNX PHY words were forced and read back identical; the panel stayed black (report 124), so the split was not the blocker.  The display was later fixed by the DISPC core clock (report 133) |
| `rpmb-key-kek-vs-fallback-literal.md` | **resolved (session 18), key pinned (session 19)** — the monitor is handed the key bytes by pointer; the live page gives the CID-derived seed and the frame key `dfeec0ce…2274` (report 143).  The Linux consequence is **conditional on the block seal** (report 144), not on the key; the seal caller is solved (147), the transform is not (148/156) |
| `rpmb-page-rodata-contents.md` | **open (mechanism); contents measured** — report 92 says `page+0x20/+0x40` hold the `.rodata` constants, report 141 says nothing writes them; the live page measures the constants, but who writes them is unknown (session 19) |
| `safety-no-emmc-mount-vs-readonly-root.md` | **resolved (session 17), extended (session 18)** — read-only eMMC/QNX6 mounts are allowed; the operator has now dropped the blanket writable-eMMC prohibition **for our own data** (NVRAM, the MCT, and dangerous RPMB still forbidden).  Rule in `SAFETY.md` §4/§6, decisions `DECISIONS.md` §17/§18; `hd3` to be repurposed (report 141) |
| `touch-bus-i2c2-vs-i2c3.md` | **open** — the 1917 touch stack opens `/dev/i2c2` (QNX I²C3 `0x48060000`, which QNX does not instantiate) while the live QNX scan saw the touch's `0x65` on `/dev/i2c1` (I²C2 `0x48072000`); `muninfw`'s default address is `0x65`.  Settle it with read-byte `i2cdetect -y -r 2` and `-r 3` on the Linux image |

## Still open — keep these in mind

Six files have a genuinely open question left; none of them blocks the
IFS rebuild, but each changes how a result should be read:

1. **`expired-counter-nuke-vs-retry-halt.md`** — what an out-of-range
   `BOOTCTR_FCTRY` does at boot (normal-path nuke vs retry-branch halt) is still
   untested.  The two 32 GB units in that state cannot be read without an OS, and
   there is no spare to trace it on.  Do not assume "it only refuses to boot";
   do not test it.
2. **`gpio2-24-resx-vs-datasheet.md`** — whether `GPIO2_24` is really the
   TC358764's RESX.  The display works with the current pulse, so this matters
   only if the reset sequence is changed; do not build an experiment that depends
   on it.
3. **`rpmb-page-rodata-contents.md`** — who writes `page+0x20`/`page+0x40` of
   `rpmb-omap4`'s page, and when.  The contents are measured; the writer is not.
   It is a mechanism question behind the `0x2F` transform, not a value question.
4. **`bootctr-drain-vs-session10-readings.md`** — the *rate* is settled going
   forward (one factory boot = one count; an OS boot tops up), but session 10's
   static `0x32` across many boots is still unexplained.  Keep measuring the
   counter at both ends of a cycle rather than trusting either story.
5. **`fan5702-dimming-register-bits.md`** — whether the FAN5702 dimming
   registers `0x30/0x40/0x50` take the datasheet's `0b11xxxxxx` or the
   bootloader's `0x36`; the on/off path (GENERAL `0x10`) is unaffected.
   *Would settle it:* a brightness sweep on the live unit, or datasheet Table 4.
6. **`touch-bus-i2c2-vs-i2c3.md`** — whether the Cypress touch at `0x65` is on
   OMAP I²C2 `0x48072000` (where QNX saw `0x65`) or I²C3 `0x48060000` (the
   library's `/dev/i2c2`).  *Would settle it:* read-byte `i2cdetect -y -r 2`
   and `-r 3` on the Linux image, before and after the decoded reset pulse.

Two smaller residuals, not contradictions in their own right:
`66` still does not prove that the `0xDE` `count` field *is* the RPMB record, and
`100` still has "which MCT the loader parses" (`External MCT/HWV: Enabled`)
unidentified.

### Which files touch the same hardware

* `BOOTCTR_FCTRY` (three files, deliberately not merged): decrement **mechanism**
  → `71-bootctr-stage-decrement-vs-provision-only.md` (resolved); drain **rate** →
  `bootctr-drain-vs-session10-readings.md` (rate resolved); **out-of-range**
  behaviour → `expired-counter-nuke-vs-retry-halt.md` (open).
  `rpmb-key-kek-vs-fallback-literal.md` is adjacent: it asks what authenticates
  the record those three read and write.
* Display (three files): **lane count** → `118-vs-121-dsi-link-rate.md`
  (resolved); **bridge programming** → `111-vs-122-bridge-programming.md`
  (resolved); **timing-field split** → `open-dispc-dsi-timing-fields.md`
  (falsified); the byte order is `111-vs-123-tcon-byte-order.md` (resolved).
