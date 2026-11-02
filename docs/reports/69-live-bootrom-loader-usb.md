# 69 — Live bootrom/loader USB: handshake, hand-off, loader protocol, NVRAM 0x2074, EP1

Status: live-device work on items (1) and (2) of the `HANDOFF.md` next-work list.
Date: 2026-10-20. Device: BlackBerry PlayBook, OMAP4430 Winchester ES2.2 HS,
QNX 6.6, as in `65-live-playbook-sar-iram.md`.

Provenance: every result below is a **live USB observation** on the unit, not
REA Evidence and not static-analysis output. Supporting static analysis of the
ramloader is cited from `66-ramloader-opcodes-and-sachesi-debug.md` and the
portable loader Evidence bundle.

Artifacts:

| file | contents |
|---|---|
| `rea-ghidra/playbook-usb-probe.py` | working client (`watch`, `metrics`, `e9`, `ep1`) |
| `playbook-live-2026-10-20/bootrom-probe.log` | full client session log |
| `playbook-live-2026-10-20/bb10mt-urb-trace.log` | byte-level URB trace of the working bb10mt reference (`info`) |
| `rea-ghidra/sachesi-debug-harness/` | CLI harness built from Sachesi 2.0.3's own `boot.cpp` |

Target loader: `loader_06001A06-00.bin`, 238,128 bytes, base `0x80100000`,
SHA-256 `c7cc33c796f9518bbc0eb929c6096f2473937ad486c579764aa4c57d8165393f`.
It is byte-identical to `LDR_01` in `bb10mt/loaders.dat` (extracted and hashed).

## 1. Access and enumeration

- Host interface `enp8s0f3u1` is `169.254.0.2/16`; the running OS enumerates as
  `0fca:8010`. Bootrom is `0fca:0001`, ramloader is `0fca:8001`.
- USB nodes default to `root:root 0664`; a scoped udev rule is required:
  `SUBSYSTEM=="usb", ATTR{idVendor}=="0fca", ATTR{idProduct}=="0001"/"8001", MODE="0666"`.
- The bootrom window is the first seconds of a power-on. A bootrom instance that
  has been used and abandoned refuses `SETMODE` (`0x09`) and eventually stops
  accepting channel-0 writes, so every clean attempt needs a fresh power-cycle
  and a client that is already polling.

## 2. Control framing

The 8-byte header matches Sachesi/busb:

```
u16 type (=0) | u16 total | u8 command | u8 mode | u16 packetId | data...
```

`total` is the whole frame length (data + 8). Channel 1/2 wrap the same inner
layout with `u16 channel | u16 total(=inner+4) | inner`.

Bootrom descriptors: one vendor-specific interface (`ff/01/ff`, "BlackBerry"),
four bulk endpoints, all 64-byte max packet: `0x81`/`0x82` IN, `0x01`/`0x02`
OUT. Normal control uses EP2; the ramloader re-enumerates with 512-byte packets.

## 3. Bootrom handshake (all confirmed live)

| step | request | response |
|---|---|---|
| PING | ch0 `0x01` + `14 05 83 19 00 00 00 00` | `0x02`, 8 bytes |
| GETVAR(2) | ch0 `0x05` + `{2000, 2}` | `0x06`, 736 bytes |
| SETMODE(1) | ch0 `0x07` + 16-byte **or** 17-byte `"RIM-BootLoader"` | `0x08` |
| PASSWORD_INFO | ch0 `0x0A` | `0x0E`, 24-byte challenge |
| SEND_PASSWORD | ch0 `0x0F` + `00 00 40 00` + HashPassV2 | `0x10` |

- The device password is **`0000`**. The challenge is always issued:
  `challenge = data[4:8]`, `salt = data[12:20]`, `iterations = u32(data[20:24])`
  = **5060**, attempts `10/10`. Empty password is rejected.
- Both SETMODE payload forms are accepted on EP2: Sachesi sends 16 bytes
  (`packetSize = 24`), bb10mt sends 17 (`packetSize = 25`).
- After `0x10` the device queues a channel-0 `0x13` ReadyForDataTransfer frame.
  bb10mt's `PasswordInfo` consumes it with an extra read; do the same.
- Metrics: hardware ID `0x06001A06` is at payload offset **16**
  (`TBRMetrics` starts at payload+4); `"RIM BlackBerry Device"` follows.
- Every channel-1 command is followed by a `0x13` frame (mode byte 1).

## 4. Loader upload and hand-off (the missing piece)

Upload: `0xF009` setup `{addr=0x80100000, size=238128}`, 916 × `0xF004`
260-byte chunks, then `0xF00A` verify. The whole 238,128 bytes transfer in
**0.8 s**; every chunk is acknowledged.

`0xF005` EXECUTE_RAM (`u16 cmd, u32 addr`) is acknowledged, but it does **not**
transfer control by itself. **The bootrom then expects one further read.**

bb10mt's `RunLoader` performs `Channel1` (response + one extra frame) and then
one more `ReadData`. That **third read returns `0b 01 01 00`** (cmd `0x0B`,
mode `0x01`, packetId `0x0001`). Only after it does the bootrom jump: the unit
stops responding as `0fca:0001` and re-enumerates as **`0fca:8001`** about 3–4 s
after `0xF005`.

Without that third read the bootrom stays alive and nothing happens. This was
the sole difference between the working reference and the first client.

`0x0B` LoadTransferredData on channel 0 returns `0x0C` but **reboots** the unit
when no transferred-data session is pending (Sachesi's source warns exactly
this); it is not an alternative hand-off.

## 5. Ramloader protocol

After `0fca:8001` appears:

1. `SETMODE(2)` ("RIM-RAMLoader") on channel 0 → `0x08`.
2. `PASSWORD_INFO` → `0x10` immediately (no challenge); one extra read follows.
3. **Drain the paged Bugdisp log before any other channel-2 command.**
   `0xB0` + 8 zero bytes returns `0xB5` pages of 512 bytes; repeat until the
   response is `0xD0`. On this unit the log was **>64 pages** (it grows with
   each session). If it is not fully drained, every later response is a stale
   `0xB5` page and all commands appear to return the same 512-byte blob.

Then the normal queries work (all channel-2, no data unless stated):

| op | response | meaning |
|---|---|---|
| `0xB4` | `0xD2`, 188/200 B | flash regions (Samsung `MCGAFA`) |
| `0xE7` | `0xD1`, 4 B | PIN `0x50175127` |
| `0xD1` | `0xC8`, 4 B | SVN `0` |

## 6. NVRAM record `0x2074` — item (1) baseline

`0xE9` reads NVRAM record `0x2074` (24 bytes) and returns command `0xFB`.

**The handler requires the first payload dword to be zero.** With a zero-length
request it reads the frame's CRC bytes as that dword, fails the check, and
returns `0x15` with payload `0x04`. With a **4-byte zero payload** the read
succeeds.

Observed result (live, before any CFP run):

```
resp = 0xFB, payload = 28 bytes
status dword = 0x00000000            (success)
record[0:24] = 03 00 00 00 00 00 75 01 00 00 00 00
               00 00 00 00 00 00 00 00 00 00 00 00
```

Decoded: record dword 0 = **3**; u16 at +4 = 0; bytes +6/+7 = `0x75 0x01`
(little-endian `0x0175`); remaining 16 bytes zero. `66` §3.5 describes the
COMPLETE-written form as `{2, 0, byte}`; this unit reads `{3, 0, 0x0175, ...}`,
so the pre-CFP state is distinguishable from that written form. This is the
baseline needed for the before/after `resetqnxosregion` comparison. No write
(`0xC0`/`0xE0`) was attempted.

## 7. EP1 / Sachesi Debug Mode — item (2), answered

Tested two ways: the independent probe, and a CLI harness built from Sachesi's
own `boot.cpp` so the exact upstream code path runs (`_found = 8000`, EP1
`0x01`/`0x81`, 16-byte payload).

Result, on a fresh bootrom, with and without a preceding normal EP2
`SETMODE(1)`:

- The first EP1 `SETMODE(1)` OUT (24 bytes on the wire) is **accepted**.
- Nothing is returned on EP1 IN `0x81` — libusb times out (`-7`).
- The next EP1 OUT (`0x11`, or a repeat setmode) **times out**; the endpoint is
  no longer serviced.
- Sachesi's own code then prints "Try rebooting your device manually." and
  reuses the stale stage-1 response buffer, which is why it can appear to
  "succeed" in a GUI trace.

Conclusion: **the Debug Mode EP1 transport is not a usable control channel on
the PlayBook bootrom**. `kCommandUnkn1 = 0x11` can never be delivered on a live
transaction. This is a dead end, not a hidden console. Normal EP2 works.

## 8. Corrections to earlier documents

- `66` §3.5: `0xE9` needs a **4-byte zero payload**. Without it the loader
  returns `0x15/0x04`, which reads like "opcode unimplemented" but is the
  handler's argument check.
- `66` §1: `0xF005` alone is not the hand-off. The client must perform the
  **third read** after the channel-1 response pair; that read carries the
  `0b 01 01 00` go-ahead and is followed by `0fca:8001`.
- Loader clients must drain the **paged `0xB0`/`0xB5` log** (until `0xD0`)
  before issuing other channel-2 commands.
- `69` earlier revision: the 16-byte SETMODE form is accepted on EP2;
  only the EP1 debug transport fails.

## 9. Still open

- The `0xC0` COMPLETE / `0xE0` installer path was **not** exercised; it writes
  NVRAM/flash and needs the CFP-equivalent backup and recovery plan. The
  `0x2074` baseline above is what a CFP `resetqnxosregion` run should be
  compared against.
- `0xE9` was read once; a second read after a known no-op would confirm the
  status dword and record are stable.
- The hidden `0x40027000` page and the secure effect of `0xF0`/`0xFE`/`0xF1`
  remain unreadable; nothing here changes that.

## 10. Safety

No flash, NVRAM, RPMB or boot-chain write was performed. The loader was written
to RAM only; `0x0B` (when tested) rebooted the unit normally. Only read-only
loader opcodes (`0xB0`, `0xB4`, `0xE7`, `0xD1`, `0xE9`) were issued.
