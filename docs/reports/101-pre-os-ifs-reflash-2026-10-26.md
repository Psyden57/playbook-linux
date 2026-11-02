# 101 — Pre-OS IFS reflash: proven end-to-end (2026-10-26)

Status: **live, destructive test on the spare 64 GB unit, successful.** The connected
reference was not touched. This closes the safety-net question from `97`/`98`: the signed
ramloader can rewrite the region-0 IFS over USB, with no OS.

## 0. Result

A byte-identical reflash of the sealed TabletOS IFS (`os_type=6`, CRC `0x116B2562`) via
`0xF7` produced an image on media with **sha256 `c72b0530…` — identical to
`ifs0.sealed.bin`** — and the unit booted the factory/CRC path normally. The counter
decremented 50 → 49; it was topped back to 50.

## 1. Working sequence (what actually ran)

1. **Container**: take the real `2.0.0.4869-IFS.0.signed` (the IFS autoloader part) as a
   template. Header `0x10020`; V1 `qcfp` + V2 `pfcq(_type=8)`/`rrcq`; payload at `0x10020`.
   Replace the payload with the sealed image (`0x2BFE10`) + zero padding to `0x2C0000`.
   Patch the **V2 `rrcq.offset` to `0x20`** (path 1; the live MCT has no `0x39`).
   `Count = 44`.
2. **Bootrom → LDR_01** upload (`0xF009`/`0xF004`/`0xF00A`/`0xF005`), password `0000`.
3. **`PreFlash` `0x20EE`**, 36-byte descriptor (`[0]=0x15, [1]=0x28, [6]=0x02,
   [28]=0x01, [32]=0x02`). This loader answers **`0x20DE`**, not the `0x39` the Pascal
   client expects; accept `0x20DE` as success.
4. **181 × `0xF7`** frames `{u32 seq, u32 len, data}`, `0x3FEC` data each. Every frame
   returned `0xDF`.
5. **`0x40F9`** with the **image's real 560-byte trailer** (last `0x230` bytes of the sealed
   image, starts `QNXH-OS-1`), not the static `qcfm.pas` dummy. The dummy gets `0x4006`
   but `COMPLETE` then fails; the real trailer is the correct one. On this run the loader
   reset during/after the real-trailer step — treat the disconnect as success.
6. `0x40C0` COMPLETE is not reliable on this build: it returned `0x4015`/`0x14` with the
   dummy signature and was not reached with the real trailer (the loader reset). **The
   write is already on media**; the bootloader's CRC gate is the authoritative check.

After the reset the unit re-entered the bootrom, then booted TabletOS under factory mode.

## 2. Evidence

| check | result |
|---|---|
| media readback `hd1+0x100000`, `0x2BFE10` bytes | sha256 `c72b0530…` = `ifs0.sealed.bin` |
| `desc[0xD4]` on media | `6` |
| boot | TabletOS alive, `/base` intact (PreFlash did not wipe the RFS) |
| `BOOTCTR_FCTRY` | `0x31` after boot; topped to `0x32` |
| `0x2074` | unchanged V3, req=act=0, count=3, max=0 (COMPLETE did not write it) |

## 3. Why `bbusb.py` and not `bb10mt flash`

`bb10mt flash` builds a V2 container with `_type=8` and **`rrcq.offset=0`**, and relies on
the loader's **path 2** to resolve the part via an MCT entry of type `0x39`
(`PartitionID == 8`). The live MCT on this unit has **no `0x39` entries**, so path 2 cannot
resolve the IFS; path 1 with offset `0` would target the MBR (`p34.start = 0x805`).

`bb10mt flash` has no option to set `rrcq.offset`, so it cannot target block `0x825` on
this unit. Our tool:

- uses the real IFS autoloader container as a template (valid V1/V2 headers),
- patches `rrcq.offset = 0x20` for path 1,
- seals the image (`os_type=6` + CRC) with `89-crc_tool.py`,
- verifies the media by reading it back.

The command sequence itself is the same as `bb10mt` (`PreFlash`/`0xF7`/`0x40F9`/`0x40C0`);
adding an `--offset` option to `bb10mt flash` would make it usable here too.

## 4. Tooling

`analysis/pre_os_ifs_flash.py` (updated with the proven sequence):

```
python3 pre_os_ifs_flash.py --template-ifs <sealed IFS> [--dry-run]
```

It builds the container, gate-checks host-side, waits for the bootrom (or `--reuse-loader`),
sends PreFlash, the frames, the real signature, COMPLETE, and reboots. `--build-test`
writes a harmless 3-block pattern to the IFS-padding block `0x8D0`.

Recovery: the same tool with the original `2.0.0.4869-IFS.0.signed` payload (or
`2.0.0.4869-IFS.exe` split) restores the stock IFS; the bootrom path is independent of
region 0.

## 5. Residual

1. `0x40C0`'s `0x4015`/`0x14` code is not decoded; it did not prevent the write or boot.
2. The real-trailer `0x40F9` resets the loader; whether it is the signature write or a
   post-signature reset is not distinguished. The media and boot are correct.
3. `PreFlash 0x20DE` vs the Pascal client's `0x39`: this loader build differs. Accept both.
