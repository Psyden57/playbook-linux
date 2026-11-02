# Is ramloader opcode `0xDE` a security/RPMB query or `BOOT_MODE`?

**Status: resolved in favour of `BOOT_MODE` for the request/response protocol;
the exact data source of the `count` field is still partly open.**  Report `66`
§2 (session 1) lists `0xDE` as an undocumented "security/RPMB query"
returning `0xFA`, based on the static handler at `FUN_8011E074`.  Reports `72`
§3 (session 4) and `100` §2 (session 6) identify the same opcode as
`BOOT_MODE`: a channel-2 request `0xDE` with an 8-byte `{u32 action, u32 value}`
payload and a 12-byte response `{u32 status, u32 mode, u32 count}`.  The live
spare read returned `status=0, mode=FACTORY, count=0x32`.  This file exists
because report `66` is still in the tree and its one-line label is too narrow to
use for protocol work.

## The claims

**`66` §2 (session 1, 2026-10-20).**  In the implemented-opcode table:

> “`0xDE` | `0x80103b90` | `0xFA` | security/RPMB query (`FUN_8011E074`) |
> **undocumented**”

The narrative says only that `0xDE` is in the actions dispatcher and returns
`0xFA`; it does not decode the request or the 12-byte response.  No live `0xDE`
transaction was run in session 1.

**`72` §3 (session 4, 2026-10-21).**  The protocol is decoded from the loader
and live reads:

> “Channel 2, cmd `0xDE`, 8-byte `{u32 action, u32 value}`; response cmd
> `0xFA`, 12-byte `{u32 status, u32 mode, u32 count}`.  Actions: 0 read,
> 1 switch (1 = FACTORY, 0 = PRODUCT), 2 set count (1..50).  Status: 0 ok,
> 1 not allowed, 2 out of range, 3 wrong device mode.”

`72` §3.1 also identifies `FUN_8011E074` as `LDR_01`'s action-0 reader and notes
its `0xFFFFFFFF` sentinel: the expired unit’s reader reports `product/0` even
though the counter is `0xFFFFFFFF`, while the newer OMAP readers and the
bootloader reader return selector `2`.

**`100` §2 (session 6, 2026-10-26).**  The live probe on the spare 64 GB unit:

> “`0xDE` action 0 → `0xFA`, `status=0, mode=1 (FACTORY), count=0x32`.”

The same section gives the bootrom identity (`BR_ver 0x051B0014`, model
`0x06001A06`, password `0000`, salt `1392dc9f0976a887`, iterations `5810`).
Session 4’s working/reference unit read `status=0, mode=0 (product), count=0`.

**`71` §7c (session 3).**  The host-side CFP functions `FUN_0044d400` and
cfp-old `0x48BAE0` build the same `0xDE` request with `{action, value}`; no CLI
command invokes the setter.  The PlayBook loader’s `0xDE` is read-only in
practice — the setter/decrement helpers exist but have no callers.

## Evidence (each claim's own)

* **For `66` §2:** the static opcode table built in session 1 lists `0xDE` with
  dispatcher `0x80103B90` and response `0xFA`, and names the handler
  `FUN_8011E074`; no request/response layout was decoded and no live `0xDE`
  transaction was run, so the label is a *name*, not a decode
  (`docs/reports/66-ramloader-opcodes-and-sachesi-debug.md` §2).
* **For `72` §3/§3.1:** the request is decoded as `{u32 action, u32 value}` and the
  response as 12 bytes `{status, mode, count}` with documented action/status
  enumerations; `FUN_8011E074` is identified as `LDR_01`'s action-0 reader, with an
  explicit `value == 0xFFFFFFFF → 0` sentinel that the newer OMAP readers and the
  bootloader reader (`0x800090CE`) do not have
  (`docs/reports/72-loader-bootmode-action2.md` §3/§3.1).
* **For `100` §2:** one live transaction on the spare 64 GB unit — `0xDE` action 0
  → `0xFA`, `status=0, mode=1 (FACTORY), count=0x32` — and the contrasting
  reference-unit read `status=0, mode=0 (product), count=0` in session 4
  (`docs/reports/100-loader-probe-2026-10-26.md` §0/§2; `session-04.md` §2).
* **For `71` §7c:** the host-side CFP callers build the same `{action, value}`
  request, and no CLI command invokes the setter; the shipped loader's `0xDE` is
  read-only in practice (`docs/reports/71-hd0-mode-map-and-verifier.md` §7c).

## Why they cannot both be right

“Security/RPMB query” describes a *data source*, not the protocol.  The loader
returns 12 bytes, and those bytes decode exactly as `{status, mode, count}`;
the live values (`product/0`, `FACTORY/0x32`) match the boot-mode semantics, not
an arbitrary RPMB record dump.  `FUN_8011E074` is the action-0 reader, not a
generic RPMB record reader.  The `0xFA` response length and the action/value
fields are observed in the later decompilation.  At minimum, report `66`’s
one-line label is incomplete: `0xDE` is `BOOT_MODE`, and whatever record it reads
is an implementation detail.

The opposite reading — that `0xDE` is a security/RPMB query and the later
“BOOT_MODE” name is a mislabel — is not supported by the 12-byte response or
the live mode/count values.  The `0xDE` handler does live in the same
actions/security dispatcher as the NVRAM/RPMB operations, which is probably why
the early label said “security/RPMB”.

## What is still uncertain

* **Is the `count` field the RPMB `BOOTCTR_FCTRY` value?**  The spare’s
  `count=0x32` matches its factory-counter reading, and the reference’s
  `product/0` is consistent with “no factory counter”, but the same-boot
  correlation (`0xDE` count vs `/dev/rpmb` `BOOTCTR_FCTRY`) was not recorded as
  a single measurement.  The loader’s `0xFFFFFFFF` sentinel also means the
  action-0 read is **not** the bootloader’s selector for an invalid record.
* **Does `0xDE` read NVRAM or RPMB?**  `FUN_8011E074` and its callees were not
  traced to the backing record.  The response is a boot-mode blob; the source
  could be a dedicated boot-mode record or the RPMB counter.
* **Are actions 1/2 implemented in `LDR_01`?**  `72` §3/§5 says every shipped
  loader accepts action 0 only; actions 1/2 log and return without a response,
  and the setters are dead code.  Do not use `0xDE` as a pre-OS factory-counter
  writer.
* **Does the expired unit’s `0xDE` read mean anything?**  `72` §3.1 says
  `LDR_01` masks `0xFFFFFFFF` to 0 (“absent”); the bootloader reader returns 2.
  The read is not the bootloader’s selector for that sentinel.

## What would settle it

1. Re-disassemble `FUN_8011E074` and its callees in `LDR_01` (base
   `0x80100000`) and trace the record ID / RPMB service calls behind the
   12-byte response.  The report `66` evidence bundle and `72`’s per-loader
   decompiles are the starting points.
2. On a booted unit, run `0xDE` action 0 and read `/dev/rpmb`
   `BOOTCTR_FCTRY` in the same boot; compare `count` with the 4-byte record.
   Repeat on the reference (`product/0` pre-arm; it is now armed) — there is no
   spare left.  Do not write either value.
3. Try action 1/2 only on a sacrificial unit and only with a loader that
   actually accepts them; no shipped loader does, so this is not a recovery
   route.

Until then: **use `72`/`100` for the protocol and the live values; read report
`66`’s `0xDE` line as “the handler exists and returns `0xFA`”, not as its
identity.**  The `0xDE` count must not be assumed to be the bootloader’s
`BOOTCTR_FCTRY` selector without the same-boot correlation.

## References

* `docs/reports/66-ramloader-opcodes-and-sachesi-debug.md` §2 — the
  “security/RPMB query” label and the `FUN_8011E074` observation.
* `docs/reports/72-loader-bootmode-action2.md` §3/§3.1 — the `{action,value}` /
  `{status,mode,count}` protocol, the action-0 reader, the `0xFFFFFFFF`
  sentinel, and the read-only `0xDE`.
* `docs/reports/100-loader-probe-2026-10-26.md` §0/§2 — the live
  `FACTORY/0x32` read on the spare and the bootrom identity.
* `docs/reports/71-hd0-mode-map-and-verifier.md` §7c — the CFP `0xDE` host
  caller and the absence of a CLI setter.
