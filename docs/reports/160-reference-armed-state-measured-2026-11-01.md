# 160 — The reference is **record-armed but image-stock**: `BOOTCTR_FCTRY = 0x32` with the stock type-4 IFS

> **Superseded for region 0 (2026-11-01).**  The operator flashed the sealed
> `os_type` 6 IFS, so the unit is now armed **and** sealed and boots.  This is the
> pre-flash measurement; current state is `STATE.md`.

**Session 21 (absolute), Linux-era session 15.  2026-11-01.**
Status: **read-only measurement.  No flash by the agent; the operator will
complete the arming with `bb10mt`.**  The `BOOTCTR_FCTRY` record exists, but
region 0 is still the **stock 4869 image** (`os_type 4`), not the CRC-sealed
image.  The next boot would take the factory path and fail its `os_type == 6`
gate, so the unit is **reboot-unsafe** until the image is resealed/flashed.

This report exists because the docs (commit `1c785c8`) record the reference as
"ARMED — the record exists and region 0 is CRC-sealed" and explicitly say the
armed values were never measured.  They were measured during this wrap-up; the
"CRC-sealed" half is **not true**.

---

## 0. Headline

| item | measured value |
|---|---|
| `/dev/rpmb/BOOTCTR_FCTRY` | **present**, size **4**, bytes **`32 00 00 00`** = `0x32` |
| region-0 IFS (media `0x100000`), exact `0x2BFE10` | sha256 **`75be24e8297e3a4a27b8f7ce2f9d857b94178b020c063d73ecb0cc90c0fe94ba`** |
| `os_type` (IFS `+0x100` = media `0x100100`) | **`0x04`** (stock; the factory gate wants `0x06`) |
| CRC at `end-4` (media `0x3BFE0C`) | **`0x8D46FB4A`**; `crc32[0:end-4]` = `0x8D46FB4A` (matches) |
| container magic at IFS `+0xC8` | `0xC3D0E193` |
| model at IFS `+0xA8` | `0x06001A06` |
| `end-8` | `0xD7C82D1F` |
| identity | **the stock 4869 image** — not our chainloader, not the 2.1.0.1917 image |

The record and the image are **two separate writes**, and only the record was
done.  The image was unchanged by the arming: the pre- and post-arm hash are
both `75be24e8…`.

The sealed asset `local/images/ifs0.sealed.bin` is the correct CRC-sealed form
of the **same** stock image: sha256 **`c72b0530…`**, `os_type 6`, CRC
**`0x116B2562`**, and exactly **5 bytes** differ from the live stock image:

```
0x000100          04 -> 06            os_type
0x2BFE0C..0x2BFE0F 4a fb 46 8d -> 62 25 6b 11   CRC32/ISO-HDLC (0x8D46FB4A -> 0x116B2562)
```

`crc32[0:end-4]` of the sealed asset = `0x116B2562`; magic/model/`end-8` are
unchanged.  The live pre-image is saved at
`local/images/ifs0.stock-75be24e8.bin`.

## 1. Why this is reboot-unsafe

The loader's factory path (record present, selector 1) verifies region 0 with
the CRC gate `desc[0xD4] == 6` (`os_type`), the magic `0xC3D0E193`, the
`0xD7C82D1F` tail, and CRC-32 over `[start, end-4)`.  The live image has
`os_type 4`, so the gate fails.  NVRAM `0x2801` is `A=0/B=10`, i.e. the **retry**
branch, so the failure **halts** rather than nukes — but the unit does not boot.
It is running the TabletOS image that was booted **before** the record was
created; a reboot now would halt.

Arming is therefore complete only when **both** are true:

1. `BOOTCTR_FCTRY` is present as exactly 4 bytes in `1..0x32` (this is done:
   `32 00 00 00`); and
2. region 0 is CRC-sealed: `os_type 6`, CRC over `[0:end-4)` stored at `end-4`
   (this is **not** done; the image is stock `75be24e8…`).

## 2. The operator's route (not performed by the agent)

The operator will modify the vendor `PB-4869-root.0.3.ifs` and flash it with
`bb10mt` themselves.  The file is the 44-block/`0x2C0000` component whose first
`0x2BFE10` bytes are the image and whose remaining 496 bytes are zero; the
vendor `.lst` places it at region-0 offset `0x100000` (loader block `0x825`).
The standard reseal of the present stock image is the sealed asset above
(`c72b0530…`, `os_type 6`, CRC `0x116B2562`); a modified `.ifs` will have its
own hash and CRC.

After that flash, the record `0x32` is already present, so the factory path will
verify the new image and boot TabletOS; the boot decrements the counter
(`0x32 → 0x31`), and TabletOS's fixed `startup.sh` tops it back up to `0x32`.

**Do not reboot the reference before that image is on media.**  The unit is up
now; a power loss or reset would halt it.

Verification after the operator's flash (read-only):

```sh
tools/pbssh.sh 'dd if=/dev/hd1 bs=16 skip=65536 count=180193 2>/dev/null' | sha256sum
# expect c72b0530… for the standard reseal (or the operator's modified hash)
tools/pbssh.sh 'dd if=/dev/hd1 bs=512 skip=2048 count=1 2>/dev/null' | \
    python3 -c 'import sys; d=sys.stdin.buffer.read(); print("os_type", d[0x100])'
tools/pbssh.sh 'dd if=/dev/hd1 bs=4 skip=982915 count=1 2>/dev/null' | xxd -p
# expect the CRC word; recompute crc32 over the exact 0x2BFE10 bytes host-side
tools/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | xxd -p
```

## 3. Bench state / the aborted agent restore

* TabletOS up; door up (host `169.254.0.2/16` on `enp8s0f3u1`); unit
  `0fca:8010`; `/dev/trustzone` **absent** (the resmgr crashed; report 157).
* An agent attempt to restore the sealed IFS with the pre-OS flasher was
  **aborted before any USB transfer**: the flasher log ends at
  `waiting for bootrom ...`; the unit stayed at `0fca:8010` and the door
  answered; no bytes were written.  The log is kept at
  `docs/reports/data/session21-ifs-restore-aborted.txt`.
* **The image remains `75be24e8…` and the record remains `0x32`.**  The
  operator's `bb10mt` flash is the intended path.

## 4. Open

* The operator's post-flash hash, `os_type`, CRC and whether the record
  decrements/tops up as expected.
* Whether the modified `.ifs` also sets the descriptor's image **length** field
  (the session-22 note; the stock sealed asset already has the correct length).
* The trustzone resmgr crash and the `0x2F` transform remain open
  (reports 157 and 156).
