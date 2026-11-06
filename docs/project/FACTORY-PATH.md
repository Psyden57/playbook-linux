# The factory/CRC path: what it is, and how a unit gets switched to it

**Read `SAFETY.md` first.**  This page explains the mechanism and the *operator-only*
procedure.  **An agent must never perform the writes described here** — `SAFETY.md` §6
allows an agent to read everything and flash region-0 images, and nothing else; the
`BOOTCTR_FCTRY` write is the operator's, on a unit they can afford to lose, with a
readback.  (Session
20: the bootloader **does** rewrite this record on every factory boot — reports
`150`/`154`, `docs/contradictions/71-bootctr-stage-decrement-vs-provision-only.md`;
§2's "no write path" means *no host-side* path.  A failed bootloader write is
fatal: it blinks `0x2F`, LED 1-4 — `KNOWN-ISSUES.md` trap 65.)  The full
procedure is `ARMING-TUTORIAL.md`.

## 1. Why there are two paths at all

The bootloader verifies region 0 in one of two ways, chosen at boot by one function,
`FUN_8000911A` (`hd0` image `0x8000911A`, report 71 §2).  It reads the RPMB record
`BOOTCTR_FCTRY`:

| record state | selector | path |
|---|---|---|
| **absent** | `0` | **product path** — SHA-512 + ECDSA/public-key verify of the region |
| **present, value `1 … 0x32`** | `1` | **factory path** — CRC-32/ISO-HDLC over region 0 only, and only if `region[0xD4] == 6` (`os_type = 6`) |
| present, value `0` or `> 0x32` or length ≠ 4 | `2` | failure — the unit halts with no OS (`0x800094BE`, `b .`) |

Source: report 71 §2/§3 (static, both ARM and Thumb verified); the value range and the
"absent → selector 0" behaviour are confirmed on hardware by report 95 §1 and report 92
§0.6.

**Which bootloader does it.**  Not the TI OMAP4 boot ROM.  The OMAP4 mask ROM
(`0x40028000`) does the initial boot, authenticates and loads the next stage, then hands
over; it knows nothing about RPMB or the counter.  The check lives in **RIM's
second-stage loader** — the MLO-header image at `hd0` offsets `0x0–0x200000`, loaded at
`0x80000000`, whose own log line is `Starting RIM bootrom with UART_DEBUG enabled`
(report 70 §1/§2).  That stage catalogues RPMB on every boot (`0x800112DC`) and calls the
selector from the verifier driver (`0x80014254`/`0x80014260`) and from boot main
(`0x80014904`).  It is itself an authenticated image — its `CertPK_`/`CertPPA`/`CertISW`
blocks are consumed by the OMAP boot ROM and/or the secure world, not by the stage
(report 71 §4) — so the selection logic cannot be patched.

Consequences that matter:

* **A clean retail tablet is on the product path.**  Its `BOOTCTR_FCTRY` record is
  **absent** — report 95 §1 measured exactly that on the spare unit before it was armed.
  Nothing in the stock OS creates the record: the periodic top-up is *our* script
  (the operator's startup script, installed as a devuser startup), not stock behaviour.
* **Our images only boot on the factory path.**  Every image this project flashes is
  **CRC-sealed**: `os_type = 6` at image offset `0x100` and a CRC-32 trailer
  (report 89).  Changing those bytes is what makes a self-built image bootable, and it is
  also what invalidates the ECDSA signature.
* **Therefore a CRC image on an unarmed unit does not boot.**  The product path's
  public-key verify fails (the reseal broke the signature) and the unit refuses to
  start.  What happens next depends on which branch the unit is in (report 75 §5–6,
  report 92 §0.6):

  | branch | entry condition | failed verify |
  |---|---|---|
  | **retry** | `0x2801` `A < clamp(B,3,10)` — the surviving reference is `A=0`, `B=10` | verification fails → **halt** (`0x800094BE`, `b .`); the region is **not** erased |
  | **normal** | `A >= B` | `0x800149C2 cbnz` → **nuke** (`0x800156E8`: erase the OS-region ranges) |

  So on a normal unit the failure is "it does not boot", not "it wipes itself"; the
  recovery is the ordinary pre-OS `0xF7` reflash of a stock image (`SAFETY.md` §4),
  which does not depend on region 0, the chainloader or the counter.  Whether the
  normal-path nuke ever runs during a *verify* failure is the open question in
  `docs/contradictions/expired-counter-nuke-vs-retry-halt.md` — the erase has never
  been observed.  Either way: **arm the unit before flashing anything CRC-sealed**,
  because an unarmed unit cannot boot anything until you reflash it.

## 2. What the switch is

The switch is nothing more than **creating the `BOOTCTR_FCTRY` record with value `0x32`
(50), from a booted stock OS**.  There is no NVRAM flag, no jumper, no host-side command:
the record lives in the eMMC RPMB partition and every write is authenticated by the
secure monitor over a nonce and a monotonic write counter (report 92 §0.2/§0.3).  The
bootloader can read the record and can provision an *absent* one only in a narrow
"RPMB not provisioned" state; it has no write path from the host, and no pre-OS writer
exists (report 92 §0.3, report 72).

Rollback exists, but only inside a window: **`rm /dev/rpmb/BOOTCTR_FCTRY` from a booted,
rooted TabletOS** returns the selector to `0` (product path) — report 92 §0.8/§5.5.
Once the only thing that boots is our Linux payload (no `/dev/rpmb`), that window is
closed and the counter can only run down.

## 3. The procedure (operator only)

This is the sequence that was actually executed and proven on the spare 64 GB unit in
report 95 §1–§4.  It is reproduced here because no other page states it end to end.
**It was not re-run in session 15**: that session only read the unit's state (the record
was already `0x32`), so the procedure below is as executed in report 95, and the
`TEST_PAD` invocation is the one in the live capture
`docs/captures/playbook-live-spare-2026-10-25-testpad.txt`.

It is written for the spare, which is now down.  The surviving **reference was armed by
the operator in session 21** (record `0x32`) and sealed by the sealed-IFS flash on
2026-11-01 — current state is in `STATE.md`.  The procedure is kept because it is the
only complete description, and because a future unit would need it.

**Preconditions**, all read-only checks first:

1. The unit boots its stock TabletOS and you have root (`tools/pbssh.sh`).
2. `BOOTCTR_FCTRY` is **absent** (this is what a clean unit looks like) or already
   `0x32`.  Read it; never guess.
3. The stock region-0 image is CRC-validatable: the reseal below is 5 bytes and must be
   verified with the bootloader's own checks before any reboot.
4. `/dev/rpmb` exists and the **rollback rehearsal** succeeds.  This is `TEST_PAD`: a
   *sacrificial record name* — nothing in the stock image defines it, the resmgr is just
   a directory of records, so any unused name works.  Create it, write 4 bytes, read
   them back, delete it:

   ```sh
   dd if=/tmp/t.bin of=/dev/rpmb/TEST_PAD bs=4 count=1 seek=0 conv=notrunc
   dd if=/dev/rpmb/TEST_PAD of=/tmp/t2.bin bs=4 count=1 && cmp /tmp/t.bin /tmp/t2.bin
   rm /dev/rpmb/TEST_PAD
   ```

   It proves that the directory resmgr, the secure-monitor write path and the exact
   `dd` invocation all work, on a record that does not matter, before you touch the
   counter.  It also spends RPMB write-counter increments, which is why it is done once
   and not looped (see R4 below).
   If the rehearsal fails, stop — you have no rollback.

**The switch**, all in one session, with **no reboot in between**:

1. Reseal region 0 to the CRC format: image offset `0x100` `4 → 6`, CRC trailer
   `0x8D46FB4A → 0x116B2562`
   (`tools/89-crc_tool.py reseal --image-off 0 --type 6 <in> <out>`).  Write the two
   sectors to `/dev/hd1` (`bs=512 conv=notrunc`), read both back byte-identical.
2. Re-verify the whole 0x2BFE10-byte image with the bootloader's own checks
   (`tools/89-crc_tool.py verify …`): structure, `desc[0xD4] == 6`, CRC match →
   `VERIFIED (CRC path)`.
3. Create the record: exactly **4 bytes** `32 00 00 00`, `open`/`lseek(0)`/`write`
   (never `>`, `>>` or `O_TRUNC`), then read it back and check both the value and that
   the length is 4.
4. `sync`, then reboot.

**Why the order is load-bearing.**  Step 1 breaks the signature, so the product path can
no longer boot the image; step 3 is what makes the CRC path selectable.  Rebooting
between them leaves a unit that fails both verifies — and a failed normal-path verify
nukes the OS region.  This is the single most dangerous step in the whole project's
recovery story.

**After the switch:**

* the first boot takes the factory path, verifies region 0 by CRC, boots, and
  **decrements** the counter (`0x32 → 0x31`, measured in report 95 §4);
* the counter is "factory boots remaining".  On the spare it was kept topped up to `0x32`
  by the operator's startup script, installed on the unit as
  `/accounts/devuser/startup.sh` and run from `/radio/scripts/post-boot.sh` on every
  TabletOS boot — that script is ours, not stock, and it is what kept 15 sessions of
  bench work alive;
* flashing our chainloader/kernel images afterwards is the ordinary pre-OS `0xF7` path
  (`SAFETY.md` §4) and does not touch the counter or the record.

## 4. Failure modes, in one place

The first four are **not** bricks: the unit refuses to boot, and the pre-OS `0xF7` flash
(`SAFETY.md` §4) still works, so a stock image — or a resealed one — brings it back.
Stock images for many PlayBook OS versions are available and backed up; the reflash does
not depend on region 0, the chainloader or the counter.

| what happened | what you have |
|---|---|
| armed, then the region-0 image is not CRC-valid | halt at `0x800094BE` (retry branch); no OS, no `/dev/rpmb`. **Recovery: pre-OS `0xF7` flash of a CRC-sealed image.** A raw stock image will not do — it has `os_type = 4`, so reseal it first (or delete the record, which needs a booting OS you no longer have) |
| unarmed, and a CRC-sealed image was flashed | product/ECDSA verify fails → halt (retry branch). **Recovery: pre-OS `0xF7` flash of a stock image** (or reseal + arm it properly this time) |
| counter reaches `0` | `BootModeCount expended`, halt. Reflash does not help — the record must be rewritten, and that needs a booting OS. The 32 GB unit in this state is parked (report 92 §0.4). The other 32 GB unit failed differently: its NVRAM partition was erased and it refuses to boot with `nvram signature invalid` |
| value written as `0`, `> 0x32`, `0xFFFFFFFF`, or length ≠ 4 | selector `2` → halt, same problem as above (report 92 §0.6) |
| record deleted while a rooted TabletOS still boots | back to the product path; boot a stock `os_type = 4` image again (reseal it back if that is what is on media) |
| RPMB write counter saturates | the partition becomes permanently write-protected; not readable from userland. Do not loop write/delete experiments (report 92 §6 R4) |

**Do not arm a unit you cannot boot with root afterwards.**  The reference was a
deliberate exception: the operator armed it on 2026-11-01 because it was the only
working unit left, and an agent still never writes RPMB (`SAFETY.md` §6).  The
armed-and-sealed state is in `STATE.md`; the rule is unchanged for any other unit.

## 5. What is still not known

* **Whether the normal-path nuke ever runs on a *verify failure*.**  The `0x800149C2`
  `cbnz` → `0x80014A16` → `0x800156E8` sequence is documented in report 71 §3.4 and the
  erase has never been observed; the retry branch (which the surviving unit takes) halts at
  `0x800094BE` instead.  See
  `docs/contradictions/expired-counter-nuke-vs-retry-halt.md`.
* **Whether deleting the record is enough to restore the product path after the stock
  image has been resealed.**  No: the reseal must also be undone (the original 5 bytes),
  because the signature only validates the original content.

## 6. Sources

* report 71 §2 (two formats, the selector), §3 (the NVRAM gate and the nuke path);
  §7b (the record is authenticated);
* report 92 §0 (summary), §5.5 (rollback), §6 (risk checklist R1–R12);
* report 95 §1–§4 (the proven switch on the spare, with the counter decrement);
* `SAFETY.md` §2 (values, the brick, who may write), §3 (the boot gate), §4 (the flash
  path and the reseal).
