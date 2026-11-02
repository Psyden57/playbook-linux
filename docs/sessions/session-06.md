# Session 6 (absolute) — the CRC/factory boot path, the pre-OS IFS reflash, and the first chainloader

> Numbering: absolute **6** of 12. This is the last **pre-Linux** session, so it has
> no Linux-era number; the Linux work starts at absolute 7 (Linux-era 1). Its
> reports are `88`–`92` and `94`–`102` (`93` is the separate Cellebrite chat), its
> handoff is `HANDOFF-2026-10-26-next-session.md`.
> `docs/history/newdocs/session-notes/session-06.md` is a *different, older*
> session-6 note from the kexec-phase numbering — not this session. See
> `NUMBERING.md`.

Date / span: 2026-10-25 → 2026-10-26 (device logs are ART = UTC−3). The final
`0x2801`/Discord review was answered at the end, with no further device writes.

Unit state at the end: the spare 64 GB (`PIN 0x50176EC1`, BSN `1063043093`) booted
TabletOS from the CRC-sealed region-0 image `ifs0.sealed.bin` (`sha256 c72b0530…`,
`os_type=6`, CRC `0x116B2562`), media verified, `BOOTCTR_FCTRY` read `0x2D` after
the last test and was topped to `0x32`; the corrected `chainloader/kernel.img`
(CRC `0xA6FF2D25`) was built but **not flashed**. At handoff the spare was taken by
another session for its kernel test (`0fca:8001`, 5–10 s USB cadence), so the final
state is “last verified clean + device owned by another session”, not a live read
at the end. The reference 64 GB (`PIN 0x50175127`) was not touched.

## Goal of this session

Prove that the factory/CRC path can boot a self-built region-0 image, make a no-OS
recovery path (pre-OS IFS reflash) so iteration is safe, and get the first
chainloader to the point where the next session can put a clean Linux kernel behind
it. “Done” was: CRC path proven; `0xF7` reflash proven byte-identical; loader
identity/MCT/`0xDC` understood read-only; a state probe that survives the WDT2
reset; and a kernel chainloader built and diagnosed. Booting Linux was explicitly
**not** this session’s milestone. The operator had already ruled out kexec: it is
“not really in a good state, riddled with bugs, unstable and random”; the native
CRC path is the route.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. The CRC/factory gate, and what it actually verifies

- **Region 0 only, once, no A/B fallback.** `0x800142B0` reads the RPMB selector
  `BOOTCTR_FCTRY`; `cbz r0, 0x800142D0` sends 0 to the product path, nonzero does
  `region := 0; bl 0x80014148; return` — it never reads NVRAM `0x2074` and never
  tries region 1. [89 §3.5, 95 §5, 97 §0]
- **Gate `desc[0xD4] == 6` exactly**, where `desc = *(hdr+0x28) = hdr+0x2C`, so the
  byte is **image offset `0x100`** (media `0x100100`), not `hdr+0xD4`. The magic
  `0xC3D0E193` at `desc+0x9C` is at `hdr+0xC8`; the state probe’s later read of
  `hdr+0x7C/0x9C/0xD4` used the wrong base. [89 §1.3/§3.1, 102 §1]
- **CRC is CRC-32/ISO-HDLC** (`zlib.crc32`): init `0xFFFFFFFF`, reflected poly
  `0xEDB88320`, final XOR, over `[start, end-4)`, stored at `end-4`; the table
  generator at `0x80013ED4` reproduces the standard table and
  `"123456789" → 0xCBF43926`. The CRC covers the header including `+0xD4`, so patch
  the type **before** the CRC. [89 §4]
- **Image end is `LoadEnd` at `hdr+0x08`, not an entry point:** `0x803BF610` is the
  first byte past the image, `LoadEnd - LoadStart = 0x2BFE10` (2,882,064 B). Word 0
  is `0xEA0001FE` = ARM `b +0x800` → `0x80100000`, the effective entry. [89 §2, 91 §0.1/§4]
- The verifier also checks `desc+0x9C == 0xC3D0E193`, `desc[0] >= 0x20000`,
  `end-8 == 0xD7C82D1F`, `end-4 != 0xFFFFFFFF`, and the model-ID match
  `(desc+0x7C & 0xFF00FFFF) == (bootrom modelID & 0xFF00FFFF)` (`0x06001A06`); the
  NVRAM `0x2819` blocklist is the only live blacklist. [89 §3.3/§3.4]
- The **5-byte reseal** was proven on the spare: image `0x100: 04 → 06`, CRC
  `0x8D46FB4A → 0x116B2562`, the same TabletOS booted, `BOOTCTR_FCTRY` 50 → 49,
  `0x2074` and `0x2801` byte-identical, PPS `osregion` unchanged. [95 §2–§4]
- A successful factory boot **decrements** the counter (`0x8000911E` →
  `0x800115E6`); a failed verify does not. [89 §3.5, 95 §4]

### 2. The pre-OS IFS reflash — the recovery net

- `0xF7` writes region 0, `0xF8` region 1; destination =
  `allow-listed partition start + QCFM 'rrcq'.offset`, in **64 KiB blocks**, and the
  only address gate is `FUN_80100118` (64-bit
  `∃ region : start ≤ dest ∧ dest*BS + len − 1 ≤ (region.end+1)*BS − 1`). The
  allow-list is built from MCT types
  `0x1C,0x22,0x1B,0x1F,0x24,0x34,0x3B,0x3A,0x20,0x21` (+ `0x19/0x1A`); `0x1B` is
  `0x815..0x4804`, which contains the region-0 IFS at block `0x825`. [98 §2–§4]
- The loader **does not verify the payload**; the only gate is the range. `LDR_01`’s
  allow-list does **not** include `0x1E` (NVRAM); the Cellebrite build adds it.
  `0xE0` (MCT rewrite) and `0xF3` (EraseMCT) destroy the table and must never be
  issued. [88 §5/§7.6, 98 §2.5]
- Working sequence (proven): take the real `2.0.0.4869-IFS.0.signed` as template
  (header `0x10020`, V1 `qcfp` + V2 `pfcq(_type=8)`/`rrcq`, payload at `0x10020`),
  patch `rrcq.offset = 0x20` (path 1; `p34.start=0x805`, IFS block `0x825`),
  `Count=44`; upload `LDR_01`; `PreFlash 0x20EE` with the 36-byte descriptor
  (`[0]=0x15,[1]=0x28,[6]=0x02,[28]=0x01,[32]=0x02`) → this loader answers
  **`0x20DE`**, not `0x39`; 181 × `0xF7` frames `{u32 seq,u32 len,data}` with
  `0x3FEC` data → `0xDF` each; `0x40F9` with the **real 560-byte trailer** (last
  `0x230` bytes, starts `QNXH-OS-1`) → `0x4006`, and the loader may reset
  (disconnect = success); `0x40C0` COMPLETE may return `0x4015`/`0x14` but the write
  is already on media. [101 §1]
- The media readback was **byte-identical**: `sha256 c72b0530…` = `ifs0.sealed.bin`;
  TabletOS booted; counter 50 → 49; `0x2074` unchanged
  (`req=act=0, count=3, max=0`); the RFS/`/base` was intact. [101 §0/§2]
- **`bb10mt flash` cannot do this on the spare:** it builds `_type=8`/
  `rrcq.offset=0` and relies on the loader’s **path 2** to resolve a `0x39`
  partition (`PartitionID == 8`). The live MCT has **no `0x39` entries**, so path 2
  fails; path 1 would target `p34.start = 0x805` (the MBR). Our tool patches the
  offset; `bb10mt` has no option for it. [98 §2.3, 100 §1, 101 §3]

### 3. The live loader probe (read-only)

- `0xB1` → `0xC8`, `{0xFFFFFFFF, 0x00010000, 0x000EDA04}`: **BS = 64 KiB**,
  ~63.8 GB. `0xB4` → `0xD2`, Samsung `MCGAFA`. `0xD9` → `0xC9`, 240 B, magic
  `0x92BE564A`, byte-identical to `hd4.bin[0:240]`; the MCT is v1.24 and its CRC at
  `0xE8` is `0x0AE2202C` (valid). [100 §0/§1, 98 §4]
- MCT extents (64 KiB blocks): `0x34` MBR `0x805..0x814`; `0x1B` OS
  `0x815..0x4804`; `0x1E` OS NV (NVRAM) `0x4805..0x4844`; `0x36`
  `0x4845..0x4904`; `0x1C` FS (UFS) `0x4905..0xFFFFF`; `0x35` id 0 region 0
  `0x815..0x2804`; `0x35` id 1 region 1 `0x2815..0x4804`. **No `0x39`.** [100 §1]
- `0xDC {0x825, 0x2C}` → `0xBB454844`; `zlib.crc32(media[0x08250000:+0x2C0000]) =
  0x0283B97A`, and the 44-sector / `0x2BFE10` variants also do not match. **`0xDC`
  is not zlib CRC-32**; use it only as a relative fingerprint. [100 §3]
- `0xDE` action 0 → `0xFA`, `status=0, mode=1 (FACTORY), count=0x32`; this
  independently confirms the selector. Bootrom identity: `BR_ver 0x051B0014`,
  model `0x06001A06`, password `0000`, salt `1392dc9f0976a887`, iterations
  `0x16B2=5810` (matches `0x2801`). [100 §0/§2]
- `0xDE` actions 1/2 produce **no response** in every shipped loader; the setters
  are dead code. There is **no pre-OS writer for `BOOTCTR_FCTRY`**. [72 §3/§5,
  88 §6/§9.2]

### 4. The state-probe chainloader

- `probe.img` = `ifs0.sealed.bin` base + a 428-byte chainloader at image `0x800`,
  CRC `0xDB0ECED3`, `os_type=6`; flashed via `0xF7`. It ran, parked, the WDT2 reset,
  the recovery tool restored TabletOS, and the breadcrumbs were read with
  `memdump3`. [102 §1]
- All four pages (`0x90000000`, `0x94000000`, `0x88000000`, `0x9FE00000`) were
  identical and **survived** the WDT2 reset + bootrom + loader upload + recovery
  flash + TabletOS boot:

```
[0x00] 4C424B43 "KBLB"   [0x04] 000000DE parked
[0x08] 50424F42 "PBob"   [0x0C] A2AAAABA
[0x10] 00C50878 SCTLR: MMU/C/I off
[0x14] 60000193 CPSR: SVC, I=1, F=1, T=0
[0x18] 80100100 VBAR (our park loop)
[0x1C] 80054059 TTBR0 (loader value)   [0x20] 00000000 TTBCR
[0x24] 55555555 DACR (all manager)     [0x28] 800F0000 SP
[0x2C] 800FF800 loader jump-target global
[0x34] 800FF800 LoadStart [0x38] 803BF610 LoadEnd [0x3C] 800FF800 LoadBase
[0x40] 800FF82C MetricsPtr
```

[102 §1]
- The probe read `hdr+0x7C/0x9C/0xD4`; the real descriptor fields are at
  `desc+0x7C/0x9C/0xD4 = hdr+0xA8/0xC8/0x100`. The run still proved the **entry
  state**; the field read was the probe’s bug. [102 §1, 89 §1.3]
- LED blinked 1 (entered), 2 (state/header), then off; a UART3 marker was emitted
  (invisible without pads). [102 §1, 96 §1]

### 5. The kernel chainloader and the DTB-magic bug

- `kernel.img`: image `0x7D0864`, LoadEnd `0x808D0064`, CRC `0xA6FF2D25`
  (corrected); chainloader 428 B at `0x80100000`; zImage 5,223,233 B at image
  `0x2C0000` (load `0x803BF800`); DTB 87,321 B at image `0x7BB348` (load
  `0x808BAB48`); copies zImage → `0x84000000`, DTB → `0x844FB348`; jump `r0=0,
  r1=~0, r2=0x844FB348, pc=0x84000000`; zImage header 8 NOPs, `b` at `+0x20`,
  magic `0x016F2818`. Host structural/gate/CRC checks PASS; the ECDSA trailer-shape
  check FAILs, which is informational on the factory path. [102 §2]
- Flash: 509 × `0xF7` accepted; `0x40F9` → `0x4006`; `0x40C0` → `0x4015`; loader
  reset. The device showed a **~5–10 s USB connect/disconnect cadence**. [102 §2]
- Diagnosis after recovery: all four pages held
  `magic=KBLB, stage=0xE1, nonce="KERN"` — the chainloader’s **DTB-magic failure**
  path, not a kernel entry. The code compared the little-endian word against
  `0xD00DFEED`; the in-memory LE word is **`0xEDFE0DD0`**. Fixed and rebuilt; the
  corrected image was **not flashed**. [102 §2]
- The zImage in this image is 5,223,233 B; the clean 6.15.11 gzip zImage is
  ≈6.96 MB (session 7), so this was **not** the clean build. The most likely source
  is the earlier/kexec-era image — an inference from the size, not a hash. The test
  was about the chainloader, not the kernel. [102 §2, 103 §4.3]

### 6. `0x2801` / `0x2040` / `0x2074` — the boot gate

- `0x2801` is `NV_OSSTORE_PasswordStruct` (0x70 B), `0x2802` its reserve; `+0x18`
  is the failed-attempt counter **A**, `+0x1c` the maximum **B**. Gate:
  `Bc = clamp(B,3,10); return (Bc < A) ? 0 : Bc - A`. Live records are `A=0/B=10`;
  the iteration count and salt are **per-unit** (the audited record in `84` has
  `+0x0C=5060`; the spare’s bootrom reports 5810, `100`), and the digest at `+0x30`
  is the iterated SHA-512 over `LE32(counter) ‖ salt ‖ state` starting from
  `"0000"` (reproduced exactly for the 5060 record; `94` records the spare’s 5810
  digest as verified). The plaintext is not stored. [75 §2–§3, 84 §4, 94 §3,
  100 §0]
- Writers: initializer `FUN_80014DEE` creates `A=0/B=10`, blob `FF`; correct
  password `FUN_80014F5C` sets `A=0` and bumps `+0x20`; mismatch `FUN_80014F3C`
  does `A++`; normal boot `FUN_80014F0C` sets `A=max(A,10)`; the pre-boot
  trampoline `0x800159AC → FUN_80014E34` recreates/repairs an absent/invalid record
  **before** boot main, so the same boot takes the retry branch; the nuke
  `FUN_800156E8` deletes `0x2801`. [75 §4]
- Boot-main order: read `0x2040` (absent → pre-init 2), `tbb` 0/2 → gate, 1 → nuke,
  3..8 → normal; gate nonzero → `0x80014A1E` retry; normal path `A=10`, selector
  `0x8000911A`, `0x800149C2 cbnz` → **any nonzero counter nukes**. [75 §3]
- Exhaustion: the `0x0F` handler calls `FUN_800158A2`, which either bumps `0x2040`
  to 3 or runs the nuke; `0x2040=3` routes the next boot down the **normal** path
  (where `A=10` and a nonzero counter nukes). The stage/CFP/ramloader
  `0x0A/0x0F` share this verifier; the OS-side `PasswordManager` is the named
  service (its binary is not in the corpus, `84` §7). [75 §1/§4, 84 §7]
- `0x2074` is `NvOSRegionStruct` V3 (72 B), not a plain counter:
  `requested/actual`, per-region `boot_count`, `max_boot_count`, `epoch`, `flags`,
  version IDs. `switch()` sets `max=4` (a hard trap: four boots then the retry
  loop); the V2 `switch()` has a `_replace(reqested_region=…)` typo and cannot
  switch; `flags` bit 0 is RMW’d by the bootloader; `0x2074` is product-path only
  and the factory path never reads it. [90 §0/§2/§5–§6]
- The two nodes the operator asked about, `hd1t179`/`hd2t179`, are the **RFS
  partitions inside each 512 MB region** — type `0xb3` (t179) at LBA `0x6000`
  (`0xC00000`), `0xFA000` sectors, end `0x20000000`. They do **not** expose the
  IFS; the IFS is the raw region at `/dev/hd1`/`/dev/hd2` + `0x100000`. `/dev/hd0`
  sector 0 is the MLO header (`0x4000`, `0x4D70`, `"MLO"`), not an MBR. [94 §3]

### 7. `BOOTCTR_FCTRY` is RPMB, and the rules

- It lives in the eMMC RPMB record store, not NVRAM. `/dev/rpmb` is a **directory
  resmgr**: each record is a file; `open/read/write/unlink` only; no devctl.
  `BOOTCTR_FCTRY` must be exactly **4 bytes** with value `1..0x32`; `0`, `>0x32`,
  `0xFFFFFFFF`, or length ≠ 4 → selector 2 → halt on the retry branch. [92
  §0/§2/§5, 75 §3]
- Every successful factory boot decrements by one; failures do not. The
  bootloader’s `0x800090A0(1)` provisions an **absent** record at `0x32` only when
  `0x80010EB8` says RPMB is not provisioned; it does **not** repair `0xFFFFFFFF`.
  No shipped loader has a live setter. [92 §3.1/§7, 72 §3/§5]
- An OS-side write is `open(O_WRONLY)`, `lseek(0)`, `write(4)`; `>` / `>>` /
  `O_TRUNC` are **brick** operations. Rehearse on a sacrificial `TEST_PAD` first;
  read back exactly 4 bytes. The RPMB write counter is not userland-readable and
  each write costs one; do not loop. [92 §5.2/§4.4]
- `0x2801` is **not** the blocker: the live units are `A=0/B=10` (retry branch).
  The blocker is the counter value and the region-0 image. Do not patch `0x2801`
  to `A=10` while the counter is `1..0x32`. [76 §1/§6, 75 §5]

### 8. The entry state and the eMMC/payload consequences

- Hand-off at `0x800192A8 bx r0`: ARM state, SVC, IRQ/FIQ masked, `SCTLR.M=0`,
  caches untouched (expect C/I on), `VBAR` never written by the image,
  `TTBR0=0x8005D000`, `TTBCR.N=0`, `DACR=0x55555555`, `SP` restored from
  `0x8005A7C0`, `r0 = image base`, `r1–r3` undefined, no return, WDT2 counting.
  [91 §2–§3]
- The jump target is not a fixed literal: `0x80014148` writes `*(0x80025E74)` from
  the loaded descriptor, and any failure writes 0; `0x800192A0` loads it and
  `bx r0` at `0x800192A8`. [91 §2.2]
- The second-stage image loader is **not** in `hd0head.bin`: the only MMC image
  loader (`0x800044BA`) targets a 512 KB TI layout at `0x80000000`, has no caller,
  and cannot be what placed the 2.75 MB OS at `0x800FF800`. The payload must carry
  its own MMCHS2 PIO driver. [99 §3/§4]
- MMCHS driver state lives in IRAM at `0x40308AB8`–`0x40309140` (59-entry command
  table, per-controller/per-device records, step counters, completion callback
  `ctx+0x1B4`); it is not in the image and no reachable code initialises it. A
  stale callback is a jump to garbage in SVC. [99 §2.4/§4]
- The recommended standalone driver is MMCHS2 `0x480B4000`, PIO, 512-byte blocks,
  CMD0/1/2/3/7/9/16/8(EXT_CSD)/17/24 (+18/25/23), 200 kHz → 12–24 MHz, 4-bit; a
  2.75 MB region-1 → region-0 restore is ~1–2 s (multi-block ~0.7 s), well inside
  the watchdog if kicked. [99 §5/§8]
- Region 1 is the payload/backup store: sealed IFS backup at `0x000000`, payload at
  `0x01000000`, control/flag/scratch/ring in the last 1 MB (`LBA 0x1FF800`–
  `0x1FFFFF`). Total ~35 MB of 512 MB. [99 §6]
- The first Linux kernel can be **embedded in the CRC image itself** (the
  bootloader loads `LoadStart..LoadEnd` at `0x800FF800`), so no eMMC/region 1 is
  needed for the first boot; eMMC is for the one-shot/restore automation. [91 §6,
  97 §1, 102 §2]

### 9. Feedback, recovery and tools

- LED: FAN5702 enable = **GPIO1_13**, `GPIO1 0x4A310000`, `CLEARDATAOUT +0x190`
  (off), `SETDATAOUT +0x194` (on); colours need I2C4 `0x48350000`/`0x36`, which is
  clock-gated and PRCM writes abort from NS. Blink patterns are the only live
  channel if the rings are unreadable. [96 §1]
- RAM breadcrumbs: `0x90000000` + mirrors `0x94000000`, `0x88000000`,
  `0x9FE00000`; `KBLB`, stage, nonce; WDT2 warm reset preserves DRAM; a power-cycle
  wipes it. The kernel DEBUG_LL rings use `+0x80` count / `+0x100` chars in the same
  pages. [96 §2, 102 §1]
- UART3 `0x48020000` 115200 8N1 is writable, but the external pads were not found;
  do not rely on a visible console. A monitor UART capture ring was suspected but
  unverified. [96 §3]
- The pre-OS reflash is the recovery path and is independent of region 0:
  bootrom → `LDR_01` → `0xF7`. A bad region-0 write is recoverable; a counter at 0
  with no OS is not. [98 §9.5, 101 §0]
- Bootrom/loader USB: `0fca:0001` bootrom, `0fca:8001` ramloader, `0fca:8010`
  OS/RNDIS; the bootrom window is the first seconds; `SETMODE(1)`,
  `PASSWORD_INFO`/`SEND_PASSWORD(0000)`, `0xF009` setup `{0x80100000,238128}`,
  916 × `0xF004` 260-byte, `0xF00A`, `0xF005`, then one **further read**
  (`0b 01 01 00`) is what actually jumps; drain `0xB0`/`0xB5` to `0xD0` before
  channel-2 commands; `0xE9` needs a 4-byte zero payload; EP1/Sachesi Debug Mode
  is a dead end. [69 §3–§7, 100 §0]
- Host tools: `bbusb.py`, `probe_loader.py`, `pre_os_ifs_flash.py`,
  `89-crc_tool.py`, `build_crc_image.py`, `build_kernel_image.py`,
  `usb_watch.py`; QNX side `memdump3`, `nvram2` + `libgcc_s.so.1`, `on`; the QNX
  shell lacks `head`, `xxd`, `od`, `md5sum`, `cmp`, `bc`, `python` — use `tail`,
  `dd`, `strings`, and hash host-side. `/tmp` is wiped;
  `/proc/boot/libnvram.so.1` is a runtime symlink; `nvram2` loads stock
  `/lib/libnvram.so.1`. [HANDOFF-2026-10-26 §4, DEVICE-REFERENCE, 76 §1]
- SSH is tied to the BerryShell `hold` qconn door; `Connection refused` **is** the
  door; a stale holder must be killed by explicit pid (never `pkill -f`); one
  session may own the USB link. [HANDOFF-2026-10-26 §3, 96, 103 §3.1]

### 10. The `0x2801` A=0/B=10 question (Discord review)

The question: does `A=0/B=10` mean a password is set and was entered correctly, and
is `A=0/B=10` the stock 10-attempt wipe budget?

- **No, `A=0/B=10` alone is not proof that a password is set or was entered
  correctly.** It is the **full attempt-budget state**. It is produced by (a) the
  initializer `FUN_80014DEE` on an absent/invalid record (`A=0`, `B=10`, blob all
  `0xFF` = no password), (b) a correct `0x0F` password (`FUN_80014F5C` sets
  `A=0`), or (c) the pre-boot trampoline `0x800159AC → FUN_80014E34 → initializer`
  recreating an absent/invalid record before boot main. [75 §4]
- A **normal** boot calls `FUN_80014F0C`, which sets `A = max(A,10)`; a normally
  booted unit is therefore `A=10/B=10`, and that is also the exhausted/normal
  marker. [75 §3]
- The gate is `A < clamp(B,3,10)`, not “a password is set”. Wrong attempts
  increment `A` (`FUN_80014F3C`); a correct password resets it to 0. [75 §3]
- Exhaustion: the `0x0F` handler calls `FUN_800158A2`, which either bumps `0x2040`
  to 3 or nukes. `0x2040=3` routes the next boot down the normal path; the actual
  nuke depends on the path (stage nuke vs OS `0xC008050C` clear-password). So
  exhaustion is not always an immediate wipe. [75 §4]
- The **10-attempt budget is stock**: `B` is clamped to `3..10`, live `B=10`, and
  the stage/CFP/ramloader `0x0A/0x0F` share the verifier; the OS-side
  `PasswordManager` is the named service but is not in the corpus (`84` §7).
  [75 §3, 84 §5/§7]
- On **these** units the record’s blob is non-`0xFF` and the digest is exactly the
  iterated SHA-512 of the password `0000`; so a password **is** set and `0000` is
  the password. `A=0` is the state a correct entry leaves — but the same `A=0` is
  the initializer/recreate state, so `A=0` by itself cannot distinguish “entered
  correctly this boot” from “record created/reset and never failed”. A normal boot
  would have set `A=10`. [76 §1, 84 §4/§6]
- The user’s second reading — “stock 10-attempt budget, shared by OS/CFP, expected
  wipe” — is essentially correct; the only nuance is the `0x2040=3`/normal-path
  routing above. [75 §4]

## Inferred (reasoning from evidence, not directly measured)

- The CRC/factory path is the only native route to a self-built image on this
  secure part; the product/ECDSA path cannot be satisfied without RIM’s key.
  Strength: strong (89/95/97). The counter is a finite, one-way budget, so the
  design must minimise factory boots. [95 §6, 116]
- The pre-OS `0xF7` reflash makes iteration safe **without** the counter: the
  recovery channel never depends on region 0. Strength: strong — the byte-identical
  reflash and the TabletOS boot are measured; “the channel is independent” is the
  code’s design. [98 §9.5, 101]
- The chainloader should own the A/B switch; `0x2074`/`resetqnxosregion` cannot
  point a CRC boot at region 1. Strength: strong (the factory path hardcodes
  region 0). [97 §0, 90 §5.4]
- The first kernel can be embedded in the CRC image, so region 1/eMMC is a later
  automation step, not a prerequisite. Strength: strong (the bootloader loads
  `LoadStart..LoadEnd`; 91 §6). [91 §6, 102 §2]
- The 5–10 s USB cadence is a **reset loop**, not a flaky cable: a test image on
  media reset-loops when the WDT2 expires; the same cadence appears when the kernel
  early-resets. Strength: good at the time; later sessions measured the WDT2 window
  at 7.812 s (see Superseded). [102 §2, HANDOFF-2026-10-26 §7]
- The state probe’s breadcrumbs prove the DRAM pages survive the full recovery
  cycle; the entry state is exactly the `91` model. Strength: strong for that run.
  [102 §1]
- The `0x2801` live `A=0/B=10` is a stable fixed point because the retry path never
  calls `FUN_80014F0C`; the unit keeps taking the retry branch until a `0x0F`
  correct entry or an explicit write sets `A=10`. Strength: strong (the live record
  survived a reboot byte-identically). [76 §1.1/§3, 75 §5]
- The two live units use the same password scheme with different iteration counts
  (5060 and 5810); the record is a one-way verifier. Strength: exact 512-bit match
  for the 5060 record (84 §4), and `94` records the spare’s 5810 digest as verified.
  [84 §4/§5, 94 §3, 100 §0]
- `0xDC` is a range hash with the same allow-list, not a CRC; the bootloader’s CRC
  gate after reboot is the authoritative check. Strength: strong (the `0xDC` value
  does not match zlib or the image CRC). [100 §3]
- The `0x40C0` COMPLETE is not a durability signal; `0xDF` per frame only means
  “consumed”. Strength: strong (the write landed and booted despite `0x4015`).
  [101 §1/§5, 98 §9.4]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **kexec was the prior route and was dropped.** The operator: it is “not really
   in a good state, riddled with bugs, unstable and random”; the session went
   native/CRC instead. The kexec history is in `docs/history/`; do not re-open it
   as the primary route. [operator statement; `docs/history/docs/01_KEXEC_ARCHITECTURE.md`,
   `docs/history/docs/05_NEXT_STEPS.md`]
2. **`bb10mt flash` will not work on this unit.** It uses path 2 (`_type=8`,
   `rrcq.offset=0`) and needs a `0x39` MCT entry; the live MCT has none, and path 1
   with offset 0 would write the MBR. The fix is the host tool with
   `rrcq.offset=0x20`. [98 §2.3, 100 §1, 101 §3]
3. **`0xDC` is not zlib CRC-32.** Observed `0xBB454844` vs `zlib.crc32`
   `0x0283B97A`; the 44-sector and `0x2BFE10` variants also miss. Use it only
   before/after a write. [100 §3]
4. **`PreFlash 0x20EE` does not answer `0x39`.** This loader answers `0x20DE`; the
   Pascal client’s expectation is for a different build. Accept `0x20DE`. [101
   §1/§5]
5. **The static `qcfm.pas` dummy signature is not enough.** `0x40F9` must carry the
   image’s real 560-byte trailer; the dummy yields `COMPLETE 0x4015`/`0x14` and
   fails. With the real trailer the loader may reset — that is success, the write
   is on media. [101 §1/§5]
6. **`0x40C0` COMPLETE is unreliable.** `0x4015`/`0x14` did not prevent the write
   or the boot; the bootloader’s CRC gate is the authoritative check. [101 §1/§5]
7. **`reboot_loader()` is unreliable.** The loader often stays at `0fca:8001`; use
   `0x80EF` then `0xEF`, or a power-cycle. The tool’s own reboot is not the final
   step. [101 §1/§5, HANDOFF-2026-10-26 §4]
8. **The DTB magic is `0xEDFE0DD0` as a little-endian word, not `0xD00DFEED`.**
   The first kernel chainloader compared against `0xD00DFEED`, parked at stage
   `0xE1`, and never entered the kernel. This cost the first kernel attempt; the
   fix is one constant. [102 §2]
9. **The state probe read the wrong header offsets.** It read `hdr+0x7C/0x9C/0xD4`;
   the verifier’s descriptor is `desc = hdr+0x2C`, so the real fields are
   `hdr+0xA8/0xC8/0x100`. The probe’s entry-state data is still valid; the header
   fields are not. [102 §1, 89 §1.3]
10. **`0x803BF610` is not an entry point.** It is `LoadEnd` (first byte past the
    image); the effective entry is `0xEA0001FE` → `0x80100000`. The earlier
    plan/`85`/`87` called it entry; that is wrong. [89 §0.2/§2, 91 §0.1]
11. **“Modified image in region 1, CRC-valid original in region 0” cannot work.**
    The factory path verifies region 0 **once** and never reads region 1; `0x2074`
    is not consulted. Region 1 is dead weight to the factory bootloader. [94 §0,
    95 §5, 97 §0]
12. **`resetqnxosregion`/`0x2074` cannot switch a CRC boot.** It is a product-path
    control; it cannot point the factory boot at region 1, and it cannot make the
    product path accept an unsigned image. [97 §0, 90 §5.4]
13. **`0x2801` is not a time/recovery record.** It is the device-password verifier;
    `0x2802` is its reserve. The “recovery/time condition” language in `69`/`71`
    was a misreading of the attempt budget. [75 §1/§8]
14. **`A=0/B=10` does not mean “a password was entered correctly”.** It is the
    full-budget state, reachable from the initializer (blob `FF`), a correct
    password, or a pre-boot recreate; a normal boot sets `A=max(A,10)`. See §10.
    [75 §3/§4, 76 §1]
15. **Do not restore `0x2801 +0x18 = 10` while `BOOTCTR_FCTRY` is `1..0x32`.**
    `A=10` takes the normal path, where `0x800149C2 cbnz` nukes on any nonzero
    counter. The retry branch requires `A < B`. [75 §3/§5, 90 §6.1.6]
16. **`0x2004 == 0xAAAAAAAA` does not skip the whole chain.** It skips the second
    (`0x8001453C`, mode-1) loop after the first verifier returns 1; the first
    verifier and the selector still run. [70 §6, 71 §2]
17. **The `0x2801` writers in `LDR_01` are dead code.** `FUN_8011DD20`/
    `FUN_8011DDEC` have no callers/pointers; the bootloader’s internal writers are
    the only pre-OS ones. [75 §4.3]
18. **`0xDE` action 2 cannot set the factory counter.** Every shipped loader
    accepts action 0 only; actions 1/2 produce no response. There is no pre-OS
    counter writer. [72 §3/§5]
19. **`0x2074` switch traps.** `Platform.switch()` sets `max_boot_count=4`; with an
    empty region 1 the unit boots exactly four times and then hangs in the retry
    loop on the fifth. The V2 `switch()` has a `_replace(reqested_region=…)` typo
    and never writes. `sane()` does not zero the counters or the epoch. [90
    §2/§4/§6.1.2]
20. **The bootloader’s MMC code cannot be called from the payload.** No callers for
    the public entries, the IRAM state at `0x40308AB8`–`0x40309140` is not in the
    image and no reachable code initialises it, and the completion callback is a
    stale function pointer. Carry a standalone PIO driver. [99 §4]
21. **UART3 pads were not found.** The 2-hour hunt in the kexec phase failed; do not
    plan on a visible console. The LED and the DRAM rings are the channels. [96 §3,
    `docs/history/docs/05_NEXT_STEPS.md` — the kexec project's task list, prior art]
22. **EP1/Sachesi Debug Mode is a dead end.** The first EP1 OUT is accepted, nothing
    comes back, and the endpoint stops being serviced; normal EP2 works. [69 §7]
23. **`0xE9` needs a 4-byte zero payload.** Without it the loader returns
    `0x15/0x04`, which looks like “unimplemented” but is the handler’s argument
    check. [69 §6/§8]
24. **Drain the `0xB0`/`0xB5` log before channel-2 commands.** `LDR_01` pages `0xB5`
    until `0xD0`; if not drained, every later response is a stale page. [69 §5]
25. **A test image on media reset-loops.** The WDT2 expires and the unit re-enters
    the bootrom/loader; a 5–10 s USB cadence is the loop, not a cable. The capture
    you read is the last boot, not necessarily the one you triggered. [102 §2,
    HANDOFF-2026-10-26 §7]
26. **The “~55 s” reset is not the Linux-path WDT2.** QNX’s own `LDR=0xffe2b400`
    is ~58.6 s; the chainloader/Linux path was later measured at
    `LDR=0xfffc1800` → 7.812 s. See Superseded. [109, session-10]
27. **`0x2801` `A=0` is sticky** until a correct `0x0F` or an explicit write; it is
    not a one-shot flag. The retry branch never calls `FUN_80014F0C`. [75 §5]
28. **The 5-byte reseal is not enough for a payload swap by itself** — it proves
    the gate; a modified payload must also preserve the header fields, the trailer,
    and the CRC order. [89 §6, 95]
29. **`0x2074` is not a plain boot counter.** It is `OsRegionV3` with
    requested/actual, per-region counts, max, epoch, flags, version IDs;
    `boot_count_0` advanced `0x017B → 0x017C` on a boot in the audit. [76 §4, 90 §1]
30. **The Cellebrite loader is not a repair route.** `06001A06_0000000A` has no
    `0x39/0x3A`, no `0xC4/0xDD`, no RPMB `BOOTCTR` writer, and its `0xDE` is
    read-only; it cannot fix the expired 32 GB units, and a damaged RPMB blocks even
    its eMMC write path. [88 §0/§9]

## Unknown / open questions

- **Who loads the OS image to `0x800FF800`?** No code in `hd0head.bin` does; the
  only MMC loader targets a 512 KB TI layout at `0x80000000` and has no caller.
  Resolve with a payload that publishes MMCHS1/MMCHS2 `PSTATE/SYSCTL/REV` at entry.
  [99 §3/§9 U1]
- **Is the MMCHS2 functional clock still on at payload entry?** PRCM `CLKCTRL`
  writes abort from NS; if the clock is off and cannot be re-enabled, the
  standalone driver cannot work and the region-1 scheme collapses. Read
  `CM_L3INIT_MMC2_CLKCTRL` at entry / probe `REV`/`CAPA`. [99 §9 U2]
- **What is `0xDC`’s exact hash?** Not zlib; the handler calls the same gate but
  computes the hash before it. Reverse the pre-gate code, or use it only
  relatively. [100 §3]
- **`0x40C0` `0x4015`/`0x14` semantics, and whether the real-trailer `0x40F9` reset
  is the signature write or a post-write reset.** The media and boot are correct;
  the codes are not decoded. [101 §5]
- **`0x205A` (per-OS-type model-ID override) and `0x2819` (OS blacklist)
  contents.** `0x205A` was never dumped; `0x2819` is read in the verifier. Resolve
  with a read-only `nvram read`. [89 §3.3/§3.4, 90 §9]
- **Which NVRAM copy the boot stage reads (insecure vs secure).** `nvram patch`
  writes both; a readback of the insecure copy does not prove the bootloader saw
  it. [90 §9, 92 §9]
- **`0x2040=3` exact routing and the actual nuke path.** `FUN_800158A2` either bumps
  to 3 or nukes; `0x2040=3` is normal-path. The nuke vs `0xC008050C`
  clear-password distinction is not fully traced. [75 §4]
- **The `0x0F` host command mapping on a real unit** (which host sequence reaches
  `FUN_80012182` case `0x0F`), and the exact setter recipe (`salt`, `+0x30`,
  `+0x20` bump). `PasswordManager` is on the RCFS, not in the corpus. [75 §8,
  84 §7/§9]
- **Who writes `0x2004`.** `0x2004 == 0xAAAAAAAA` skips the second verify loop; the
  writer is unknown. [70 §6]
- **`0x80009BF4` semantics** (`(0x1F,3)` then `(0x1F,1)` immediately before
  MMU-off) and the hidden `0x40026F28–0x40027FFF` page. No secure/JTAG path on this
  host. [91 §9 Q1, 30 §5, 64 Part 3]
- **The exact chainloader WDT2 window on the Linux/chainloader path.** At this
  session it was assumed ~58.6 s from QNX; later measured 7.812 s
  (`LDR=0xfffc1800`). [see Superseded; 109]
- **Whether a Linux payload can control WDT2** (kick or disable). Later sessions
  kicked it from a `timer_list`; the chainloader itself does not touch it. [109,
  10]
- **The 32 GB bricked units.** One is an NVRAM wipe, the other an invalid RPMB
  flag; the Cellebrite loader audit gives no repair, and the agent must not write
  RPMB. Physical eMMC access remains the route. [88 §9.2, 92 §6]
- **The Cellebrite modified-ramloader track** continues in the separate chat; the
  deliverable is `docs/reports/93-cellebrite-modified-ramloader.md` and the handoff
  `HANDOFF-cellebrite-modified-ramloader.md`. [86, 88, 93]

## Superseded / uncertain

- **The WDT2 window on the chainloader/Linux path was believed to be ~58.6 s.**
  At this session the QNX live read (`LDR=0xffe2b400`) and the `--delay` recovery
  practice were transferred to our path, and the handoff says “stop kicking → warm
  reset in ~58 s”. Later sessions measured the chainloader path at
  `LDR=0xfffc1800` → **7.812 s**, re-armed 7 ms before the chainloader; the
  “~55 s” is the restore flasher catching a later iteration of a 7.8 s reset loop.
  Current: `109` §1.1/§8.1, `session-10.md`. [109, session-10]
- **`0x803BF610` was called the entry point** in `85`/`87`; it is `LoadEnd`.
  Current: `89` §0.2/§2, `91` §0.1. [89, 91]
- **`0x2801` was called a time/recovery record** in `69`/`71`; it is
  `NV_OSSTORE_PasswordStruct`, the device-password verifier. Current: `75` §1, `84`.
  [75, 84]
- **“Modified region 1 + CRC-valid region 0 fallback”** in `85` Phase 3/4 is dead:
  the factory path is region 0 only, once, no fallback. Current: `94` §0, `95` §5,
  `97` §0. [94, 95, 97]
- **`0x2074`/`resetqnxosregion` as the switch** is dead: it is product-path only.
  Current: `97` §0. [97]
- **`0xDC` as zlib CRC-32** is wrong; it is a relative fingerprint. Current: `100`
  §3. [100]
- **Path 2 / `0x39` / `bb10mt flash`** is unavailable on this unit; path 1 with
  `rrcq.offset=0x20` is the route. Current: `100` §1, `101` §3. [100, 101]
- **`0x2004 == 0xAAAAAAAA` skips the whole chain** is wrong; it skips the second
  loop. Current: `70` §6, `71` §2. [70, 71]
- **`A=0/B=10` means “a password is set and was entered correctly”** — not proven;
  it is the full-budget state, reachable from the initializer/recreate/correct
  password. The live units do have a set password (`0000`, non-`FF` digest), but
  `A=0` alone does not distinguish entry from creation. Current: `75` §3/§4, `76`
  §1, `84` §4. [75, 76, 84]
- **`A=10/B=10` is “normal”** — yes, and it is also the exhausted marker; the
  normal path sets `A=max(A,10)`, and with a nonzero `BOOTCTR_FCTRY` it reaches the
  nuke at `0x800149C2`. Do not restore `A=10` while the counter is `1..0x32`.
  Current: `75` §3/§5. [75]
- **The 10-attempt budget is stock and shared by OS/CFP** — essentially correct;
  `B` is clamped `3..10`, and the OS `0x0A/0x0F` and the boot stage share the
  verifier. Exhaustion is not always an immediate wipe (`0x2040=3` may route the
  next boot normal). Current: `75` §4. [75]
- **The device-side `startup.sh` top-up worked** — later found broken: `printf`
  does not exist on TabletOS, the script wrote `0x00000000` six times per boot
  (refused as out-of-range), and its readback could not distinguish “write landed”
  from “nothing changed”; the counter really drains (`0x32 → 0x28` over ~10 boots).
  This session’s handoff gives a **host-side** `dd` recipe and a readback, but it
  does not record which writer actually performed the top-up; if the device-side
  script was used, it inherited the bug, and in either case the readback only
  proves the value, not that the bootloader read the secure copy. Current: `116`
  addenda, `session-10.md`, `session-11.md`,
  `docs/contradictions/bootctr-drain-vs-session10-readings.md`.
  [116, session-10/11, contradictions]
- **The `0x2801` live state was stable** — true across the reboot in `76`; but the
  later counter drain means the *budget* readings in this session’s handoff
  (topped to `0x32`) are a snapshot, not a guarantee. Treat `0x32` as “last
  verified”, not “static”. [76, 116 addenda]
- **The “sealed IFS” naming.** `ifs0.sealed.bin` (`sha256 c72b0530…`, `os_type=6`,
  CRC `0x116B2562`) is the **CRC-sealed** image; the original shipping image is
  `75be24e8…` / CRC `0x8D46FB4A` / `os_type=4`. Do not use “sealed” for the
  original. [95, 98, 101]
- **The kernel chainloader’s zImage was not the clean 6.15.11 build** (5,223,233 B
  vs the clean gzip ≈6.96 MB); the most likely source is the earlier/kexec-era
  image, inferred from the size. Session 7 built the clean zImage. [102 §2,
  103 §4.3]
- **The `0x2801` live A=0/B=10 is not proof the password was entered this boot.**
  The blob is a set password (`0000`), and `A=0` is consistent with a correct
  entry, but the initializer/recreate also produces `A=0` (with an `FF` blob, which
  this record is not). The record alone cannot timestamp the entry. [75, 84]

## Artefacts produced

- `docs/reports/88-cellebrite-ramloader-audit.md` — Track A: signed Cellebrite
  loader audit; no arbitrary eMMC read/write, no RPMB repair, `0x39/0x3A` absent,
  `0xDE` read-only.
- `docs/reports/89-ifs-crc-recipe.md` + `analysis/89-crc_tool.py` — IFS
  header/end/trailer, CRC-32/ISO-HDLC proof, reseal/verify tool.
- `docs/reports/90-ab-switch-runbook.md` — `0x2074` V3 bytes, `switch()`/`sane()`
  traps, boot-outcome table, recovery.
- `docs/reports/91-linux-entry-state.md` — `0x800192A8` hand-off, entry state,
  chainloader design.
- `docs/reports/92-rpmb-factory-write-runbook.md` — RPMB store, `/dev/rpmb`,
  `BOOTCTR_FCTRY`, write/readback/rollback.
- `docs/reports/94-collation-and-revised-plan.md` — A–E collation, the
  region-0-only correction, revised plan.
- `docs/reports/95-crc-path-proof-2026-10-25.md` — the 5-byte reseal, boot,
  counter 50→49.
- `docs/reports/96-feedback-and-debug-channels.md` — LED/rings/UART3/loader logs,
  readback coupling.
- `docs/reports/97-region-architecture-and-live-usb.md` — factory hardcodes
  region 0, chainloader-owned switch, live USB.
- `docs/reports/98-loader-write-and-autoloader.md` — `0xF7/0xF8` path, allow-list,
  QCFM, pre-OS reflash recipe.
- `docs/reports/99-payload-emmc-access.md` — hd0 MMC driver audit, “do not call
  it”, standalone MMCHS2 PIO design, region-1 layout.
- `docs/reports/100-loader-probe-2026-10-26.md` — BS/MCT/`0xDC`/`0xDE` live probe.
- `docs/reports/101-pre-os-ifs-reflash-2026-10-26.md` — proven end-to-end path-1
  reflash, byte-identical.
- `docs/reports/102-state-probe-and-kernel-chainloader.md` — state probe,
  breadcrumbs, kernel chainloader, DTB-magic bug.
- `docs/reports/HANDOFF-2026-10-26-next-session.md` — the handoff to the
  clean-kernel session; `DEVICE-REFERENCE-2026-10-26.md` — the QNX/device tool
  reference.
- `analysis/`: `89-crc_tool.py`, `build_crc_image.py`, `build_kernel_image.py`,
  `pre_os_ifs_flash.py`, `probe_loader.py`, `usb_watch.py`;
  `chainloader/probe.S`, `probe.img`, `kernel.img` (corrected CRC `0xA6FF2D25`).
- Captures: `analysis/playbook-live-2026-10-25/` (reference),
  `analysis/playbook-live-spare-2026-10-25/loader-probe/` (spare);
  `ifs0.sealed.bin` (`sha256 c72b0530…`, CRC `0x116B2562`), the original image
  (`75be24e8…`, CRC `0x8D46FB4A`), the region sector backups.
- On the unit: `/accounts/devuser/nvram2` + `libgcc_s.so.1`, `memdump3`, `on`;
  `/proc/boot/libnvram.so.1` is runtime.
- The separate Cellebrite chat: `HANDOFF-cellebrite-modified-ramloader.md`; its
  report `docs/reports/93-cellebrite-modified-ramloader.md` is that chat’s output, not this
  session’s.
