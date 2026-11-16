# Bench scripts — the ACM console and the input path

Small host- and device-side helpers from session 58.  They are tracked here
because the captures they produced are evidence; earlier sessions kept copies in
`/tmp` only.

| script | runs on | what |
|---|---|---|
| `acm.py` | host | drive `/dev/ttyACM0` directly: a marker line + a paced write (`acm.py '<cmd>' [timeout]`).  Needed because the Nura `ttyGS0` shell is not `pb> ` (trap 274).  One command per invocation. |
| `evcap.py` | device | read `/dev/input/event0` (16-byte `input_event`, 32-bit ARM) and print every event with a monotonic timestamp (`evcap.py <secs> <file>`). |
| `keylog.py` | device | set stdin raw and log every byte an application receives, with timestamps (`keylog.py <secs> <file>`) — the decisive tool for a repeat/drop. |
| `inj.py` | device | inject a command line into a terminal's pty with `TIOCSTI` (`inj.py "<cmd>"`); the foot shell is on `/dev/pts/0`.  Writing `/proc/<pid>/fd/<ptmx>` does **not** feed the terminal. |

| `mmccache.c` | host→device | build a static ARM `MMC_IOC_CMD` reader with `arm-linux-gcc -static -O2 -Wall`; `mmccache read\|off\|on\|dump` reads EXT_CSD (CMD8) and sets `CACHE_CTRL` (CMD6).  Session 59 used it to measure `CACHE_SIZE = 0` — the card has no cache (report 250). |
| `pb-touchwake.py` | device | watches `/dev/input/event0` for `BTN_TOUCH` down and runs `catacomb msg dpms on` when `bl_power != 0` (session 59; trap 298).  Installed at `/usr/bin/pb-touchwake` and launched by catacomb's initrc; needs the session user in the `input` group. |

The device-side scripts go to `/tmp` on the unit.  **Push them paced**
(8 KiB chunks / 5 ms; trap 302) — an unpaced RNDIS burst truncates.  The device
receives with `nc -l -p <port> > /tmp/<name>`.

`python3` exists on the Nura root; `acm.py` needs the host's `/dev/ttyACM0`
(0666).
