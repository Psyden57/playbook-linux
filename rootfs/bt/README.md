# BT bring-up tools (WL1283, session 53)

Bench tools for the WL1283's Bluetooth core over UART2 (`/dev/ttyS1`, H4 at
115200 8N1).  They are **not yet installed by `tools/build_rootfs.py`** — build
one and push it to `/tmp` (dropbear has no `scp`):

```bash
. tools/env.sh
arm-linux-gcc -static -O2 -Wall -o /tmp/btdump rootfs/bt/btdump.c
ssh -i local/secrets/rsa root@169.254.0.1 'cat > /tmp/btdump' < /tmp/btdump
ssh -i local/secrets/rsa root@169.254.0.1 'chmod 755 /tmp/btdump'
```

`/tmp` is wiped by a reboot.  **Power-cycle `BT_EN` before a raw probe** (the
core sleeps and then answers nothing, which looks like a dead chip):

```sh
echo 0 > /sys/class/gpio/gpio535/value; sleep 2
echo 1 > /sys/class/gpio/gpio535/value; sleep 1
```

| tool | what it is for |
|---|---|
| `btdump <op> [dev] [ms] [param-hex]` | Send **one** HCI command and hexdump everything that comes back.  **Start here** — it does no parsing, so it cannot hide a reply. |
| `btsload [file.bts] [dev] [baud]` | Replay a TI `.bts` service pack over H4.  `--stay-awake` re-disables Cortex Sleep afterwards; `--probe-reset` checks whether `HCI_Reset` wipes the pack. |
| `btinq2 [dev] [secs]` | BR/EDR inquiry: reset, identity, one inquiry, decoded results. |
| `btgate [dev]` | Walk the LE gate: states / host-support / features in a fixed order, printing each status. |
| `btdetach [dev]` | Put the tty back to `N_TTY`.  **Does nothing** to a dead probe: `N_HCI` attaches to the *tty*, so killing `pbhci --attach` leaves it behind and the kernel eats every later byte. |

## What is established (report 238)

* The core does real BT: it answers `HCI_Reset`, reports itself
  (`hci ver 6`, `lmp ver 6`, `mfr 0x002b` = TI), and finds a nearby device in an
  inquiry.
* `WL1283.bts` loads **315/315** and moves the LMP subversion `0x2b0f` → `0x2b33`.
* Load it **before** attaching, and `hci0` registers with mgmt (0 → 1 controller).
  Without it, `LE_Read_Supported_States` returns a short reply, the kernel's LE
  init stage aborts, and the controller never registers.
* Powering still fails on a UART framing fault (`Frame reassembly failed (-84)`,
  `-EILSEQ`) that only happens through the kernel's H4 path — see report 238 §4.

## Three ways these tools lied, so you don't repeat it

1. **Buffered stdout throws the evidence away.**  A static ARM binary writing to a
   pipe is block-buffered; when `timeout` SIGTERMs it, every line is lost and the
   tool "looks silent".  Call `setvbuf(stdout, NULL, _IOLBF, 0)`.
2. **Command Complete and Command Status do not share a layout.**  `0x0e`:
   `04 0e len num op_lo op_hi status`.  `0x0f`:
   `04 0f len status num op_lo op_hi`.  Matching `ev[4]|ev[5]<<8` for both drops
   every asynchronous reply — including a perfectly good inquiry.
3. **A "silent" chip is usually a stale `N_HCI`.**  Check
   `cat /proc/tty/ldiscs` and run `btdetach` before concluding anything.
