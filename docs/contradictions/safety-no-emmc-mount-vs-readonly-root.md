# May Linux mount the eMMC read-only?

**Status: resolved (session 17), extended (session 18) — read-only mounts are
allowed, and the operator has now dropped the blanket prohibition on writable
eMMC *for our own data*.**  NVRAM, the MCT, and RPMB changes other than the
verified `BOOTCTR_FCTRY` top-up remain forbidden (`SAFETY.md` §4/§6).

## Session 18 update

The operator decided to make the unit independent of QNX and repurpose `hd3`
(the MCT type-`0x1C` partition) as a Linux/postmarketOS root.  The old rule
"**never** write the eMMC" was a proxy for "do not write QNX's filesystem"; it
is now replaced by a scoped rule: **writable eMMC is allowed for our own
data** (region 1, and `hd3` once repurposed), while **NVRAM stays read-only**,
the **MCT is never rewritten** (`0xE0`/`0xF1`/`0xF2`/`0xF3` are the brick path),
and the only RPMB write we contemplate is the verified `BOOTCTR_FCTRY = 0x32`
top-up, and only once the Linux path is proven (report 141, in progress).  The
way back is the vendor UFS image plus the sealed IFS/RCFS.  See `SAFETY.md` §4
and §6.

## Resolution

The operator decided explicitly: "I approve of the read-only mount on linux."
The read-only case is therefore allowed, with the operator's approval, and it
must be read-only at the **block layer** (the console's `qnx6` uses a
`LO_FLAGS_READ_ONLY` loop over `/dev/mmcblk1` and mounts `MS_RDONLY`).  A
writable mount of **QNX's** filesystem remains forbidden except for the
deliberate `hd3` repurpose; a writable mount of **our own** ext4 is allowed under
the session-18 decision.  The rule is now in `SAFETY.md` §4/§6 and
`DECISIONS.md` §17/§18.

The decision was exercised in report 138: hd3 was mounted read-only at raw
`0x49000000` to diagnose the reseed (the navigator store, versions and logs were
read; no write was made through the mount).

## The claims

**No (canonical safety rule).** `docs/project/SAFETY.md` §4, "Media, regions, and
the only write path we use":

> **Never**: mount or write the eMMC from Linux; write NVRAM; issue `0xE0` …

It lists *mounting* with writing, without distinguishing read-only from
writable.

**Yes, read-only (the stated next milestone).** `docs/project/HANDOFF.md` §1
(session 15) — "Then the one thing to do next" — says to mount `mmcblk1`
read-only as a root, and `docs/project/ROADMAP.md` §5 closes item 5 when "the
kernel mounts a real root filesystem from the eMMC".  The session-16 kickoff
prompt repeats it ("get a minimal root on mmcblk1") while its DO NOT list forbids
only *writing* RPMB, the eMMC and NVRAM.

## Evidence for each

* The staging rule exists because a writable mount is indistinguishable, in the
  old docs, from a write to the media: the agent must never create the chance of
  an unintended write to TabletOS's filesystem, NVRAM or RPMB.
* A read-only *block* read is obviously safe and is used everywhere (the
  session-16 proof reads and hashes 11 MB from `/dev/mmcblk1` with no mount).
* Not every "read-only mount" is write-free: `ext4` replays its journal on a
  `ro` mount unless `noload` is given, i.e. it can write.  A filesystem that is
  read-only by implementation (Linux's `qnx6` has no write path) cannot.
* Mounting `mmcblk1` as a whole device requires a byte offset for the MCT
  partitions; the verified mapping is in report 134 §5.

## What is uncertain

*This was the open question at the time; it is resolved by the operator's
session-17/18 decisions above and kept as history.*  Whether the safety rule was
meant literally ("no mount at all, ever") or as a proxy for "no writes through a
mount".  The two session-15 documents contradicted each other on it.

## What settled it

An explicit operator decision, recorded in `SAFETY.md`: read-only mounts are
allowed (block-layer read-only), writable eMMC is allowed for **our own** data,
and writable QNX filesystems only for the deliberate `hd3` repurpose.  Session 16
had only done raw read-only block reads; report 138 then exercised the read-only
mount.
