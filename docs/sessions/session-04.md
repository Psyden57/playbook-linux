# Session 4 (absolute) — `loaders.dat` audit, the ECDSA `+0x9000` relocation, BOOT_MODE tooling, and the NVRAM-editor lead

> Numbering: absolute **4** of 12, **pre-Linux** (no Linux-era number). This
> session's reports are `72`, `73` and `74`; the 2026-10-21 handoff chain is
> `HANDOFF-2026-10-21-next-session.md` (the repo copy is session 5's
> consolidated version; see *Artefacts*). Session 5 (newer) covers the live
> `0x2801` audit, the password/PIN distinction and `0x2074`; see the attribution
> note under *Superseded / uncertain*. `NUMBERING.md`.

Date / span: 2026-10-21 (device logs are ART = UTC−3).

Unit state at the end: the working/reference unit (later identified as PIN
`0x50175127`) was booted in TabletOS with password **`0000`**; `0xDE` action 0
read `status=0, mode=0 (product), count=0`. The expired 32 GB unit was
non-booting and read the same `product/0` through `LDR_01`'s sentinel. **No
NVRAM, RPMB or flash writes were made in this session.** The NVRAM editor was
read and its static gate logic reversed, but it was not run; session 5's `76` is
the first live audit.

## Goal of this session

Start from `HANDOFF-2026-10-21-next-session.md` and close its open items:

1. parse `bb10mt/loaders.dat` for a loader that implements the `0xDE` action-2
   factory-counter write, then add it to `bb10mt`/`bbusb`;
2. settle the ECDSA key-pointer anomaly (`0x800187D0 -> 0x800143D9`,
   `*(0x8001B188) -> 0x800144E8`), the handoff's top product-path item;
3. add a usable BOOT_MODE read path to both host tool stacks.

The 2026-10-21 handoff still said **kexec was the working route** and the
bootloader thread was research. This session did not attempt a native Linux
boot; the native CRC pivot belongs to session 6. The NVRAM-editor capability
arrived at the very end and was documented for the next session.

"Done" was: the loader question answered with evidence, the ECDSA question
resolved, and the host tools able to read `status/mode/count` without CFP.

## Confirmed (measured, read in a file, or seen on the bench)

### 1. `loaders.dat` audit — no `0xDE` action-2 writer exists

- **Archive inventory.** `bb10mt/loaders.dat` is 5,884,641 B, SHA-256
  `37e34cb6…907f9`, header `BB10MT`, 82 declared entries. 81 extract and
  decompress with `TMyArcReader` + LZMA-alone. **LDR_00 is corrupt**: its
  `dataOffset` is `0xa`, inside the entry table (table ends `0xc36`); stored CRC
  `0xe092b8e5` does not match the bytes at `0xa` (`0xc06aed4b`), and the LZMA
  stream fails. Cause: `TMyArcWriter.AddFileFromStream` writes entry 0 at
  `0xa`, then `UpdateHeader` overwrites `6..0xc36` with the table.
  [`72` §1; `analysis/loader-audit-2026-10-21/parse_loaders.py`,
  `loaders.csv`]
- **PlayBook mapping.** `bb10mt.ini` maps `06001A06` (P100-16WF) to `LDR_01`;
  `LDR_01` is exactly `loader_06001A06-00.bin` (238,128 B, SHA-256
  `c7cc33c7…5393f`), the same image CFP embeds. `07001A06` (P150) maps to
  `LDR_28`. The `0c/0d/0e…1a06` P150 variants in `ramloader.pas` are not keys
  in this archive. [`72` §2]
- **Counter references.** Eleven entries contain `BOOTCTR_FCTRY`:
  `LDR_01, 13, 14, 15, 22, 24, 28, 29, 44, 47, 49`. Nine of them also contain
  `BOOTCTR_SIG`: `13, 14, 15, 22, 24, 29, 44, 47, 49`. All eleven carry
  `Fail to write RPMB`. [`72` §4]
- **Every `0xDE` handler accepts action 0 only.** The per-loader case addresses
  are in `72` §5 (`LDR_01 0x80103D50`, `LDR_13 0x80103BE8`, `LDR_24
  0x80103D60`, `LDR_28 0x80104060`, `LDR_44 0x80103D5C`, `LDR_14
  0x802040F0`, `LDR_15 0x80204144`, `LDR_29 0x8020414C`, etc.). Action 1/2
  logs and returns **without sending a response**; the host sees a USB timeout.
  `LDR_29` has an extra `action == 1 && value == 3 && (metrics[0x84] &
  0x10000)` pre-check, but its true branch still falls into the same
  `if (action != 0)` rejection at `0x80204300`; it is not a write path.
  [`72` §5]
- **The setters are dead code.** For every OMAP setter (`cmp value,#0x32` →
  read → RPMB write) there are zero `BL`/`BLX` callers and zero little-endian
  pointer references in the image; the decrement helpers are equally
  unreferenced. The Qualcomm builds contain only an unreferenced decrement.
  The record writer itself works (`0x80113D9C` is reached for record index 2
  at boot); only the index-0 `BOOTCTR_FCTRY` caller is absent. [`72` §5.1]
- **Conclusion:** there is **no pre-OS action-2 writer** in any recoverable
  `loaders.dat` entry. Adding a `0xDE` action-2 request to a host tool cannot
  recover or provision the counter. The general writer remains `/dev/rpmb` on a
  booted OS; the bootloader's `0x800090A0(1)` can provision an *absent* record at
  `0x32`, but it does not fire for `0xFFFFFFFF`. [`72` §6; `71` §7b]

### 2. `0xDE` read and the two live units

- **Protocol.** Channel 2, cmd `0xDE`, 8-byte `{u32 action, u32 value}`;
  response cmd `0xFA`, 12-byte `{u32 status, u32 mode, u32 count}`. Actions:
  0 read, 1 switch (1 = FACTORY, 0 = PRODUCT), 2 set count (1..50). Status:
  0 ok, 1 not allowed, 2 out of range, 3 wrong device mode. [`72` §3; `71`
  §7c]
- **Working unit read.** After `--from-bootrom` and the `0000` password
  challenge, `bbusb.py --boot-mode` printed:
  ```
  status=0 (ok)
  mode=0 (product)
  count=0
  ```
  Debug context: `metrics 736 bytes, BR_ver 0x051B0014, model 0x06001A06`;
  `set_mode(1): cmd 0x08 len=41` with the 41-byte `RIM-BootLoader` mode-info
  blob; `password_info: cmd 0x0E len=24` (attempts `0a 0a`, salt words
  `0xB9A043B5`/`0x222D86B5`, iterations `0x13C4` = 5060) then `cmd 0x10`.
  [operator console output; the same salt/iteration values appear in `76` §1,
  and `69` §3 gives the challenge layout]
- **Expired 32 GB read.** Same command on the non-booting unit:
  `status=0, mode=0 (product), count=0`. This is **not** the bootloader's view:
  `LDR_01`'s action-0 reader (`FUN_8011E074`) has an explicit
  `if (value == 0xFFFFFFFF) return 0;` sentinel, so the counter known to be
  `0xFFFFFFFF` is reported as "absent". The newer OMAP readers (e.g.
  `FUN_8011EF78` in `LDR_13`) and the bootloader reader (`FUN_8000911A` →
  `0x800090CE`) have no such case and return `2`. [operator console output;
  `72` §3.1; the `LDR_13` reader disassembly shows `FUN_8011EF78` goes
  `value == 0 || value > 0x32 → return 2`]
- **Unit identities (later notes).** The booted/reference unit is the 64 GB
  `PIN 0x50175127`; the spare 64 GB (`0x50176EC1`) is the factory/test unit and
  reads `mode=1 (FACTORY), count=0x32` in session 6 (`100` §2). This session did
  not read the PINs; the identification is from the later notes. [`76`,
  `100`, session-5/6 notes]

### 3. ECDSA key pointer — the image is pre-relocation

- **`hd0head.bin` is linked at `0x7FFF7000` and loaded at `0x80000000`; a
  load-time fixup adds `+0x9000`.** Absolute *literals* are 0x9000 low; branch
  and call targets are PC-relative and already correct at the runtime base.
  [`73` §0/§3]
- The four verifier literals all resolve after the same delta:
  - `0x800187D0` holds `0x800143D9`; `+0x9000` → `0x8001D3D9`, an uncompressed
    secp521r1 point that satisfies `y² = x³ − 3x + b (mod p)`.
  - `0x8001B188` holds `0x800144E8`; `+0x9000` → `0x8001D4E8`, a 17-limb
    integer equal to `p = 2^521 − 1` (top limb `0x000001FF`).
  - `0x8001B18C` holds `0x80014460`; `+0x9000` → `0x8001D460`, the 17-limb
    integer `3` (the `a = −3` term).
  - `0x8001B190` holds `0x800144A4`; `+0x9000` → `0x8001D4A4`, the exact
    secp521r1 `b` constant.
  The constants are contiguous at `0x1D460`, `0x1D4A4`, `0x1D4E8` — 0x44 bytes
  apart, the 17×u32 limb size the parser expects — and the key is immediately
  before them at `0x1D3D9`. [`73` §3; `check_reloc.py` output: `p == 2^521−1`,
  `b == secp521r1 b`, `const@0x1D460 == 3`, `key on curve`]
- **MLO header at `0x0`.** The three 0x20-byte records match the OMAP CH TOC
  item layout `{u32 start, u32 size, u32 rsv, u32 rsv, u32 rsv, char name[12]}`:
  `KEYS 0x200/0x960 → CertPK_`, `PRIMAPP 0xC00/0x32B8 → CertPPA`,
  `MLO 0x4000/0x4D70 → CertISW`. Each block is `{header, signed payload}`
  (`CertISW` 0x350 + 0x4A20; `CertPPA` 0x1DC + 0x30DC). The code starts at file
  offset `0x8000`, inside the 0x9000-byte prefix (cert directory plus padding);
  this is consistent with the +0x9000 link/load delta. [`73` §1]
- **Consequence:** the product path is **functional**. The working unit's
  `product/0` read and normal boot are consistent with the ECDSA verifier, not
  with a broken key pointer. The earlier "static path is broken" reading was a
  pre-relocation artifact. [`73` §4]

### 4. Host tooling

- **`bbusb.py` bootrom sequence** (now matching the Pascal tool):
  `Ping0` → `GetVar(2, 2000)` → `SetMode(1)` → `PasswordInfo` →
  `SwitchChannel` → `GetMetrics` → `SendLoader(0x80100000, 260-byte chunks)` →
  `RunLoader` → wait for `0fca:8001` → `SetMode(2)` → `PasswordInfo` →
  `BugdispLog` → `FlashRegionsInfo`. [`69` §3–§5; `bbusb/bbusb.py`]
- **Model ID offset.** `GetVar(2)` returns 736 bytes; `TBRMetrics` starts at
  payload `+4`, and `modelID` is at `TBRMetrics+0x0C`, i.e. payload **offset
  16**. The first Python attempt read offset 4 and got `0x00060018`; offset 16
  gives `0x06001A06`. [`69` §3 says the same; this session fixed the Python
  port]
- **Password challenge.** `0x0A` returns `0x0E` + a 24-byte payload:
  attempts at `[0]/[1]`, challenge at `[4:8]`, salt at `[12:20]`, iterations
  LE32 at `[20:24]`. The reply is `0x0F` + `00 00 40 00` + the 64-byte
  `HashPassV2` digest; success is `0x10`. The working unit's password is
  `0000`; the challenge values matched `76` §1 (`0xB9A043B5`/`0x222D86B5`,
  5060). [`69` §3; `bbusb.py:hash_pass_v2()`; `76` §1]
- **`HashPassV2` port:** `state = password`; repeat `iterations` times:
  `state = SHA-512(LE32(counter) || salt || state)`; after the last round,
  prepend the 4-byte challenge and run a second `iterations` rounds; the
  result is the 64-byte digest. This reproduces the record digest for `0000`
  (`84` §4) and the `0x0F` answer. [`ucrypto.pas:HashPassV2`; `84` §4/§5]
- **`set_mode` responses.** `0x08` is the success/transition response (41-byte
  mode-info blob on the working unit). `0x09` is also seen when a bootrom
  instance has already been used/abandoned; the code accepted 8 or 9, but `69`
  §1 says a used bootrom refuses SETMODE and needs a power-cycle. Treat 9 as
  stale, not as success. [operator debug output; `69` §1]
- **CLI.** `python3 bbusb.py --boot-mode --from-bootrom [--password 0000]
  [--debug]`; `--factory`/`--product`/`--set-count` require `--force` and time
  out on every shipped loader. `--debug` prints the raw `GetVar`, `set_mode`
  and `password_info` responses. `--help`/any argument now routes to argparse;
  the first version's `--help` fell through to the legacy flash flow, connected
  to the bootrom and left it in a stale state (still `0fca:0001`); a
  power-cycle was needed. [`bbusb/bbusb.py`; this session's operator report]
- **`bb10mt` changes.** `LOADER_CMD_BOOT_MODE = $DE`, action constants,
  `TBootModeInfo`, `TBBLoader.BootMode(Action, Value)`,
  `ParseBootModeResponse`, `BootModeStatusText`, `BootModeValueText`; the
  `bootmode` command (`--set-count`, `--factory`, `--product`, `--force`,
  `--loaders`, `--delay`) and a read-only Boot Mode line in `info`.
  [`72` §7; `bb10mt-main/bbloader.pas`, `uflash.pas`, `bb10mt.lpr`]
- **PyUSB.** `pyusb 1.3.1` installed user-local (`~/.local`) on Arch; system pip
  is PEP 668-managed, so `--user --break-system-packages` was used. `libusb-1.0`
  and the `MODE=0666` udev rule were already present. [operator environment;
  `bbusb.py` import]

### 5. NVRAM editor lead

- **Tool.** `nvram-editor/nvram` is an ARM/QNX binary (source `nvram.c`); it
  dlopens `/proc/boot/libnvram.so.1`, opens `/dev/nvram` `O_RDWR`, and exposes
  `read`, `partialread`, `patch`, `write`, `delete`, `delete_secure`,
  `get_permissions`, `is_protected`, `record_exists`, `get_bootrom_metrics`,
  `get_os_metrics`. It only works from the **booted OS**. [`74` §1]
- **Static `0x2801` gate.** `FUN_80014FA6` reads 0x70 bytes; `FUN_80014D88`
  writes them and mirrors to `0x2802` only on a primary-write failure.
  `FUN_80014F28` returns `[+0x1c]` (clamped 3..10); `FUN_80014F84` returns 0
  when `[+0x1c] < [+0x18]`, else the difference; `0x80014998` treats nonzero as
  the `0x80014A1E` retry/CRC loop. I read `+0x18` as a threshold and `+0x1c`
  as the max, and proposed `patch 0x2801 0x18 0` to force the recovery branch.
  [`74` §2; the correction is in §*Superseded*]
- **Caveats.** `patch` re-reads the live record, preserves its length, patches
  in place, then writes insecure and secure in that order; it returns early if
  the insecure write fails. `write` uses the supplied size as the **new record
  length**, so a 4-byte `write 0x2801 …` would truncate the 0x70-byte record.
  Use `patch`. `/proc/boot/libnvram.so.1` is a RAM/temporary symlink.
  [`74` §1; `76` §1; `78` §2.9]
- **Not run in this session.** The tool was read and the static gate was
  reversed; the first live audit is session 5's `76`, which found `0x2801
  A=0/B=10` and made the planned `+0x18=0` patch moot.

### 6. Core device facts this session worked from (inherited, not discovered here)

- **Boot ROM** at `0x40028000` (48 KB, NS-readable; entry `0xEA000242`, IRAM
  vector ladder to `0x4030D004+`; no UART pads found). [`10`, `30`, `61`,
  `64`, `BOOTROM-0x40028000-REPORT.md`]
- **Second-stage loader / media.** The eMMC second stage is loaded at
  `0x80000000`; the region-0 IFS is at media `+0x100000` and is loaded at
  `0x800FF800`; the image word 0 is `0xEA0001FE` (`b +0x800`) → `0x80100000`.
  Region 0 = `/dev/hd1`, region 1 = `/dev/hd2` (the MCT map and 64 KiB block
  size are later measured in `100` §1). [`70`, `71` §3, `89` §2, `91`,
  `100` §1]
- **NVRAM IDs (as understood here).** `0x2004` committed-image flag (read
  only; no writer in any available image), `0x2040` boot-mode/region counter
  (absent on the live unit; pre-initialised to 2), `0x2801`/`0x2802` the
  0x70-byte record, `0x2074` the OS-region/A-B record, `0x2002` the hardware
  PIN, `0x2819` the OS blocklist. [`71` §3; later `75`/`77`/`83`/`84` refine
  the names and layouts]
- **RPMB.** `BOOTCTR_FCTRY` is an authenticated RPMB record, exactly 4 bytes,
  1..0x32; the bootloader reads it through the secure monitor (services 0x12
  nonce, 0x2E MAC, 0x2F write, 0x31 counter, 0x33 key load). The only general
  writer is `/dev/rpmb` on a booted OS; 0/`0xFFFFFFFF`/length≠4 selects the
  product/failure path. The bootloader can provision an *absent* record at
  `0x32`. [`71` §7b; `72` §6; later `92`/`116`]
- **Verification.** The product path is SHA-512 + ECDSA-family secp521r1
  (`0x80014716` → `0x80018688` → `0x8001B194`); the factory path is CRC-32/
  ISO-HDLC over `[start, end−4)` at the trailer, gated by
  `region[0xd4] == 6`. Both are in `hd0head.bin`; neither executes an SMC.
  [`71` §2; `73` §2]

## Inferred (reasoning from evidence, not directly measured)

- **The `+0x9000` link base/fixup is real, not a coincidence.** Four
  independent literals land on a valid P-521 point, `p`, `b` and `3`; the
  numeric matches are exact (`p == 2^521−1`, `b` diff bits 0, key on curve).
  Strength: very strong. The exact agent/table that applies the fixup is still
  unknown (see below). [`73` §3]
- **The working unit's `product/0` is a genuine absent factory counter, not
  the `0xFFFFFFFF` sentinel.** The bootloader would select the failure path
  for `0xFFFFFFFF`, and the unit boots; `0x2801 A=0/B=10` (session 5's live
  audit) puts it in the retry branch, where selector 0 selects the now-known
  functional ECDSA path. Strength: strong, but it depends on session 5's live
  `0x2801` reading. [`73` §4; `76` §1; `75` §3]
- **The expired unit's `product/0` is the `LDR_01` sentinel.** The loader
  reader has the explicit `0xFFFFFFFF → 0` branch; the bootloader reader does
  not, and the unit does not boot. Strength: strong. [`72` §3.1; the
  `FUN_8011EF78` disassembly]
- **`cmd 0x09` from `set_mode` is a stale/used-bootrom refusal, not a second
  success code.** `69` §1 documents the refusal; the working unit's first
  attempt returned `0x08` then timed out at the password, and a later
  invocation returned `0x09`. Strength: good; the code's acceptance of 9 as
  success was a workaround, not a protocol fact. [operator debug output; `69`
  §1]
- **The expired unit is a 32 GB unit.** The later notes identify two bricked
  32 GB units and the two 64 GB units; the expired unit with `0xFFFFFFFF` fits
  the invalid-RPMB-flag brick. Strength: strong from the inventory, not from
  this session's own PIN read. [session-5/6 notes]

## Dead ends and wrong assumptions (valuable — do not skip)

1. **"The ECDSA key pointer is broken, so the product path always fails."**
   This was my first reading of `0x800187D0 = 0x800143D9`: the byte at
   `0x800143D9` is `0xF8`, which the SEC1 parser rejects, and the normal-path
   caller treats the parser error as the nuke branch. The working unit's
   `mode=product, count=0` and normal boot contradicted it. The resolution was
   the `+0x9000` relocation: the literals are link-time, not runtime. The false
   start cost the most time in this session; the numeric proof is in `73` §3.
   (The earlier `71` §5 text is preserved with a pointer to `73`.)
2. **"`0x2801` is a time/recovery record; setting `+0x18 = 0` forces the
   recovery branch."** This was inferred from `FUN_80014F84`'s comparison of
   `[+0x1c]` and `[+0x18]`, without the record's identity. Session 5's live
   audit found `A=0/B=10` already, and `75`/`84` proved `0x2801` is
   `NV_OSSTORE_PasswordStruct`: `+0x18` is the failed-attempt counter **A**,
   `+0x1c` the maximum **B**, and `A=0` is the full budget, not a threshold.
   The planned `patch +0x18=0` was a no-op at best. See *Superseded*.
3. **"The expired unit's `0xDE` read tells us its factory counter."** It
   returned `product/0`; I initially read that as "no counter", which
   contradicted the handoff's `0xFFFFFFFF`. The `LDR_01` reader's explicit
   `0xFFFFFFFF → 0` branch explains it, and the newer reader/bootloader return
   `2`. The read is not the bootloader's selector for that sentinel.
4. **`--help` ran the legacy flash flow.** The first `bbusb.py --help` had no
   argparse dispatch; it connected to the bootrom, sent `ping0`/`set_mode(1)`
   and timed out at `password_info`, leaving the bootrom in a stale state
   (still `0fca:0001`). Any argument now goes to argparse; the legacy flow only
   runs with no arguments. A power-cycle was needed.
5. **The Python bootrom flow used the wrong model-ID command and offset.**
   `get_model_id` (channel-1 `0xF00B`) returned 10 bytes; the reliable path is
   `GetVar(2)` and `modelID` at payload offset 16 (`TBRMetrics+0xC`). Reading
   offset 4 gave the version word `0x00060018`, not `0x06001A06`.
6. **The Python flow omitted `SwitchChannel`/`GetMetrics` and
   `BugdispLog`/`FlashRegionsInfo`.** The loader accepted the upload and ran,
   but the first `0xDE` read returned no response. Matching the Pascal
   `ConnectToBB`/`InitializeLoaderInterface` sequence fixed the channel/loader
   init. The loader also has a host-activity timeout: do the upload and the
   query in one run, and power-cycle if it is left idle.
7. **`set_mode(1)` returning `0x09` is not a success.** The working unit's
   first run returned `0x08` and then timed out at `password_info`; a later
   invocation (after that failed run) returned `0x09` with an 18-byte
   `RIM-BootLoader` echo on its first `set_mode`. The code accepted 8 or 9, but
   `69` §1 says a used bootrom refuses SETMODE (`0x09`) and needs a fresh
   power-cycle. The later password timeout is consistent with a stale bootrom.
8. **The linear Capstone `cmp #0xde` scan initially found no dispatchers.**
   `int("0xde", 0)` works but `int("#0xde", 0)` raises; the scan silently
   skipped every operand until the `#` was stripped. On the larger ARM images
   linear disassembly also desyncs across data pools; Ghidra headless was
   needed for authoritative decompilation. The secondary `cmp #0xde` sites in
   the 0x80129xxx/0x8012a7xx range are not command dispatchers and should not
   be read as `0xDE` handlers.
9. **The handoff's premise that some loader implements `0xDE` action 2 was
   false.** The setter is dead in every build; the only pre-OS counter write is
   the bootloader's `0x800090A0(1)` provisioning of an absent record, not an
   action-2 write. The task's expected "add the loader to bb10mt/bbusb" could
   not be satisfied; the tools were added for the read/protocol path instead.
10. **`LDR_00` cannot be audited.** The archive's first entry is corrupt from a
    `TMyArcWriter` header overwrite; it is a Qualcomm/Classic entry, not a
    PlayBook loader. Do not count it as "not checked but possibly action-2".
11. **The `0x2801` editor write test would have been redundant.** The planned
    read-only-first ladder assumed a normal `A=10`; the live unit was already
    `A=0` (session 5), so the write would not have proved what it was designed
    to prove.

## Unknown / open questions

- **What applies the `+0x9000` fixup?** The static image has no writer to
  `0x800143D8`–`0x800144F0`, and the boot ROM has no hard-coded `0x80014xxx`
  reference. The boot ROM or a first-stage image loader applies it from the
  signed header; the exact table/agent is still unfound. A live read of
  `0x800187D0` during the boot window, or a RAM dump of the MLO after the boot
  ROM starts it, would settle it. [`73` §5]
- **What is `BOOTCTR_SIG`?** The newer OMAP builds (`LDR_13/14/15/22/24/29/
  44/47/49`) carry a second record name; this session found the string but did
  not reverse its use. It does not add an action-2 write. [`72` §4]
- **What is `LDR_29`'s `action==1 && value==3 && metrics[0x84] & 0x10000`
  gate for?** Its true branch still reaches the `action != 0` rejection; the
  intent (a mode switch? a factory-only path?) is unknown. [`72` §5]
- **The exact CFP `FUN_0044d400` request payload.** `71` §7c gives the
  action-2 semantics, but this session did not re-derive the caller's 8-byte
  payload; the `cfp-old.exe` `0x48BAE0` path appears to build action 0/1, and
  the setter wrapper's argument layout is easy to misread. The loader side is
  decisive regardless: no shipped loader accepts action 2. [`72` §3; `71`
  §7c]
- **The working unit's RPMB counter value.** The `0xDE` read says product/0;
  the `0x2801` live state is session 5's `A=0/B=10`. This session did not read
  `/dev/rpmb`; the reference's factory counter is not directly known here.
  [session-5/6 notes; `100` §2 is the spare's factory/0x32]
- **The expired unit's exact failure branch.** `0xFFFFFFFF` → selector 2; on
  the retry branch that is status `0x0F`/failure, not necessarily the
  normal-path `0x800149C2` nuke. Whether the retry failure erases the region is
  a code-path question (see the contradiction file); the observable is
  non-booting either way. [`71` §2/§3; `75` §6; `76` §1]
- **Whether the NVRAM editor's secure write is required for `0x2801`.** The
  live record was `is_protected=0`; `patch` writes insecure then secure and
  returns early if the insecure write fails. A protected record may never reach
  the secure writer. [`74` §1; `76` §1]
- **The `0x2074 +0xd4` field.** `71` §2 says the factory CRC gate is
  `region[0xd4] & 0xff == 6`, but the `0x2074` record is 72 bytes and the
  observed read is 0xC1 bytes; later `74`/`83` conclude `0xd4` belongs to the
  image descriptor, not the NVRAM record. Do not patch `0x2074 +0xd4` without
  a live descriptor dump. [`74` §3; `83`]
- **The `0x2801` `+0x0C` iteration count is per-unit.** The working unit's
  challenge showed 5060 and the spare's bootrom reports 5810 (`100` §0); the
  digest is a one-way verifier for `0000`, not a stored plaintext. [`84` §4;
  `100` §0]

## Superseded / uncertain

- **`0x2801` as a "time/recovery record" — superseded.** I believed `+0x18`
  was a threshold and `+0x1c` a maximum, and that `patch +0x18 = 0` would force
  the `0x80014A1E` branch. Evidence at the time: `FUN_80014F84`'s comparison
  and the `0x80014F28`/`0x80014FC6` clamp. Current: `75` §1–§3 and `84` §4 —
  `0x2801` is `NV_OSSTORE_PasswordStruct`; `+0x18` is the failed-attempt
  counter **A**, `+0x1c` the clamped maximum **B**; `A=0` is the **full**
  budget; the live unit is already `A=0/B=10` (`76` §1), so the write is moot.
  The correction is reflected in the repo's `74` (edited in place) and the
  canonical `75`/`84`.
- **"The CRC path needs the NVRAM `0x2801` recovery condition" — imprecise.**
  My `72` §6 phrased the gate as a special recovery condition. Current: the
  gate is `A < clamp(B,3,10)`; the live unit is already there; the factory/CRC
  path additionally needs a valid `BOOTCTR_FCTRY` (RPMB) and
  `region[0xd4] == 6` (the image descriptor). `75` §5/§6, `76` §6, `89` §3.
- **"The expired unit's bootloader nukes it" — imprecise.** My `72` §3.1 says
  the bootloader "still nukes it". `0xFFFFFFFF` makes the bootloader reader
  return `2`; on the retry branch (`A=0/B=10`) selector 2 is status `0x0F` and
  a failure/halt in `FUN_80014C98`, not the normal-path `0x800149C2` nuke.
  Current: `71` §2/§3, `75` §6, `76` §1, `92` §0/§5. A contradiction file
  records the distinction; the observable (non-booting) is unchanged.
- **The `0xFFFFFFFF` sentinel is `LDR_01`-only — corrected in-session.** My
  first wording generalized it to "the ramloader"; disassembling
  `FUN_8011EF78` in `LDR_13` showed no sentinel, and `72` §3.1 now says
  `LDR_01` only. The newer readers and the bootloader return `2`.
- **Report attribution.** Session 5's note says its reports are `72–79` and
  `83–87`. `72`, `73` and `74` were produced in this session (session 4);
  session 5 built on them, and `74` was corrected in place by session 5. The
  content is consistent; only the "its reports" phrasing is broader than the
  authorship. This is an attribution note, not a technical contradiction.
- **Unit identity.** I called the booted unit "working/regular". Later notes
  identify the reference as the 64 GB `PIN 0x50175127` (product, password
  `0000`) and the spare 64 GB `0x50176EC1` as the factory/test unit
  (`0xDE` mode=1, count=0x32). This session did not read the PINs; do not cite
  it for the inventory. [session-5/6 notes; `76`; `100` §2]
- **The live `0x2801` values are not from this session.** `76` (session 5) is
  the first live read (`A=0`, `B=10`, salt `0xB9A043B5`/`0x222D86B5`,
  iterations 5060, record SHA-256 `5311a186…0628`). This session only reversed
  the static gate and read the challenge in the BOOT_MODE debug output; it did
  not dump the record.
- **The ECDSA "static path is broken" conclusion — superseded.** The static
  read at `0x800143D9` is pre-relocation; `73` resolves it. The earlier `71`
  §5 text is preserved with a resolution note. Do not use the "broken key"
  reading for the product path.

## Artefacts produced

- `docs/reports/72-loader-bootmode-action2.md` — the `loaders.dat` audit, the
  per-loader action-0-only table, the dead setters, the `0xFFFFFFFF` sentinel,
  and the host-side BOOT_MODE support.
- `docs/reports/73-mlo-ecdsa-key-pointer.md` — the `+0x9000` relocation, the
  key/`p`/`b`/`3` numeric proof, the MLO header cert directory, the verifier
  chain and the remaining fixup-agent question.
- `docs/reports/74-nvram-editor-and-0x2801.md` — the NVRAM editor command set,
  the tool caveats, the static `0x2801` fields and the `0x2074` layout; later
  corrected in place by session 5 (see *Superseded*).
- (Session workspace only) `HANDOFF-2026-10-21-next-session.md` — this session
  rewrote the handoff to be self-contained; the repo's copy is the later
  session-5 consolidated version (it lists `75`–`87` and the live audit), not
  this rewrite.
- `analysis/loader-audit-2026-10-21/` — `parse_loaders.py`, `scan_de2.py`,
  `disarm.py`, `FindDisp.java`, `find_p521.py`, `scan_key_slot.py`,
  `scan_rom_refs.py`, `check_reloc.py`, `loaders.csv`, and the per-loader
  Ghidra decompiles (`LDR_*-de.txt`).
- `bb10mt-main/bbloader.pas`, `uflash.pas`, `bb10mt.lpr` — `TBootModeInfo`,
  `TBBLoader.BootMode`, the response/status/value helpers, the `bootmode`
  command, the `info` Boot Mode line and the command registration.
- `bbusb/bbusb.py` — `CMD_BOOT_MODE`, action/status/mode tables,
  `boot_mode`/`parse_boot_mode`/`read_boot_mode`/`set_boot_mode`/
  `set_boot_count`, `hash_pass_v2`, the `0x0E`/`0x0F` password loop,
  `get_var`, `switch_channel`, `set_mode_raw`, `send_loader(chunk_size)`, the
  argparse CLI (`--boot-mode`, `--factory`, `--product`, `--set-count`,
  `--force`, `--from-bootrom`, `--loaders`, `--addr`, `--debug`,
  `--password`), and the legacy-flow address/chunk fix.
- Host environment: `pyusb 1.3.1` installed user-local; the `MODE=0666` udev
  rule and `libusb-1.0` were already present.
- On-device state left at the end: the working unit was left in its normal
  TabletOS state (the CLI's `finally` issues a best-effort `reboot_loader`);
  the expired unit was non-booting. No NVRAM/RPMB/flash write.

## Deliberately left out

- The full RPMB/secure-monitor service map, the CRC-32 recipe, the `0x2074`
  A/B semantics and the password/PIN arithmetic — session 5/6's reports
  (`75`–`84`, `89`, `92`) already carry them; this note only cites them.
- The Linux-era display/kernel/backlight work (`103`–`122`) — later sessions.
- The detailed per-loader decompilation table (it is in `72` §5) and the full
  boot ROM/media/monitor tables (`10`–`68`).
- The Cellebrite loader audit and the native-boot plan (`86`–`98`) — session 6.
- The expired unit's recovery options beyond "no pre-OS writer; `/dev/rpmb`
  needs a booted OS" — that is `72` §6 and session 6's `92`.
