# The boot chain, stage by stage

**Read `SAFETY.md` before acting on anything here.**  This page exists because
session 20 stopped a unit dead and the project now has **one working unit
left**: every claim below is marked with how it is known, and every proposed
step is read-only unless it says otherwise.

Marker convention: **[P]** proven from bytes or a measurement in the cited
report · **[M]** measured on the bench · **[I]** inferred (reasoning stated) ·
**[U]** unknown.

---

## 0. The chain in one picture

```
 eMMC user area, offset 0                     region 0 (raw 0x8000000)
 ┌──────────────────────────┐                ┌──────────────────────────────────┐
 │ boot TOC (LBA 0)         │  ROM loads it  │ +0x000000  region-0 MBR (64 KiB) │
 │  MLO     @0x4000         │ ─────────────► │ +0x010000  zeros                 │
 │  PRIMAPP @0x0C00         │                │ +0x100000  IFS  ◄── the slot     │
 │  KEYS    @0x0200         │                │            (+0x100 os_type)      │
 │  arm9000 @0x9000         │                │ +0xC00000  RCFS (500 MiB)        │
 └──────────────────────────┘                └──────────────────────────────────┘
 OMAP4 boot ROM 0x40028000  ──►  RIM MLO @0x80000000  ──►  IFS entry
 (authenticates, loads)          (RPMB selector, NVRAM gate, verify,
                                  counter decrement, then load+jump)
```

The TOC record is `{u32 offset, u32 length, …, char name[14] at +0x14}`,
measured (report 155 §5b): MLO `0x4000`/`0x4D70`, PRIMAPP `0xC00`/`0x32B8`,
KEYS `0x200`/`0x960`, then the `0xFFFFFFFF` sentinel.  **`os_type` is not at
region-0 `0x100`** (that byte is `0x61`, the `'a'` in the MBR's
`Missing OS Image` string); it is at IFS image `+0x100` = media `0x100100`.

| # | stage | where | what it does | reads / writes | evidence |
|---|---|---|---|---|---|
| 1 | **OMAP4 boot ROM** | `0x40028000` (reset `EA000242` → `0x40028910`) | reads `SYS_BOOT`, picks the medium from a 32-entry command table (`0x4002F7C4`), reads **LBA 0, 512 bytes** and scans it as **16 × 32-byte TOC records** (14 names: `CHSETTINGS…MLO…KEYS…`), stages the image and hands over with **`SMC #0`, `r12 = 0xF0`** — i.e. **to the secure world**.  It contains **no authentication code, no RSA/SHA, no certificates**; USB-boot (`'E'`/`'F'`) presents **VID:PID `0000:0000`** with a working `GET_DESCRIPTOR`; failure is an **infinite retry loop**, never a halt or reset; no GPIO/I²C/PMIC/LED | reads LBA 0 and the image; **writes nothing** | [P] report **152** §0 (full trace), 70, 71 §2 |
| 2 | **RIM MLO (Thumb) + `arm9000` (ARM)** | MLO at file `0x4000..0x8D70`; the ARM stage at file `0x9000..0x1E000` (`0x80009000`, its own vector table) | rebuilds the RPMB catalogue (`0x800112DC`), reads/validates `BOOTCTR_FCTRY` (`0x800090CE`), NVRAM gate, verifies the **region image it will jump to** (CRC-32 factory / pubkey), **decrements the record**, then jumps with `r0 = image entry` | **reads** RPMB + NVRAM; **writes** the record via the ARM dispatcher `0x800183DC` (SMC `0x31`→`0x33`→`0x2F`→`0x33`→`0x2E`, domain 5) then RPMB type 2/3/5 | [P] reports **150** and **151** (full trace), 71 §2/§3, 89 §3.5, 95 §4 |
| 3 | **IFS / chainloader** | region 0, media `0x8100000` (loader block `0x825`), **already resident in DRAM when `arm9000` jumps** | stock: QNX IFS → procnto → startup. Ours: an 828-byte loader at image `+0x800` (= `0x80100000`) that copies zImage/DTB high, checks the DTB magic `0xEDFE0DD0`, and calls the kernel with `r0=0`, `r1=~0`, `r2=DTB` | reads region 0 only | [P] report 109 §8.1, report 103 §5.2; report **151** §3 (the stage never touches `0x100000` itself) |
| 4 | **Linux kernel** | zImage at `0x84000000`, decompressed to `0x80008000` | initramfs `/init` (PID 1) → configfs ACM gadget → `/console`; optionally loop-mounts the region-1 root | reads region 1 (read-only) | [M] reports 140/142 |

**Stage 2 is the gate.**  Everything that can stop the unit happens there, and
it is the only stage that writes the persistent store.

---

## 1. Stage 2 in detail: the selector and the gate

`FUN_8000911A` (ARM, `hd0` image `0x8000911A`) reads the RPMB record
`BOOTCTR_FCTRY` and chooses a verify path **[P]** report 71 §2:

| record state | selector | path |
|---|---|---|
| **absent** | `0` | product: SHA-512 + ECDSA over region 0 (the original signed image) |
| **present, 4 bytes, value `1 … 0x32`** | `1` | factory: **CRC-32/ISO-HDLC over region 0 only**, and only when `region[0xD4] == 6` |
| present, value `0` or `> 0x32`, or length ≠ 4 | `2` | **failure**: `0x800094BE b .` — the unit halts with no OS |

Around it:

* **NVRAM gate** (`0x2040`, then `0x2801`): `A = [0x2801+0x18]`,
  `B = [0x2801+0x1C]`; the surviving reference is `A=0, B=10`, so it takes the
  **retry** branch — a failed verify **halts** and does not erase.  The
  **normal** branch (`A ≥ B`) nukes on a nonzero selector **[P]** report 71 §3,
  `SAFETY.md` §3.
  The normal-path nuke on a *verify failure* has never been observed
  (`docs/contradictions/expired-counter-nuke-vs-retry-halt.md`).
* **Verify scope**: region 0 only, once, no NVRAM A/B state: `desc[0xD4] == 6`,
  magic `0xC3D0E193` at `desc+0x9C`, `end-8 == 0xD7C82D1F`, `end-4 != 0xFFFFFFFF`,
  length ≥ `0x20000`, model ID `0x06001A06`, then CRC-32 over `[start, end-4)`
  compared with the stored word **[P]** `SAFETY.md` §3, report 89 §3.5.
* **The decrement is a write by the bootloader itself.**  A successful factory
  boot takes `0x32 → 0x31` **[M]** report 95 §4, report 89 §4.  The mechanism is
  visible in the stage's own service wrappers **[P]** report 146 §Q1.1f:
  `0x33` (key preload) → `0x2E`/`0x2F` (HMAC / block transform) → `0x32` (write
  frame).  **So the bootloader and the resmgr seal the same record with the same
  monitor services**, which is why a record written by either is readable by the
  other.
* **The switch itself** (absent → `0x32`) is done from a booted OS
  (`FACTORY-PATH.md` §3); the bootloader can only *provision* an absent record in
  a narrow "RPMB not provisioned" state and has no host-side write path.

## 2. Stage 3: the IFS slot

* Region 0 starts at raw `0x8000000` (**loader block `0x815`**); the IFS lives at
  region-0 offset `0x100000` = media **`0x8100000`** (loader block `0x825`) and
  is `0x2BFE10` bytes [M] report 134 §5, `SAFETY.md` §4.
* Region-0 offset 0 is a **64 KiB MBR** (vendor `PB-4869-root.0.0.mbr`,
  byte-identical to the live unit, report 155 §5b) with one QNX6 partition:
  **type `0xB3`, LBA `0x6000` = region-0 `0xC00000`, `0xFA000` sectors =
  500 MiB** — the RCFS exactly fills `0xC00000..0x20000000`.  The IFS at
  `0x100000` is outside any partition; the live `0x10000..0x100000` gap is
  zeros.
* The **only** write path this project uses for it is the pre-OS `0xF7` loop
  (boot ROM → `LDR_01` → `PreFlash` → `0xF7` frames → `0x40F9`); the loader
  checks only that the destination falls in an allow-listed MCT range and does
  **not** verify the payload [P] report 98 §2–§4, report 101.
* The loader container is the **V1 `qcfp`** record, whose offset is
  **region-relative** (`dest = 0x815 + offset`); the V2 `rrcq` is ignored here.
  A `-v 2` container is rejected `0x11`.  Patch both records [P] report 139,
  trap 55.
* Our chainloader replaces the stock IFS **in the same slot**; it is not a
  second OS.  That is why "flash a kernel" and "restore the IFS" are the same
  operation on the same blocks [M] every flash cycle this project has run.

## 3. What writes what (the store map)

| writer | service path | what it writes | notes |
|---|---|---|---|
| bootloader (stage 2) | SMC `0x33` → `0x2E`/`0x2F` → `0x32` | decremented `BOOTCTR_FCTRY` | every successful factory boot **[M]** |
| QNX `rpmb-omap4` | resmgr → SMC `0x33` → `0x2F` (seal) → MMC write | any record, incl. the top-up to `0x32` | the proven writer **[M]** |
| our Linux `pbmon` | SMC `0x33` + `0x2F` | nothing yet — the transform is not reproduced (report 148) | read-only monitors only |
| our Linux `pbrpmb replay` | device-level write frame, **no SMC** | re-sends a captured sealed block | **used once, session 20; see report 149** |

The record itself is a 224-byte plaintext sealed by the monitor (`0x2F`, op 0),
with a 32-byte block HMAC over the ciphertext (`seed[0:16]`) and the RPMB frame
MAC over the frame (`frame key`), both of which the project can compute
(report 143).  What it cannot yet do is *produce the ciphertext* (report 148).

## 4. The session-20 failure, read against this chain

Reports **150** and **151** supply the mechanism, and it fits the symptom
exactly **[P]**:

* the record reader/validator is `0x800090CE`; a record whose length is not 4,
  or whose value is `0`/`>0x32`, selects path **2**; a read error (I/O, name
  mismatch, `len > 4`) also selects **2**;
* the **decrement write** (`0x800115E6` → `0x800114FA` → `0x80010B2E`) runs
  **after** the region verify and **before** the jump, and it re-seals through
  the ARM dispatcher `0x800183DC` (SMC `0x31`→`0x33`→`0x2F`→`0x33`→`0x2E`) then
  writes RPMB type 2/3/5.  **If any of that fails, its return value is
  discarded** by the caller, boot main prints `"\nBootModeCount expended\n"`
  and halts at `0x800094BE`;
* **the halt is a bare `b .` with WDT2 armed and not kicked.**  The stage arms
  WDT2 (`0x4A314000`, `WCRR = WLDR = 0xFFFC1800` ≈ 7.8 s at 32.768 kHz) at
  `0x800094F0` and kicks it from every long loop, but not from the halt.  A
  WDT2 timeout is a **global reset** (TRM `CORTEXA9_WDT_RST`), so a failed boot
  becomes an **endless ~7.8 s reset loop** — not a dead CPU;
* `arm9000` programmes the MUSB core (`0x4A0AB000`: INDEX/CSR0/INTRTX/INTRUSBE
  = `0x27`) but **never the TLL (`0x4A0AE000`) or the PHY (`0x4A0B0000`)**, and
  it contains **no descriptors, no VID and no `GET_DESCRIPTOR` handler**.
  A link that comes up therefore presents a **full-speed device that cannot
  answer `GET_DESCRIPTOR`** — precisely what Linux (`-110`/`-71`/`-62`) and
  Windows CFP ("unknown descriptor") both reported.

**Resolved by report 153 (measured + static, 2026-10-31).**  The unit is **not**
reset-looping.  The red LED is driven by a **FAN5702 I²C LED driver** (7-bit
`0x36`, red channel) and the blink is **error code `0x5B` emitted bit-serially**:

| bit | emitted |
|---|---|
| `1` | ON 20 units, OFF 20 units |
| `0` | OFF 40 units |
| end | OFF 200 units |

`0x5B = 1 0 1 1 0 1 1` ⇒ gaps 60/20/60/20/220 units ⇒ the observed **1-2-2**
groups, and a **7344 ms** period against the measured **7347 ms** (±3 %).  `0x5B`
is the only code in the emitter that produces 1-2-2 [P] report 153 §Q2.

**`0x5B` means "NV-record / RPMB subsystem unusable"**, and it is emitted from
exactly four sites [P] report 153 §Q4: catalogue mount (`0x80014940`), RPMB
probe/provision (`0x80014958`), the `BOOTCTR_FCTRY` selector (`0x800090F6`) and
the counter re-seal (`0x80014A5A`).  Which one fired is **[U]** — the pattern is
identical.

The loop **does not reset the SoC**: `0x80014D04 b.n 0x80014CB4` re-enters, and it
kicks WDT2 100× per unit (≈48 000 kicks per cycle), so the watchdog never
expires either [P] report 153 §Q5.  And because `arm9000` never brings up a USB
device that can answer descriptors (§1 above), **the unit is unreachable** — the
pre-OS flash loop has nothing to talk to.  The documented recovery is the eMMC
route.

Two consequences for the survivor:

1. **Every boot is a write that can fail.**  The loader's own decrement write
   runs after the verify, its error is discarded, and a failure lands in this
   same class of terminal state.
2. **`0x5B` is a diagnostic worth remembering**: 1-2-2 red blink + no USB =
   record store unusable, not a display or power fault.
3. **If a dead unit ever does appear as USB, look for `0000:0000`.**  The ROM's
   USB-boot device has a zero VID/PID template (report 152 §3.3) and *does*
   answer `GET_DESCRIPTOR` — so a unit sitting in ROM USB boot looks like
   "nothing at all" to `lsusb`, not like a RIM device.

## 4b. The ROM image: we now have a dump, but is it *our* ROM?

The operator supplied an OMAP4430 boot-ROM dump (`boot.rom.tar`,
`omap4_boot_rom.tar` from an XDA thread, dumped by a kernel module that reads
**`0x40028000`, `0xC000` bytes** — `local/proprietary/device-binaries/omap4_boot_rom.c`).
It is kept at `local/proprietary/device-binaries/bootrom-0x40028000.bin`
(gitignored payload) and it is plausibly genuine: it opens `4202 00ea` with a
vector table whose literals are `0x1cf48309, 0x4030D004, 0x4030D008, …`, matching
the trampolines report 150 found in the bootloader.

**Measured on the surviving unit (2026-10-31): the dump is byte-identical to
our unit's ROM — 49 152 of 49 152 bytes, zero differing** (`memdump3 40028000
c000` from a booted TabletOS; the raw dump is kept at
`local/proprietary/device-binaries/bootrom-ours-0x40028000.txt`).  So report 152's
analysis applies verbatim to the PlayBook, and the SRAM API stubs at
`0x4030D000` are readable too
(`local/proprietary/device-binaries/sram-api-0x4030d000.txt`).

**The GP/HS question is *not* resolved by that match, and the ROM does not answer
it.**  The PlayBook's own inventory says `Processor_Type::HS`, but that is an
OS-provided string.  What the bytes say [P]:

| marker | hits in our ROM |
|---|---|
| RSA public exponent `0x00010001` | **0** |
| `CertPK` / `CertPPA` / `CertISW` strings | **0 / 0 / 0** |
| `SECURE` string | **0** |
| certificate magic `0xB5A60BFD` | **0** |

So the *readable* ROM carries no secure-boot path.  Report **152** §2.8 explains why:
the ROM **hands off with `SMC #0` (`r12 = 0xF0`) to the secure world**, so the
authentication is done there (and the ECDSA verification this project measured
lives in **RIM's `arm9000`**, report 151 §2).  That is also the clean answer to
the GP/HS question: **the 48 KiB ROM is generic and identical on both**; the
difference is the secure-world image and the eFUSEs, which we cannot read from
the non-secure world.  The `Processor_Type::HS` string is therefore not
contradicted by the dump — and not confirmed by it either.  **[I]**  Whether the silicon is a GP or
an HS part is therefore a question about the mask ROM's *other* regions and the
eFUSE/monitor state, not about the 48 KiB we can read.  Do not let the "HS" label
justify or forbid anything on its own; use the measured chain.

**The decisive check is read-only and cheap, and it doubles as a GP/HS probe.**
It was run on the surviving unit (the byte-identical result above); this is how
to repeat it from a booted TabletOS (no boot, no flash):

```bash
tools/pbssh.sh 'cat > /tmp/memdump3' < /tmp/memdump3   # rebuilt from tools/memdump3.c
tools/pbssh.sh '/tmp/memdump3 40028000 c000' > /tmp/rom_ours.txt
tools/pbssh.sh '/tmp/memdump3 4030d000 100'  > /tmp/api_ours.txt   # the SRAM API stubs
```

* **same bytes as the dump** → the XDA image is (at least flow-) representative;
  then the GP/HS question is answered "no difference in the readable part";
* **different bytes** → we have the real HS ROM and must re-run the analysis
  against it;
* **SIGBUS / L3 firewall abort** → the ROM is protected from non-secure reads on
  this unit, which is itself an HS signature (the dump above was taken from a GP
  Android device, where it was readable).

Do not read it from a Linux image on this unit yet: the survivor runs TabletOS
and a reboot means flashing (see §5).  QNX `memdump3` is read-only and needs
neither.

## 5. Read-only protocol for the remaining working unit

No step below changes anything on the unit.  Run them in this order and stop at
the first surprise.

1. **Identify the unit**: `tools/pbssh.sh 'uname -a'` (QNX) and the hardware PIN
   (`0x50175127` is the reference, the one working unit; `0x50176EC1` was the
   spare, now down).
2. **Read the store, do not write it**: `ls /dev/rpmb/BOOTCTR_FCTRY`; if present,
   `dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1`.  An **unarmed** unit reads
   *absent*; an armed unit reads `32 00 00 00` (our reference was armed during
   session 21, so it now reads the latter).  Record the exact bytes
   (or the exact `No such file` message).
3. **Read region 0's state**: the IFS at media `0x100000` (= raw `0x8100000`),
   `os_type` at IFS `+0x100` (= media `0x100100`), CRC at `end-4`; hash the whole
   `0x2BFE10` bytes (the exact-length `dd`, trap:
   `bs=16 skip=65536 count=180193`).
4. **Read the NVRAM gates** (`0x2040`, `0x2801+0x18/0x1C`) with `nvram read`
   only — `SAFETY.md` §1.
5. **Never** run `TEST_PAD`-style rehearsals again on a unit you cannot afford to
   lose, and never write the record from Linux.

**Session-21 result (report 155):** steps 1–4 were run on the reference and all
matched — `BOOTCTR_FCTRY` **absent**; IFS sha256 **`75be24e8…`** over exactly
`0x2BFE10` bytes; `os_type` **4** at media `0x100100`; CRC **`0x8D46FB4A`**;
`end-8` **`0xD7C82D1F`**; NVRAM `0x2040` **absent** (the loader substitutes
`2` before the read), `0x2801` **A=0 / B=10**.  The vendor
`PB-4869-root.0.3.ifs` and `.0.0.mbr` are byte-identical to the live IFS/MBR.
**The `0x2040` precondition is therefore "absent (→ 2) or `1..8`"; only a
*present* `0` or `≥ 9` is the nuke path.**

**Ordering/result:** the pair *recapture* is **done** (report 156: a same-boot
block-`0x3B` pair; both MACs verify, so the `0x2E` oracle is answered).  The
`0x2F` transform still does not match AES/DES with the visible keys; validating
or sealing it needs our `pbmon` driver, i.e. our CRC-sealed Linux image.  The
unit is now armed, so that image can boot — what remains is the transform, not
the arming.  The `trustzone-omap4` pass-through shortcut was tried and is
**blocked** (report 157: the stock resmgr segfaults in libc on the `0x5001`
devctl), so it cannot replace arming either.

**Armed-state measurement (report 160; superseded for region 0 on 2026-11-01).**
Session 21 measured the record present (`32 00 00 00`) with region 0 still the
stock `75be24e8…` image (`os_type` 4, CRC `0x8D46FB4A`), **not CRC-sealed**.  The
operator then flashed the sealed `os_type` 6 IFS, so the unit is **armed and
sealed** and boots; the exact asset on media has not been read back.  Current
state: `STATE.md`.

For any *future* write experiment, the sequence must be: prove the transform on
a **read-only** pair first (report 148 §6), then a rehearsal whose content is
byte-identical to what is already on media, then — only with the operator's
explicit approval — the value change.

## 6. Open questions, each with the read-only test that closes it

| question | test |
|---|---|
| Does the boot ROM read the MLO from the user area offset 0 or from `boot0`? | **Evidence now points to the user area**: `emmc-first1mb.bin` (`dd if=/dev/hd0`, first 1 MiB) begins with the 3-record TOC (MLO/PRIMAPP/KEYS + sentinel), while region-0 offset 0 is the QNX6 MBR and `0x10000`+ is zeros (report 155 §5b).  A `boot0`/`boot1` dump would settle it. |
| Does the bootloader write the record with the *same* seal the resmgr uses? | the wrapper set (`0x33`/`0x2E`/`0x2F`/`0x32`) already says yes [P]; confirm by reading a record immediately after a factory boot and matching the block HMAC |
| Is the monitor's `0x2F` seal deterministic in (plaintext) only? | the identical `E(0)` block across two records is the evidence [P]; a decisive test needs a reproduced seal, report 148 §6.  A **same-boot pair** now exists (report 156, block `0x3B`) and both MACs verify, but the `0x2E` HMAC does not reveal the `0x2F` cipher (report 156 §4). |
| What exactly happened to the halted unit? | only readable if it enumerates again; otherwise the eMMC route (`DEVELOPMENT.md` §9) |

## 7. Sources

`SAFETY.md` §1–§4 · `FACTORY-PATH.md` §1–§4 · reports 70, 71, 89, 92, 95, 98,
101, 103, 109, 134, 139, 143, 144, 146, 148, 149, 150–155 (155 is the session-21
read-only sweep and the vendor-corpus cross-check).
