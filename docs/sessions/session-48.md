# Session 48 (absolute) — ssh over USB, and the session-45 diagnostics retired

> Numbering: absolute `48` (the project's chronological scheme — see
> `NUMBERING.md`).  Linux-era **session 42** in the reports' scheme
> (`absolute = Linux-era + 6`).

Date / span: 2026-11-14.
Unit state at the end: **on our Linux image `0x12C8C320`** (region 0,
`0x939D24` B, kernel `7.3.3` **#45** — the RNDIS + dropbear initramfs),
region 1 = the ssh rootfs **`a4e76b18…`** (67,108,864 B, 27 modules).
`0fca:8010`, console `pb> ` on `ttyACM0`, **and ssh on `169.254.0.1` over
`enp8s0f3u1i2`**.  `BOOTCTR_FCTRY` read `0x2a` at the boot after the flash and
`0x28` at the last boot of the session (each
`>= threshold 0x1e, no write`).  No NVRAM/MCT/RPMB write.  No oracle boot.
**No device-side file deployed**: the ssh key, the dropbear binary and its host
keys all live in the region-1 image, not on the UFS.

## Goal of this session

Two things, in order: ROADMAP item 9's small task — **retire the session-45
audio diagnostics and re-verify the audio** — and then the operator's request:
**get ssh working over USB**, because the ACM console is limiting.

## Confirmed (measured, read in a file, or seen on the bench)

* **Both audio diagnostics are gone from the running kernel.**  The current
  boot's `dmesg` has no `pcm: rate …`, no `pcm: no dma_area` and no
  `steady-state regs` / `live 0..7` lines, where the pre-flash boot had all of
  them.  `strings` on both staged `.ko` files and on the copies extracted from
  the flashed image: zero matches.
* **The audio still works after the cleanup** (operator heard the 440 Hz tone
  at DAC1 volume 40; three runs, ~3–6 s each):
  `repair: jack out (speaker)` → `repair: done, FLL1 locked`;
  both `pcm0p`/`pcm1p` `state: RUNNING` mid-stream;
  `XCR2 = 0x8041` in the McBSP register dump; `spk` out **lo** and `boost` out
  **hi** while the speaker stream runs; the data link wrote its full
  589 824 / 966 656 bytes with rc 0; `spk` back **hi** after the streams end.
* **The headphone branch works after the cleanup** (operator plugged the
  headphones in and asked for the check; 4 s at volume 40):
  `hp` reads **hi** and the repair took `repair: jack in (headphone)` →
  `repair: done, FLL1 locked`; both PCMs `RUNNING` mid-stream; the data link
  wrote its full 770 048 bytes, rc 0; **`spk` stayed high** throughout, which is
  the correct speaker-amp-off state for the headphone route.  **The operator
  heard the 440 Hz tone correctly in both ears.**  So the jack-dependent output
  stage survived the diagnostics removal on both branches.
* **The patch's 54-file export was missing a real source file.**
  `sound/soc/ti/omap-mcbsp-priv.h` is modified in the tree (it declares
  `active_rate`/`active_width`, which `omap-mcbsp.c`'s `hw_params` guard reads)
  but was in **no** export since session 41.  Applying the shipped patch to
  pristine 7.3.3 produced an `omap-mcbsp.c` that assigns and compares
  `mcbsp->active_rate` while nothing declares it — the patch could not compile.
* **The re-export is 55 files / 8 500 lines**, sha256 `f5856a8b…`, verified
  **0 rejects / 55/55 byte-identical** by apply-and-compare.
* **ssh works over USB, for commands and interactively.**  The gadget enumerates
  **four interfaces** (ACM + RNDIS) under `0fca:8010`; the host gets a net
  device `enp8s0f3u1i2` with MAC `02:1a:11:00:00:02`; `usb0` on the unit is
  `169.254.0.1/16` and `169.254.0.1:22` is dropbear.  `ping` 0.4 ms,
  `ssh` → `uid=0(root)`, `uname -r` = `7.3.3`.  **`tools/pbssh.sh 'cmd'` is
  2.3 MB/s** (8 MiB in 3.47 s), against the ACM console's 134 KB/s that drops
  bytes when the host blasts (trap 51).  **An interactive session works as
  well** — the operator's terminal gives `root@playbook:~#` with history and
  completion, and `script -qec "ssh …" /dev/null` reproduces it from here
  (trap 216).
* **The ACM console still works** — it is bound in the same configuration, and
  `tools/pbcon.py` was used throughout.  The card, the modules and the audio
  all came up on the new image.

## Inferred

* **The 55-file patch is the first one that would build from pristine alone.**
  Inferred from the header now being present with its hunks and from 55/55
  byte-identical; not proven by a full pristine build (the verification copy
  holds only the changed files, so it cannot `make`).
* **The omission has been latent since session 41** because every export since
  then was driven from the previous patch's file list (traps 106/161), and a
  missing file is absent from both sides of a byte-compare.  Only a tree walk
  finds it.
* **RNDIS is the right choice for a Windows host**, on the strength of the
  reference implementation and the in-box driver situation; it was tested on
  Linux here, not on Windows.

## Dead ends and wrong assumptions (valuable — do not skip)

* **"Drive the export from the previous patch's file list and byte-compare."**
  Both done, both passed, and the patch was still unbuildable — because the
  missing file was never on either list.  The walk is the check that catches
  this class; run it even when nothing new was added.
* **"A bare `pbplay` is harmless."**  `pbplay` with no arguments plays a 2 s
  440 Hz tone immediately, so a probe of its usage is a listening test.  It
  played at DAC1 volume 40 (unintended but quiet).
* **"Restore the volume when the first test passes."**  Restoring to 96 between
  runs put two later captures at full volume; the operator heard all of them.
  Correct order: **lower once at the start, run every test, restore once at the
  very end** — and treat `pbplay` itself as a playback command.
* **"A TERM check can tell ssh from the console."**  It cannot: the console's
  getty sets `TERM=vt102`, so a `$TERM` test gave the console an ssh-style
  prompt and broke `tools/pbcon.py`, which syncs on `pb> `.  The console looked
  dead for several minutes while it was working perfectly; recovered by
  sending `export PS1="pb> "` as raw bytes with `tools/pbraw.py` (trap 217).
* **"Interactive ssh is broken — the session closes with no output."**  It was
  not.  The agent's own shell has **no tty** (`/proc/self/fd/0 -> /dev/null`),
  so `ssh` found no local terminal, requested no pty, and the remote shell
  exited at once.  The operator's terminal works perfectly (prompt, history,
  completion), and `script -qec "ssh …" /dev/null` reproduces that from an
  agent shell.  This cost a long debugging detour through dropbear's pty code,
  every step of which was healthy.  **Check the harness before recording a
  defect** — trap 216.
* **"The pty path is the problem — prove it by patching dropbear."**  Three
  hypotheses about dropbear's pty code were disproven by instrumentation
  (pty allocation, `openpty` vs `/dev/ptmx`, `login_login`, `DO_MOTD`,
  `getrandom`), and the standalone reproductions all worked.  The lesson is the
  one this project keeps relearning: build the *smallest* reproduction of the
  failing step and run it on the device **before** patching the real binary —
  and make sure the reproduction is itself valid.
* **"A hung console means a wedged unit."**  Twice this session the console
  appeared dead and was fine: once behind a foreground command, once with the
  wrong prompt.  `tools/pbraw.py` one newline distinguishes all the cases.
* **"The `pb-rndis` profile will bind the new interface."**  It was pinned to
  `enp8s0f3u1`, and the composite device's net interface is
  `enp8s0f3u1i2`; the link came up on both sides and nothing was reachable until
  the profile was rebound (trap 214).
* **"`ip -br` works on this BusyBox."**  It does not; use `ip addr show dev
  usb0`.  Worth remembering, because the console is where such a command gets
  typed.
* **"Start the flasher after the reboot."**  The order is the reverse — start
  the flasher, *then* reboot — and running a dry run first left the unit
  booted with the flasher already finished, which is a wasted cycle at best.
  (Caught by the operator.)

## Unknown / open questions

* **Nothing about ssh.**  Commands and interactive sessions both work.  The
  one open question is a Windows/macOS host, below.
* **Windows and macOS were not tried.**  RNDIS is in-box on both, and the
  device's descriptors are the reference's shape, but only Linux was tested.
* **The `audio_boost` net (GPIO1_15)** is still behaviourally confirmed only.
* **MCLK1** remains optional and unused by the working design (trap 165).

## Superseded / uncertain

* **The session-47 wrap-up's "54 files / 8 584 lines, sha256 `93a7a8ce…`"** is
  superseded by 55 / 8 500 / `f5856a8b…`.
* **`STATE.md` and `HANDOFF.md`'s "still instrumented"** notes are now closed.
* **The session-48 mid-session state** (region 0 `0xAD85DF0F` + rootfs
  `af5a0a20…`, ACM console only) was real for the first half of the session and
  is superseded by the RNDIS image at the end.
* **Within this session, twice.**  (a) "interactive ssh is broken" — recorded in
  `STATE.md`, `HANDOFF.md`, `ROADMAP.md`, `DECISIONS.md` §62 and this note, then
  disproved by the operator and by `script -qec`; the correction is trap 216 and
  it is what those documents now say.  (b) "a `$TERM` test distinguishes the
  console from ssh" — implemented in `/etc/profile`, then disproved by
  `pbcon.py` losing the console; trap 217 has the correct discriminator.
  Both were corrected the same session, which is the point of the rule that a
  claim needs the artefact that proves it.

## Bench state inherited by session 49

| item | value |
|---|---|
| region 0 | **`0x12C8C320`**, `0x939D24` B, kernel `7.3.3` **#45** — the RNDIS + dropbear initramfs; saved as `local/images/kernel-0x12C8C320-rndis-ssh.img` (sha256 `9071272d…`), and that is also what `boot/chainloader/kernel.img` holds |
| region 1 | **`a4e76b18…`**, 67,108,864 B, 27 modules — `local/images/rootfs-region1-rndis-ssh.ext4` |
| OS running | **our Linux image** (`0fca:8010`).  Not TabletOS. |
| console | `pb> ` on `/dev/ttyACM0`, `0666` via the udev rule |
| ssh | **dropbear on `169.254.0.1:22`**, key-only root; host side `enp8s0f3u1i2` at `169.254.0.2/16`.  Commands and interactive sessions both work. |
| `BOOTCTR_FCTRY` | `0x28` at the last boot (`>= threshold 0x1e, no write`); self-replenishing |
| device-side files | **none** — the ssh key, dropbear and its host keys are in the region-1 image |
| way back to TabletOS | flash `local/work/ifs-shim-sealed.ifs` at block `0x825` (the pruned RCFS is still at `0x8D5`) |
| no restore needed | the unit is left on the image the operator wants |

## Artefacts produced

* **Machine driver** — `sound/soc/ti/playbook-wm8994.c`: the `pcm:` ring
  readback and the `substream` member it needed are removed.
* **McBSP driver** — `sound/soc/ti/omap-mcbsp.c` and
  `sound/soc/ti/omap-mcbsp-priv.h`: the `steady_dump` work item, its
  `INIT_WORK`, its `schedule_work` and the struct member are removed.
  (`omap-mcbsp-priv.h` is now in the patch, which is how the omission surfaced.)
* **Initramfs** — `initramfs/init.c`: the gadget gains the RNDIS function
  (`dev_addr`/`host_addr` set) beside ACM; the configuration string is
  `acm+rndis`.
* **Region-1 root** — `rootfs/etc/init.d/rcS` brings `usb0` up, mounts devpts
  with `gid=5,mode=620,ptmxmode=0666` (without the options no pty can be
  allocated at all), and starts dropbear; new `rootfs/etc/{passwd,group,shells}`;
  `rootfs/etc/profile` keeps `pb> ` on `/dev/ttyGS0` and gives ssh a normal
  prompt (trap 217); `tools/build_rootfs.py` gains
  `--dropbear`/`--hostkeys`/`--authorized-key` and installs them with the modes
  dropbear's `StrictModes` wants.
* **dropbear** — 2026.94, cross-built static for ARM (968 KB stripped) with
  password/PAM/shadow/utmp/wtmp, client forwarding, `DO_MOTD` and `getrandom`
  disabled, each for a stated reason in `localoptions.h`; host keys
  (ed25519 + rsa2048) generated on the host; binary and keys under
  `local/work/` (gitignored).  **Interactive sessions work** — an earlier
  inward conclusion that they did not was a harness artefact (trap 216).
* **One-off diagnostics** — `local/work/src/{ptytest,ptychild,ptyread,ptyrand,
  grtest,logintest}.c`: minimal static ARM probes for the pty machinery,
  `getrandom` and urandom-after-dup2.  All pass on the device.  They were
  written to chase a defect that did not exist, but they are the reason the
  *real* devpts defect (trap 218) was found, and they are reusable.
* **`tools/pbssh.sh`** — serves both the QNX door and dropbear
  (`PB_LINK=usb|door`, auto-detected by probing qconn 9090).
* **Images** — region 0 **`0x12C8C320`** (`0x939D24`, kernel #45);
  region 1 **`a4e76b18…`** (`local/images/rootfs-region1-rndis-ssh.ext4`).
  Both regions changed, so both were flashed in one bootrom session.
  The earlier session-48 image `af5a0a20…` (`…-audio-oss-linout.ext4`) and the
  session-47 one remain on disk; the **session-47 image `0342ab06…` was
  overwritten in place** by a `build_rootfs.py --out` in the same file (trap 213)
  — the operator's call is that the current rootfs is known-good, so it is not
  restored.
* **Patch** — 55 files / 8 500 lines, sha256 `f5856a8b…`, verified 0 rejects /
  55/55.
* **Docs** — the session note, traps 211–218, `DEVELOPMENT.md` §2/§3a (the
  ssh path and the dropbear build), `STATE.md`, `HANDOFF.md`,
  `ROADMAP.md` item 9, `KERNEL-PORT.md`, `ARCHITECTURE.md`
  (`kernel/source` scope), `DECISIONS.md` §59/§61/§62,
  `sessions/NUMBERING.md`, `kernel/patches/README.md`.

## Note for the next session

SSH over USB is the new bench default: `tools/pbssh.sh 'cmd'` for anything
substantial, the ACM console for recovery.  The WL1283 is the work, with the
method that solved the speaker (DECISIONS §61): static RE of the reference
stack in subagents, then running `devnp-ti1283` on the minimal oracle with its
own controls, then implementation in the driver/DTS.

