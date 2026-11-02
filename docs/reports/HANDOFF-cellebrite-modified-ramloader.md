# Handoff — Cellebrite / modified-ramloader track

Paste this whole file as the first message of a new chat. It is self-contained.

---

## Mission

Determine whether a **modified ramloader can be executed on a BlackBerry PlayBook
(OMAP4430, BB10/TabletOS)** — i.e. whether we can get custom code running in the pre-OS
loader context, starting from the Cellebrite UFED loader artifacts. Deliver a decision with
evidence and a minimal, staged test plan. This is authorised reverse engineering of our own
hardware; the long-term goal is repair/experimentation on bricked units and a native-boot
path, not attacking third-party devices.

The specific question is **not** "can the signed loader read/write arbitrary eMMC"
(prior track A closed that as *no*, see `88`). It is:

> Can we make the bootrom (or the signed loader itself) **execute bytes we control**,
> rather than only the signed image?

---

## Working directory and access

- Workspace: `/home/psyden/rea-work`
- Device family: PlayBook P100-16WF, model ID `0x06001A06`, `BR_ver 0x051B0014`,
  OMAP4430 Winchester ES2.2 HS, QNX 6.6. Device password `0000`.
- Live units: **connected 64 GB reference** (running 2.0.0.4869, rooted) and a
  **spare 64 GB test bed**. Two **32 GB units are bricked** (NVRAM/RPMB damage) and are
  recovery targets, not test beds.
- **The spare 64 GB unit may be connected to this host.** The bootrom is single-owner:
  only one process/session may drive the USB device at a time. Decide up front whether this
  chat is static-only. If it needs the bootrom, coordinate with the main session so the
  other one is not polling/uploading.
- Do **not** touch the connected 64 GB reference. All live work, if any, is on the spare.
- Root access path (when needed): `BerryShell-V4.py` qconn door -> root SSH
  (`ssh -i ./rsa root@169.254.0.1`, legacy algorithms). The door must be held open
  (`BerryShell-V4.py hold --key /home/psyden/rea-work/rsa --pub /home/psyden/rea-work/rsa.pub`).
  This is irrelevant to bootrom work except for reading files off a booted unit.

---

## Read these first (paths under `/home/psyden/rea-work`)

| File | Why |
|---|---|
| `bootdumps-2026-09-11/analysis/88-cellebrite-ramloader-audit.md` | **The prior audit. Read in full before doing anything.** It closes arbitrary eMMC R/W and maps the Cellebrite loader. |
| `bootdumps-2026-09-11/analysis/86-cellebrite-loader-lead.md` | Original lead and inventory. |
| `bootdumps-2026-09-11/analysis/69-live-bootrom-loader-usb.md` | Live USB protocol: handshake, upload, hand-off, loader mode. |
| `bootdumps-2026-09-11/analysis/66-ramloader-opcodes-and-sachesi-debug.md` | LDR_01 opcode map, CREAD gating, dispatchers. |
| `bootdumps-2026-09-11/analysis/72-loader-bootmode-action2.md` | `0xDE` read-only in every shipped loader. |
| `bootdumps-2026-09-11/analysis/71-hd0-mode-map-and-verifier.md` §2, §7b, §7c | Boot-stage verifier, RPMB, CFP `0xDE`. |
| `bootdumps-2026-09-11/analysis/HANDOFF.md` | Boot-ROM RE state, hidden page, SMC ABI. |
| `bootdumps-2026-09-11/analysis/ramloader-commands.txt` | Human-readable opcode list. |
| `bb10mt-main/ramloader.pas`, `bb10mt-main/uarchive.pas` | Host-side loader selection, signed-loader check, archive format. |
| `cellebrite-blackberry/BlackBerry OS cellebrite exploit - wumbomumbo.txt` | BBOS three-address exploit write-up. |
| `cellebrite-blackberry/messages.txt` | Notes; states BB10 strictly checks address parameters. |

Artifacts referenced by `88` (read its §3–§9 for the exact maps):
`analysis/88-…` opcode diff, `analysis/66-…`, `analysis/loader-audit-2026-10-21/`.

---

## Established facts — do not redo

### Artifacts (verify hashes anyway)

| File | Size | SHA-256 | Header |
|---|---|---|---|
| `cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A` | 159,968 | `03ffaf26dc0ce640c2f44dc5e223bb34d15175234f26d9876846484022a5c6dc` | `D7D32D1F`@+4, load `0x80100000`, `D7C82D1F`@len-8 |
| `cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A` | 138,796 | `b5f345d36e042a582ce27e383da87250af268ded046facdef05d67ca15e18a3f` | `D7D32D1F`@+4, load `0x02000008` (Qualcomm) |
| `bbusb/loaders/loader_06001A06-00.bin` (`LDR_01`) | 238,128 | `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f` | standard PlayBook loader |

- `06001A06_0000000A` is a **signed** ramloader (signature area non-FF; intact CRC-32 over
  `[0,len-4)` verified by `88`). It is **not** the BBOS patched loader: the documented
  Cellebrite patch signature `DB9F07EB` is absent. The two `6A0000EA` words are ordinary
  `b` instructions (`0x801055F8`, `0x8011F2E8`), not dispatcher cases.
- **Correction to `86`:** its "entry `0x801234FC`" is not an entry. `88` found header `+0xC`
  is a descriptor-table pointer; the code starts at `base+0x10` (`0x80100010`). Treat any
  address from `86` as unverified until `88` or your own decode confirms it.
- `05001A06` is a Qualcomm phone loader; likely irrelevant except as a signature/format
  comparison.
- The pack contains many other model-ID-named loaders; inventory them (see `88` §2/§10).

### `06001A06` behaviour (from `88`, proven unless marked)

- One flat router `FUN_8010F280` (`base+0xF280`, 5,116 B), compare chain plus two jump tables
  (`0xD2..0xDC`, `0xE0..0xEB`). No four-dispatcher split.
- Missing vs `LDR_01`: `0x20, 0x39, 0x3A, 0xAD/AE/AF, 0xB5, 0xBF, 0xC2/C3/C4, 0xE8/E9/EA,
  0xEC/ED`; `0xDD`/`0xC4` never existed.
- `0xDE` BOOT_MODE is read-only at `base+0x1049C`: `action != 0` returns silently, no frame.
  There is **no** `BOOTCTR_FCTRY` setter in this build.
- `0xF7`/`0xF8` write a QCFM V2 container; destination LBA comes from an `'rrcq'` run record.
  The only gate is `FUN_8011B774`, a 64-bit range check against an MCT-built allow-list
  (types `0x1C,0x1B,0x1F,0x24,0x34` + `0x19/0x1A`). `0xEE` erase has the same shape.
- Every write session first performs an authenticated **`FS_DIRTY` RPMB read-modify-write**;
  if that fails the eMMC write is abandoned.
- No arbitrary read: `0xE4/0xE5/0xE6/0xEB` are gated on state byte `+0xAEC`, emit cipher
  output, and `0xE5` is range-gated; `0xDC` yields one CRC-32.
- Unknown **U1** in `88`: is the NVRAM partition inside the MCT allow-list? Closable
  read-only with `0xB4`/`0xD9` on the spare.

### Bootrom upload / hand-off (from `69`)

- Bootrom enumerates `0fca:0001`; ramloader re-enumerates `0fca:8001`.
- Upload: `0xF009` setup `{addr, size}`, 916 x `0xF004` 260-byte chunks, `0xF00A` verify.
  `0xF005` EXECUTE_RAM (`u16 cmd, u32 addr`) is acknowledged but **does not jump by itself**.
  The client must perform one further read on channel 1; that read returns `0b 01 01 00`
  (cmd `0x0B`, mode `0x01`, packetId `1`), and only then does the unit re-enumerate as
  `0fca:8001` (~3–4 s later).
- `0x0B` on channel 0 returns `0x0C` and **reboots** the unit; it is not an alternative.
- Bootrom can wedge after a failed attempt; power-cycle is the recovery. Do the upload and
  the command in one run (loader host-activity timeout).
- `0xE9` (NVRAM `0x2074` read) requires a 4-byte zero payload; drain the paged `0xB0`/`0xB5`
  Bugdisp log to `0xD0` before other channel-2 commands.
- The `0xF00x` handlers are **not in the NS-readable boot ROM** (no `movw #0xF00x`). They are
  in the hidden page / secure world around `0x40027000`, which is firewalled. Static
  analysis of the upload/verify logic may therefore be impossible; plan for black-box
  probing.

### BBOS exploit vs BB10

- BBOS exploit: `WRITE_RAM_SETUP`/`WRITE_RAM`/`WRITE_RAM_VERIFY` write a patched loader at the
  normal load address, a payload at `addr+delta`, the original signed loader at
  `addr+2*delta`; verify reads one copy while execution uses another. The patched BBOS loader
  changes dispatcher case `0x8` to `bl` into `0x40200400`.
- `messages.txt`: **BB10 strictly checks address parameters**, so the literal three-address
  trick does not work as-is. The question is whether an analogous **verify/execute
  mismatch** exists on BB10 (see hypotheses).

---

## What to investigate (ranked hypotheses)

For each: state the exact mechanism, the addresses/files, the observable prediction, the
risk, and a kill criterion. Do not accept "the BBOS exploit" as an answer.

1. **Verification is skipped or optional.** Does `0xF005` execute the uploaded buffer even if
   `0xF00A` was never called, returned failure, or was called with a different size/address?
   Is there a "verified" flag in hidden RAM? What exactly does `0xF00A` compare — signature,
   CRC, or a host-supplied value?
2. **Verify/execute aliasing (BB10 analogue).** Does `0xF00A` hash the same physical bytes
   that `0xF005` jumps to? Test coverage/length: short verify, declared-size vs actual-size,
   the 260-byte chunk tail, zero-length, integer wrap, overlapping ranges. Also cache aliasing
   (write-back not invalidated, `0x80100000` vs a low alias, TCM/IRAM alias).
3. **Address-parameter checks.** `0xF009` takes `{addr,size}` and `0xF005` takes `addr`.
   Can `0xF005` be given an address different from the `0xF009` address? Can `0xF009` be
   repeated to write a second range that is not covered by the verify? Can a chunk write
   extend past the declared size into the verified region after `0xF00A`?
4. **Signature scope.** Determine what the loader signature actually covers (header only?
   code? tail?) by comparing a valid loader with one byte flipped in each region and observing
   rejection. `uarchive.pas`/`ramloader.pas` describe the host-side `isSignedLoader` check
   (`0xD7D32D1F`@+4, `0xD7C82D1F`@len-8, non-FF signature area) — that is host-side only;
   the bootrom/secure-world check is what matters.
5. **Model-ID substitution.** The bootrom learns the model via `GETVAR(2)` (`modelID` at
   payload offset 16). Is the loader's load address checked against that model, or taken from
   the loader header? Can a loader signed for another model (different load address) be
   accepted, or one model's loader be placed at another model's address?
6. **Loader-side control-flow hijack.** Re-audit `06001A06` with a hijack lens (not an
   eMMC-access lens): unchecked lengths/indices in the flat router, stack buffers in
   `0xF7/0xF8` QCFM parsing, the `'rrcq'` run-record parser, `0xB0` log paging, `0xC0`
   COMPLETE, `0xE0` installer, `0xF9` signature trailer, `0xAF` mode flag. A write-what-where
   or return-address overwrite is enough even without arbitrary eMMC access.
7. **Signed-loader features that execute or load.** Any command that takes an address and
   branches, loads a second stage, or calls through a function pointer (e.g. installer/
   complete/signature paths). Compare with `LDR_01`; the Cellebrite build may have a smaller
   but differently-reachable path.
8. **CFP RAM agent.** `71` §7c notes cfp's on-device agent as a possible pre-OS writer.
   Audit `CFP.exe` / `cfp-old.exe` for the loader-upload and execute sequence; a host-side
   difference in the sequence may reveal a bootrom weakness (e.g. verify omitted).
9. **Other pack loaders.** Inventory every entry in `ufedblackberrypack.epr/`; a signed loader
   with a different base, dispatcher, or verification header may be a better base for a
   patch or a chain.

Deliver a **hypothesis matrix** with a clear verdict for each: feasible / not feasible /
needs live test / blocked.

---

## Constraints and safety

- Read-only by default. No flash, NVRAM, RPMB, or eMMC writes on the connected reference.
- Do not run a patched loader on either working unit until the main session/user approves the
  exact bytes and the recovery story. The risk is a bootrom wedge (power-cycle recovers) or a
  unit that no longer boots (no pre-OS writer exists per `72`).
- The two 32 GB units are bricked recovery targets; do not use them as test beds.
- The bootrom is single-owner; coordinate if the main session is using the spare.
- Keep throwaway scripts in `$COMMANDCODE_SCRATCHPAD`; do not modify the signed images,
  `hd0head.bin`, or any `.pyc`.
- If a live step is needed, stop and hand it back with the exact command and expected
  observation.

---

## Tooling

- Capstone 5 (`python3 -c 'import capstone'`), `readelf`, `strings`, `objdump`.
- REA 6.7.0 + Ghidra 12.1.4 are configured (`~/.commandcode/mcp.json`,
  `REA_ANALYSIS_PROVIDER=ghidra`, `JAVA_HOME=/usr/lib/jvm/java-21-openjdk`). Open a raw image:
  `open_binary(path, format='raw-binary', architecture='arm', entry_point=<base>,
  provider_id='ghidra')`. Loader base `0x80100000`; boot ROM base `0x40028000`.
- Existing helper: `bootdumps-2026-09-11/analysis/loader-audit-2026-10-21/disarm.py`
  (`python3 disarm.py <file> <base> <start> [arm|thumb] [n]`).
- Host USB: `bbusb/bbusb.py` (PyUSB 1.3.1, user-local). Upload path is
  `loaders/loader_06001A06-00.bin` at `0x80100000`, 260-byte chunks.
  `python3 bbusb.py --boot-mode --from-bootrom [--password 0000] [--debug]`.
  udev rule needed: `SUBSYSTEM=="usb", ATTR{idVendor}=="0fca", ATTR{idProduct}=="0001"|"8001", MODE="0666"`.
- Also: `analysis/rea-ghidra/playbook-usb-probe.py`, `analysis/rea-ghidra/sachesi-debug-harness/`,
  `bb10mt-main` (Pascal client).

### Reproduction snippets

```bash
cd /home/psyden/rea-work
sha256sum cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A \
          cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A
python3 - <<'PY'
import struct
for p in ('cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A',
          'cellebrite-blackberry/ufedblackberrypack.epr/05001A06_0000000A'):
    d=open(p,'rb').read()
    print(p, len(d), hex(struct.unpack_from('<I',d,4)[0]),
          hex(struct.unpack_from('<I',d,8)[0]),
          hex(struct.unpack_from('<I',d,len(d)-8)[0]))
PY
python3 bootdumps-2026-09-11/analysis/loader-audit-2026-10-21/disarm.py \
  cellebrite-blackberry/ufedblackberrypack.epr/06001A06_0000000A 80100000 8010f280 arm 60
```

---

## Deliverable

Write your report to:

`/home/psyden/rea-work/bootdumps-2026-09-11/analysis/93-cellebrite-modified-ramloader.md`

Include:

1. Provenance: hashes, tool versions, which facts you re-verified vs took from `88`.
2. Verification model: which command gates execution, what is signed/checked, where the
   check lives (and whether that is static or hidden).
3. Hypothesis matrix (H1..Hn) with verdicts, evidence, exact test, expected observation,
   risk, kill criterion.
4. Static findings per hypothesis with addresses and quoted instructions/decompilation.
5. A read-only live test plan for the spare unit, then a gated execute plan with the exact
   bytes/commands and the recovery story.
6. A clear decision: **feasible / not feasible / needs X**, and what the smallest next
   experiment is.
7. Residual unknowns and anything that contradicts `86`/`88`/`69`/`71`.

Return a short summary: the decision, the top mechanism (if any), and the report path.
