# 173 — The bootloader's load base, the RPMB state wipe, and the chainloader probes

**Session 26 (absolute), Linux-era 20.  2026-11-04.**
Status: **static analysis plus three chainloader probes (one dump, two calls).
No RPMB write was issued.**  The unit was restored to PlayBook OS 2.1.0.1917
and the counter re-topped to `0x32` after a mount-probe reset loop.

This report exists because the session set out to reuse the bootloader's own
RPMB writer from our chainloader and, before any call could be trusted, had to
settle **where the bootloader is actually loaded**.  That turned out to be the
missing fact behind a contradiction in report 151, and it changes how every
address in reports 150/151 should be read.

---

## 0. Headline

1. **The arm9000 section (media file `0x9000`) is loaded at `0x80000000`.**
   Reports 150/151 used `address = file + 0x80000000`, i.e. they assumed media
   file 0 maps to `0x80000000`.  It does not: media `0x9000` maps to
   `0x80000000`.  **Every *code* address quoted in reports 150/151 is `0x9000`
   too high.**  Data addresses (the absolute literals the code uses) are
   correct as written.
2. **The "wipe table anomaly" in report 151 §Q5 does not exist.**  Report 151
   read the table at media `0x1431C`; the table is at DRAM `0x8001431C` =
   media `0x1D31C`, where it holds valid ranges.
3. **The bootloader intentionally wipes its RPMB state before jumping to the
   OS.**  The tail wipe covers `[0x80025E78, 0x800517C4)`, which includes the
   store state (`0x80025FB0`), catalogue (`0x8002C760`) and staging
   (`0x80033F80`).  Therefore a chainloader cannot call the bootloader's
   `record_read`/`record_write` on a pre-built catalogue; it must rebuild it.
4. **The tail also disables the CM2 L4SEC crypto clocks**
   (`0x8000F184`).  A chainloader must re-enable them (`0x8000F108`) before any
   seal/unseal SMC; the enable returned `1` from our chainloader.
5. **Calling the bootloader's selector from the chainloader works** (it
   returned `0` = "absent", because the catalogue had been wiped).  **Calling
   its mount (`0x800082DC`) is not viable as delivered**: it hung and reset the
   unit in a loop, which spent ~10 counter units before the unit was restored.

---

## 1. How the load base was found (the dump probe)

The chainloader gained a build-time probe (`BL_PROBE`, default off) in
`boot/chainloader/chainloader_kernel.S.in`, built by
`tools/build_kernel_image.py`.  `BL_PROBE=1` copies a set of bootloader regions
into the DTS no-map page `0xA0200000` (the old `pbmon-low` page), then Linux
reads it with `peek a0200000 16`.  It is pure reads: no calls, no writes.

The dump (image CRC `0xBD8966DD`) came back with `"BLDU"` in the page and WDT2
CRR `0xFFFC8DF0`.  The copied region that was supposed to be `0x80009000`
contained Thumb code, not the ARM vector table the corpus has at media
`0x9000`.  Matching those bytes against the media found them at **`0x12000`**:

```
DRAM 0x80009000 bytes  = media 0x12000 bytes   (16/16 match)
DRAM 0x80009078 bytes  = media 0x12078 bytes   (40/40 match)
```

and the report's addresses were unloaded `0xFFFFFFFF`:

```
DRAM 0x800158E0 = 0xFFFFFFFF
DRAM 0x80018100 = 0xFFFFFFFF
DRAM 0x80019220 = 0xFFFFFFFF
```

The media copy is the corpus: `blk sumb 0 1048576` from the running Linux image
returned FNV-1a64 **`21c38ed917e0b38f`**, exactly the host FNV of
`local/proprietary/device-binaries/emmc-first1mb.bin`, and media `0x9148` is
`"BOOTCTR_FCTRY"`.  The executing image is the same bytes; only the base was
mis-assumed.

**Deduction:** for a code address `A` in reports 150/151, the real DRAM address
is `A - 0x9000` (equivalently, media file `f` maps to `0x80000000 + f - 0x9000`).

---

## 2. Corrected code addresses

These are the addresses the chainloader must use.  Data literals are **not**
shifted.

| function | report 150/151 address | **corrected DRAM address** |
|---|---:|---:|
| selector worker `selector(u32 *out)` | `0x800090CE` | **`0x800000CE`** (Thumb) |
| selector veneer (out = NULL) | `0x8000911A` | **`0x8000011A`** |
| provision helper (mode 1 writes `0x32`) | `0x800090A0` | **`0x800000A0`** |
| `record_read` | `0x80011690` | **`0x80008690`** |
| `record_write` | `0x800115E6` | **`0x800085E6`** |
| install / rewrite | `0x800114FA` / `0x80011332` | **`0x800084FA`** / **`0x80008332`** |
| RPMB block read (type 4) | `0x80010A34` | **`0x80007A34`** |
| RPMB block write (type 3/5) | `0x80010B2E` | **`0x80007B2E`** |
| seal | `0x80010F58` | **`0x80007F58`** |
| unseal | `0x80011048` | **`0x80008048`** |
| mount / catalogue rebuild | `0x800112DC` | **`0x800082DC`** |
| RPMB probe (SMC 0x31/0x32/0x31) | `0x80010D3C` | **`0x80007D3C`** |
| SMC dispatcher | `0x800183DC` | **`0x8000F3DC`** |
| SMC 0x3A wrapper | `0x80017F64` | **`0x8000EF64`** |
| CM2 L4SEC enable | `0x80018108` | **`0x8000F108`** (ARM) |
| CM2 L4SEC disable | `0x80018184` | **`0x8000F184`** (ARM) |
| reset path / jump | `0x80019224` | **`0x80010224`** |
| tail wipe | `0x800158E4` | **`0x8000C8E4`** |
| wipe helper | `0x80015910` | **`0x8000C910`** |
| boot main | `0x80014904` | **`0x8000B904`** |
| L4SEC-disable call in the tail | `0x800094CC` | **`0x800004CC`** |

Data / table addresses, correct as written: session flag `0x80025F00`; store
state `0x80025FB0`; catalogue `0x8002C760`; staging `0x80033F80`; free table
`0x80033780`; RPMB frame `0x8002A4E0`; wipe table `0x8001431C`; saved SP
`0x800517C0`; OS entry pointer `0x80025E74`.

Verification against the media (every corrected address decodes to the
function the report described; every uncorrected address lands on the next
function or `0xFFFFFFFF`):

```
0x800000CE  media 0x090CE  1c b5 04 00...   push {r2,r3,r4,lr}; movs r4,r0
0x800000A0  media 0x090A0  08 b5 30 b9...   push {r3,lr}
0x8000F108  media 0x18108  a8 01 9f e5...   ARM: ldr r0,[pc,#424]
0x8000F3DC  media 0x183DC  0f 00 2d e9...   ARM: push {r0,r1,r2,r3}
0x8000C8E4  media 0x158E4  2c 48 10 b5...   tail wipe entry
0x80010224  media 0x19224  d3 00 a0 e3...   ARM: mov r0,#0xD3 (reset)
```

---

## 3. The wipe table, resolved (report 151 §Q5)

Report 151 read `0x8001431C` as a *file* offset and got Thumb code, and flagged
the range table as inconsistent.  At the corrected media offset **`0x1D31C`**
the table is:

| word | value |
|---|---:|
| start 1 | `0x800267C0` |
| len 1 | `0x0002B004` |
| start 2 | `0x80025E78` |
| len 2 | `0x00000948` |
| start 3 | `0x800F8000` |
| len 3 | `0` (skipped) |
| start 4 | `0x800F8000` |
| len 4 | `0` (skipped) |
| start 5 | `0x40304000` |
| len 5 | `0` (skipped) |
| start 6 | `0x40304000` |
| len 6 | `0` (skipped) |

The helper `0x8000C910` wipes `[start, start+len)`; the two live groups cover
**`[0x80025E78, 0x800517C4)`**.  The guard is `[0x80026150]`, set to `1` on the
factory path (`0x8000BA48` in boot main), so on a factory/CRC boot this wipe
runs.  It zeroes:

* the saved-SP word `0x800517C0` — **harmless**, because the entry (our
  chainloader, and QNX's) establishes its own SP; the bootloader reads SP
  before the first push and only then branches;
* the whole RPMB in-memory store: session flag `0x80025F00`, store state
  `0x80025FB0`, frame `0x8002A4E0`, catalogue `0x8002C760`, free table and
  staging `0x80033780`/`0x80033F80`.

This is why the chainloader's selector returned **0 (absent)**: the catalogue
`find()` scans an empty table.  It is not a failed call.

---

## 4. The tail disables the CM2 L4SEC clocks

`0x800004CC` (report `0x800094CC`) writes `0` to `0x4A307840` and tail-jumps
through `0x8000AF94` into **`0x8000F184`**, which clears MODULEMODE for the
whole CM2 L4SEC crypto block (`0x4A009580/5A0/5A8/5B0/5B8/5C0/5C8`, base
`0x4A009000`).  The enable twin is **`0x8000F108`** (MODULEMODE = 2, or 1 for
`0x5C0`).  The boot path enabled the clocks once at `0x80000248`; the tail
disables them before the jump.

So the seal/unseal SMC path has no clocked crypto block at chainloader entry.
Our mode-2 probe called `0x8000F108` first and it returned **1**.

---

## 5. Probe results

All three probes are in `boot/chainloader/chainloader_kernel.S.in`
(`BL_PROBE` = 1/2/3), built by `tools/build_kernel_image.py` (`BL_PROBE=<n>`).
The stash is the DTS no-map `pbmon-low` page `0xA0200000`; Linux reads it with
`peek a0200000 16` after `pkexec chmod 666 /dev/ttyACM0`.

### Mode 1 — dump (CRC `0xBD8966DD`)

```
a0200000: 424c4455  fffc8df0
```

`"BLDU"`, WDT2 CRR `0xFFFC8DF0`, then the copied regions.  It produced the
base mapping in §1.  The regions copied at the report's addresses read
`0xFFFFFFFF`; the region at `0x80009000` matched media `0x12000`.

### Mode 2 — selector call (CRC `0xA051A23C`)

```
a0200000: 50524f42 00000001 00000000 00000000
a0200010: fffc8df2 fffc8df3
```

`"PROB"`; `0x8000F108` returned **1**; the selector `0x800000CE` returned
**0** ("absent") with value `0`; WDT2 advanced one tick
(`0xFFFC8DF2 -> 0xFFFC8DF3`).  The call itself is therefore sound; the empty
catalogue is the wipe (§3).

### Mode 3 — L4SEC + mount + selector (CRC `0x984267D3`)

```
L4SEC 0x8000F108  -> mount 0x800082DC -> selector 0x800000CE
```

**This hung and reset the unit in a loop.**  The mount re-opens the MMC from a
wiped slot and then walks the store; the likely fault is the MMC controller
re-initialisation (`0x8000420C`) at a point where the bootloader's controller
state has already been torn down.  WDT2 (~7.8 s) reset the SoC each time, and
each iteration was another factory boot.  Roughly **10 counter units** were
spent (`0x32 -> 0x28`) before the loop was caught and the unit restored.

**Do not call the mount from the chainloader as-is.**  Any future attempt needs
a plan for the MMC controller state (and a way to abort), not just a call.

---

## 6. Bench cycle and restore

| # | action | result |
|---|---|---|
| — | TabletOS 2.1.0.1917, door up, `BOOTCTR_FCTRY = 0x32` | — |
| 1 | `BL_PROBE=1` flash + boot | dump returned; base = media `0x9000 -> 0x80000000` |
| 2 | `BL_PROBE=2` flash + boot | L4SEC ret 1; selector ret 0; no fault |
| 3 | `BL_PROBE=3` flash + boot | mount hung; unit reset-looped |
| 4 | sealed-IFS restore (`ifs0.sealed.bin`) | wrong asset: 2.0.0.4869 IFS vs 1917 RCFS; did not boot |
| 5 | **1917 IFS restore** (`local/work/ifs-shim-sealed.ifs`, 489 frames) | unit booted 2.1.0.1917, `/base` mounted |
| 6 | operator `startup.sh` | `bootctr: wrote 0x32, readback ok (was '(')` — counter was `0x28`, now `0x32` |

Final state: QNX 6.6 on 2.1.0.1917; `/dev/hd1t179 on /base type qnx6`;
`BOOTCTR_FCTRY = 0x32`; 1917 IFS sha256
`b52195bbf85b7618f9860ad73aebbae65932443b63335f57053020402b771861`
(`local/work/ifs-shim-sealed.ifs`).  The 2.0.0.4869 `ifs0.sealed.bin` is the
wrong IFS for this RCFS; do not restore it to a 1917 media set.

---

## 7. What this changes

* **Reports 150/151 (and any doc quoting their code addresses) inherit a
  `-0x9000` correction on code addresses.**  Their *analysis* of the code is
  sound — the bytes are the same — but the address labels are high.  Data
  literals and the `0x8001431C` table address are correct.
* **Report 151 §Q5's wipe-table anomaly is withdrawn.**  The table is valid;
  the wipe is a normal pre-boot clear of the bootloader's own parameter/RPMB
  state.
* **The call-the-bootloader's-writer plan is blocked by the state wipe**, not
  by the `0x2F` transform.  The bootloader's record machinery is deliberately
  gone before our chainloader runs, and its mount hangs when re-run there.
* **The Linux `pbmon` path is worth re-opening with the corrected evidence.**
  The bootloader seals with **domain 5** and the exact order
  `0x31 -> 0x33 -> 0x2F -> 0x33 -> 0x2E`; `pbmon` used domain 4.  The corpus
  is now correctly mapped, so a re-check of the seal with domain 5 (and the
  bootloader's exact descriptor order) is the next experiment.

---

## 8. Reproduce the base check

```sh
cd /home/psyden/SSDdrive/playbook-linux
. tools/env.sh
IMG=local/proprietary/device-binaries/emmc-first1mb.bin

# the executing dump matched these media offsets
python3 - <<'PY'
d=open('local/proprietary/device-binaries/emmc-first1mb.bin','rb').read()
print(d[0x12000:0x12020].hex())   # == probe DRAM 0x80009000
print(d[0x12078:0x120a0].hex())   # == probe DRAM 0x80009078
PY

# corrected code addresses (media = 0x9000 + (addr - 0x80000000))
python3 - <<'PY'
d=open('local/proprietary/device-binaries/emmc-first1mb.bin','rb').read()
for a in (0x800000CE,0x800000A0,0x8000F108,0x8000F184,0x8000F3DC,0x8000C8E4,0x80010224):
    off=0x9000+(a-0x80000000); print(hex(a), d[off:off+16].hex())
PY
```

On the unit, the media copy is confirmed with
`blk sumb 0 1048576` (FNV-1a64 `21c38ed917e0b38f`).

---

## 9. Sources

* Reports 150, 151 (the addresses corrected here), 143/144/147/148/156 (the
  seal ABI and the same-boot pair).
* `docs/reports/data/` is unchanged; the probe stashes were read live and are
  recorded above.
* `boot/chainloader/chainloader_kernel.S.in`, `tools/build_kernel_image.py`.
