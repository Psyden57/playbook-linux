#!/usr/bin/env python3
"""
BerryShell-V4 — non-interactive qconn door keeper for BlackBerry PlayBook / BB10.

Derived from the author's own earlier BerryShell-V3.py (not a third-party
project; V3 is not in this repository). The qconn pairing protocol layer below (pb_hash,
pack_frame, aes_encrypt, conduct_handshake, the frame code sequence, the
RSA-1024 e=65537 handshake key, the PKCS1v15/SHA1 signature, the [:720] pubkey
slice) is copied VERBATIM from V3 — it is a hard-won reverse-engineered pairing
flow. Do NOT edit, "fix", or reinterpret any wire bytes in it.

Agent-facing modes (never interactive, never read stdin):
  auth   connect + handshake + upload SSH key, close cleanly, exit 0
  exec   handshake + key upload, heartbeat thread in background, run ONE
         non-interactive SSH command, print its output, exit with its code
  hold   handshake + key upload, heartbeat every 4 s FOREVER, re-handshake on
         connection loss; runs until SIGINT/SIGTERM. While it runs, plain
         `ssh -i KEY root@IP ...` and `scp` work against the device.

Examples:
  BerryShell-V4.py auth
  BerryShell-V4.py exec "uname -a"
  BerryShell-V4.py exec -- ls -l /var
  BerryShell-V4.py --log /tmp/door.log hold
"""

import socket
import struct
import hashlib
import binascii
import os
import sys
import time
import subprocess
import argparse
import threading
import signal
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

# Config
DEFAULT_IP = "169.254.0.1"
DEFAULT_USER = "root"
DEFAULT_PASSWORD = "0000"
# Resolved from this file's location so a clone works anywhere; PB_KEY / PB_PUB
# override.  The key pair is created by `--generate` or supplied by the operator.
_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_KEY = os.environ.get("PB_KEY", str(_ROOT / "local" / "secrets" / "rsa"))
DEFAULT_PUB = os.environ.get("PB_PUB", str(_ROOT / "local" / "secrets" / "rsa.pub"))
HEARTBEAT_INTERVAL = 4.0  # seconds; frame(6) heartbeat cadence

# SSH option set — reused exactly from V3's ssh_cmd (minus -tt, which must NOT
# be used in exec mode: the command has to run without a TTY).
SSH_OPTIONS = (
    "-o", "StrictHostKeyChecking=no",
    "-o", "UserKnownHostsFile=/dev/null",
    "-o", "IdentitiesOnly=yes",
    "-o", "PreferredAuthentications=publickey,password,keyboard-interactive",
    "-o", "HostKeyAlgorithms=+ssh-rsa",
    "-o", "PubkeyAcceptedKeyTypes=+ssh-rsa",
    "-o", "PubkeyAcceptedAlgorithms=+ssh-rsa",
    "-o", "KexAlgorithms=+diffie-hellman-group1-sha1",
    "-o", "MACs=+hmac-sha1,hmac-sha1-96",
    "-o", "Ciphers=+aes128-cbc,3des-cbc,aes256-cbc",
    "-o", "LogLevel=ERROR",
)

# ---------------------------------------------------------------------------
# Logging: UTC-timestamped, flushed (python3 -u-safe), tee'd to --log file.
# ---------------------------------------------------------------------------
_LOG_FH = None
_LOG_LOCK = threading.Lock()


def log(msg):
    line = "[{}] {}".format(time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), msg)
    with _LOG_LOCK:
        try:
            sys.stdout.write(line + "\n")
            sys.stdout.flush()
        except Exception:
            pass
        if _LOG_FH is not None:
            try:
                _LOG_FH.write(line + "\n")
                _LOG_FH.flush()
            except Exception:
                pass


def setup_log_file(path):
    global _LOG_FH
    try:
        _LOG_FH = open(path, "a", buffering=1, encoding="utf-8")
        log(f"[*] Logging to {path}")
    except OSError as e:
        _LOG_FH = None
        log(f"[!] Cannot open log file {path}: {e}")
        sys.exit(2)


def fix_key_permissions(keyfile):
    if not os.path.exists(keyfile): return

    if sys.platform == "win32":
        try:
            username = os.getenv("USERNAME")
            abs_key = os.path.abspath(keyfile)
            os.system(f'icacls "{abs_key}" /reset >nul 2>&1')
            os.system(f'icacls "{abs_key}" /inheritance:r /grant:r "{username}":F >nul 2>&1')
        except: 
            pass
    else:
        try:
            os.chmod(keyfile, 0o600)
        except Exception as e:
            log(f"[!] Warning: Could not set permissions on {keyfile}: {e}")


def generate_keys_if_missing(key_file, pub_file):
    # Adapted from V3: explicit paths instead of module globals; same key
    # parameters (e=3, 4096 bits) and same either-file-missing -> regenerate
    # the pair semantics as V3.
    if not os.path.exists(key_file) or not os.path.exists(pub_file):
        log("[*] Generating new RSA keypair...")
        
        key = rsa.generate_private_key(public_exponent=3, key_size=4096)
        
        with open(key_file, "wb") as f:
            f.write(key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption()
            ))
        
        pub = key.public_key()
        with open(pub_file, "wb") as f:
            f.write(pub.public_bytes(
                encoding=serialization.Encoding.OpenSSH,
                format=serialization.PublicFormat.OpenSSH
            ))
        
    fix_key_permissions(key_file)


def ensure_keys(cfg):
    missing = [p for p in (cfg.key, cfg.pub) if not os.path.exists(p)]
    if missing:
        if cfg.generate:
            log(f"[*] Missing key file(s): {', '.join(missing)}")
            generate_keys_if_missing(cfg.key, cfg.pub)
        else:
            log(f"[!] Missing key file(s): {', '.join(missing)}")
            log(f"[!] Re-run with --generate to create the keypair ({cfg.key} / {cfg.pub}), or pass --key/--pub explicitly.")
            return False
    else:
        fix_key_permissions(cfg.key)
    return True


# ---------------------------------------------------------------------------
# PROTOCOL LAYER — copied VERBATIM from BerryShell-V3.py. Do not edit.
# Only print() was replaced with log() (UTC timestamps + --log tee); the wire
# bytes and their ordering are untouched.
# ---------------------------------------------------------------------------

def pb_hash(password, salt, iterations, challenge):
    hashed_data = password.encode("latin-1")
    count = 0
    while count < iterations:
        buf = struct.pack("<I", count) + salt + hashed_data
        hashed_data = hashlib.sha512(buf).digest()
        count += 1
    hashed_data = challenge + hashed_data
    count = 0
    while count < iterations:
        buf = struct.pack("<I", count) + salt + hashed_data
        hashed_data = hashlib.sha512(buf).digest()
        count += 1
    return binascii.hexlify(hashed_data).upper()

class QCONNDoor:
    def __init__(self, ip, password, pub_file):
        self.ip = ip
        self.password = password
        self.sock = None
        self.handshake_key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        
        with open(pub_file, "rb") as f:
            self.pub_to_upload = f.read().strip()[:720]
            
        self.session_key = None
        self.cstate = 0 

    def pack_frame(self, code, data=b""):
        return struct.pack(">hhh", 6 + len(data), 2, code) + data

    def aes_encrypt(self, plain):
        iv = os.urandom(16)
        pad_len = 16 - (len(plain) % 16)
        padded_plain = plain + bytes([pad_len] * pad_len)
        cipher = Cipher(algorithms.AES(self.session_key), modes.CBC(iv))
        encryptor = cipher.encryptor()
        ct = encryptor.update(padded_plain) + encryptor.finalize()
        return struct.pack(">hh", len(ct), len(plain)) + iv + ct

    def conduct_handshake(self):
        log(f"[*] Looking for QCONN door on {self.ip}:4455...")
        
        # Infinite retry loop to wait for device to boot/become reachable
        while True:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(5) # Shorter timeout for the retries
                self.sock.connect((self.ip, 4455))
                self.sock.settimeout(10) # Restore normal timeout for handshake
                # Detect a peer reboot quickly: without keepalives a half-open
                # TCP connection keeps accepting heartbeats into a dead socket,
                # so the holder never re-handshakes when the device comes back.
                try:
                    self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 2)
                    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 2)
                    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 2)
                except (AttributeError, OSError):
                    pass
                # The 4 s heartbeat keeps the connection non-idle, so TCP
                # keepalive alone never probes.  USER_TIMEOUT bounds how long
                # unacknowledged data may sit before the socket is failed.
                try:
                    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_USER_TIMEOUT, 4000)
                except (AttributeError, OSError):
                    pass
                break # Success, exit retry loop
            except OSError as e:
                # Use \r to overwrite the same line instead of spamming the console
                sys.stdout.write(f"\r[-] Unreachable. Retrying in 3s... ({e})".ljust(80))
                sys.stdout.flush()
                try:
                    time.sleep(3)
                except KeyboardInterrupt:
                    log("\n[*] Cancelled by user.")
                    sys.exit(0)

        log("\n[*] Connected! Authenticating...")
        
        self.cstate = 1 
        self.sock.send(self.pack_frame(1))

        while self.cstate < 6:
            header = self.sock.recv(6)
            if not header: break
            pkt_len, ver, code = struct.unpack(">hhh", header)
            payload = self.sock.recv(pkt_len - 6)

            if code == 2:
                if pkt_len == 14:
                    self.cstate = 2
                    n = self.handshake_key.public_key().public_numbers().n
                    self.sock.send(self.pack_frame(3, struct.pack(">h", 128) + n.to_bytes(128, 'big')))
                elif pkt_len == 17:
                    self.cstate += 1
                    if self.cstate == 3:
                        self.sock.send(self.pack_frame(8))
                    elif self.cstate == 4:
                        self.sock.send(self.pack_frame(7, self.aes_encrypt(struct.pack(">h", len(self.pub_to_upload)) + self.pub_to_upload)))
                    elif self.cstate == 5:
                        self.sock.send(self.pack_frame(11))
                    elif self.cstate == 6:
                        self.sock.send(self.pack_frame(6))
                        log("[+] Authenticated. Key uploaded.")

            elif code == 4:
                source_len = struct.unpack("<H", payload[4:6])[0]
                container_len = struct.unpack("<H", payload[10:12])[0]
                blob_offset = 24 + source_len
                enc_blob = payload[blob_offset : blob_offset + container_len]
                server_challenge = self.handshake_key.decrypt(enc_blob, padding.PKCS1v15())
                chal_buf = server_challenge[:30] + bytes([3, 4, 118, 131, 1])
                self.session_key = chal_buf[8:24]
                signature = self.handshake_key.sign(chal_buf, padding.PKCS1v15(), hashes.SHA1())
                reply = struct.pack(">hhh", 4 + len(chal_buf) + len(signature), len(chal_buf), len(signature)) + chal_buf + signature
                self.sock.send(self.pack_frame(5, self.aes_encrypt(reply)))

            elif code == 9:
                _, iterations, slen, clen = struct.unpack(">IIhh", payload[:12])
                salt = payload[12:12+slen]
                challenge = payload[12+slen:12+slen+clen]
                auth_hash = pb_hash(self.password, salt, iterations, challenge)
                self.sock.send(self.pack_frame(10, self.aes_encrypt(struct.pack(">h", len(auth_hash)) + auth_hash)))

    def send_heartbeat(self):
        try:
            if self.sock:
                self.sock.send(self.pack_frame(6))
                # A half-open connection can still accept small sends, so
                # peek non-blockingly for EOF/RST as well.
                try:
                    pending = self.sock.recv(1, socket.MSG_PEEK | socket.MSG_DONTWAIT)
                    if pending == b"":
                        raise ConnectionResetError("door closed (EOF)")
                except BlockingIOError:
                    pass
        except (BrokenPipeError, ConnectionResetError, OSError):
            log("[!] Connection lost sending heartbeat.")
            raise

    def close(self):
        try:
            if self.sock:
                self.sock.close()
            log("[*] QCONN Door closed.")
        except: pass

# ---------------------------------------------------------------------------
# END PROTOCOL LAYER
# ---------------------------------------------------------------------------

class Heartbeat(threading.Thread):
    """Sends frame(6) every 4 s in the background.

    On send failure the thread marks the door dead and stops:
      - hold mode: the main loop sees self.dead and re-handshakes;
      - exec mode: nothing else happens, the ssh exit code decides the outcome.
    """
    def __init__(self, door, interval=HEARTBEAT_INTERVAL):
        super().__init__(daemon=True, name="qconn-heartbeat")
        self.door = door
        self.interval = interval
        self.stop_evt = threading.Event()
        self.dead = threading.Event()

    def run(self):
        log(f"[*] Heartbeat thread started (frame 6 every {self.interval:g}s).")
        while not self.stop_evt.wait(self.interval):
            try:
                self.door.send_heartbeat()
            except Exception as e:
                log(f"[!] Connection lost sending heartbeat: {e}")
                self.dead.set()
                break
        log("[*] Heartbeat thread stopped.")

    def stop(self):
        self.stop_evt.set()


def build_ssh_cmd(cfg, remote):
    return ["ssh", "-i", cfg.key] + list(SSH_OPTIONS) + [f"{cfg.user}@{cfg.ip}", remote]


def join_remote(tokens):
    """Join argparse REMAINDER tokens into one remote shell string.
    A leading '--' separator (if argparse kept it) is stripped."""
    tokens = list(tokens)
    if tokens and tokens[0] == "--":
        tokens = tokens[1:]
    return " ".join(tokens).strip()


def handshake_failed(door):
    log("[!] Handshake ended before authentication completed (wrong password or door refused).")
    return 3


def mode_auth(cfg):
    door = QCONNDoor(cfg.ip, cfg.password, cfg.pub)
    try:
        door.conduct_handshake()
        if door.cstate < 6:
            return handshake_failed(door)
        log("[+] auth mode complete: door opened and key uploaded; closing.")
        return 0
    except KeyboardInterrupt:
        log("[!] Interrupted during auth.")
        return 128 + (_LAST_SIGNAL["signum"] or 2)
    finally:
        door.close()


def mode_exec(cfg):
    remote = join_remote(cfg.remote)
    if not remote:
        log("[!] exec: no remote command given. Example: BerryShell-V4.py exec \"uname -a\"")
        return 2

    door = QCONNDoor(cfg.ip, cfg.password, cfg.pub)
    hb = None
    try:
        door.conduct_handshake()
        if door.cstate < 6:
            return handshake_failed(door)
        log("[*] Preparing SSH...")
        time.sleep(2)  # V3 lets the door settle before SSH; kept
        hb = Heartbeat(door)
        hb.start()
        ssh_cmd = build_ssh_cmd(cfg, remote)
        log(f"[*] Exec on {cfg.user}@{cfg.ip}: {remote}")
        proc = subprocess.Popen(ssh_cmd, stdin=subprocess.DEVNULL)
        try:
            rc = proc.wait()
        except KeyboardInterrupt:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            raise
        log(f"[*] SSH command exited with code {rc}.")
        return rc
    except KeyboardInterrupt:
        log("[!] Interrupted while running exec.")
        return 128 + (_LAST_SIGNAL["signum"] or 2)
    finally:
        if hb is not None:
            hb.stop()
            hb.join(timeout=2)
        door.close()


def mode_hold(cfg):
    """Robust door keeper: NEVER exits on transient failures (device reboots,
    mid-handshake drops, socket timeouts) — every attempt is wrapped and
    retried with a fixed delay. Exits 0 only on SIGINT/SIGTERM."""
    door = None
    hb = None
    fails = 0
    try:
        while True:
            try:
                door = QCONNDoor(cfg.ip, cfg.password, cfg.pub)
                door.conduct_handshake()  # loops on connect failure (V3 behavior)
                if door.cstate < 6:
                    # The door accepted the connection then dropped before auth
                    # completed (device rebooting mid-handshake, wrong password,
                    # ...). A keeper must ride these out — log and retry.
                    log("[!] Handshake ended before authentication (wrong password? device dropping?); retrying...")
                    door.close()
                    door = None
                    time.sleep(3)
                    continue
                fails = 0
                hb = Heartbeat(door)
                hb.start()
                log(f"[*] Holding door open; plain 'ssh -i {cfg.key} {cfg.user}@{cfg.ip} ...' and scp work now. Ctrl-C to stop.")
                while not hb.dead.wait(0.5):
                    pass
                # Door died: stop heartbeats, close, re-handshake.
                hb.stop()
                hb.join(timeout=2)
                hb = None
                door.close()
                door = None
                log("[*] Door lost; re-handshaking in 1s...")
                time.sleep(1)
            except KeyboardInterrupt:
                raise
            except Exception as e:
                fails += 1
                log(f"[!] Door attempt failed ({e!r}); retry #{fails} in 3s...")
                if hb is not None:
                    hb.stop()
                    hb.join(timeout=2)
                    hb = None
                if door is not None:
                    try: door.close()
                    except Exception: pass
                    door = None
                time.sleep(3)
    except KeyboardInterrupt:
        log("[*] Door keeper shutting down.")
        return 0
    finally:
        if hb is not None:
            hb.stop()
            hb.join(timeout=2)
        if door is not None:
            door.close()


# Signal plumbing: SIGTERM is mapped to KeyboardInterrupt so hold/exec shut
# down cleanly (V3 already handles KeyboardInterrupt during connect retries).
# SIGINT keeps its default KeyboardInterrupt behaviour.
_LAST_SIGNAL = {"signum": None}


def _signal_to_keyboardinterrupt(signum, frame):
    _LAST_SIGNAL["signum"] = signum
    raise KeyboardInterrupt


def install_signal_handlers():
    signal.signal(signal.SIGTERM, _signal_to_keyboardinterrupt)


def build_parser():
    # Common flags are attached to the main parser AND every subparser (with
    # argparse.SUPPRESS sub-defaults) so they may be given before or after the
    # subcommand without clobbering each other.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--ip", default=argparse.SUPPRESS, metavar="IP",
                        help=f"Device IP address (default: {DEFAULT_IP})")
    common.add_argument("--user", default=argparse.SUPPRESS, metavar="USER",
                        help=f"SSH user (default: {DEFAULT_USER})")
    common.add_argument("--password", default=argparse.SUPPRESS, metavar="PASSWORD",
                        help=f"Device password (default: {DEFAULT_PASSWORD})")
    common.add_argument("--key", default=argparse.SUPPRESS, metavar="FILE",
                        help=f"SSH private key used for ssh/scp (default: {DEFAULT_KEY})")
    common.add_argument("--pub", default=argparse.SUPPRESS, metavar="FILE",
                        help=f"SSH public key uploaded to the device (default: {DEFAULT_PUB})")
    common.add_argument("--log", default=argparse.SUPPRESS, metavar="FILE",
                        help="also tee all log lines to FILE (append)")
    common.add_argument("--generate", action="store_true", default=argparse.SUPPRESS,
                        help="generate the SSH keypair if either key file is missing")

    parser = argparse.ArgumentParser(
        prog="BerryShell-V4.py",
        description="Non-interactive BlackBerry PlayBook/BB10 qconn door keeper "
                    "(protocol layer copied verbatim from BerryShell-V3).",
        epilog="examples:\n"
               "  %(prog)s auth\n"
               "  %(prog)s exec \"uname -a\"\n"
               "  %(prog)s exec -- ls -l /var\n"
               "  %(prog)s --log /tmp/door.log hold\n"
               "common flags may appear before or after the subcommand.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        parents=[common])
    parser.set_defaults(ip=DEFAULT_IP, user=DEFAULT_USER, password=DEFAULT_PASSWORD,
                        key=DEFAULT_KEY, pub=DEFAULT_PUB, log=None, generate=False)

    sub = parser.add_subparsers(dest="mode", metavar="{auth,exec,hold}", required=True)

    sub.add_parser("auth", parents=[common],
                   help="connect + handshake + upload key, close cleanly, exit 0")

    p_exec = sub.add_parser("exec", parents=[common],
                            help="handshake + upload key, then run ONE non-interactive SSH command")
    p_exec.add_argument("remote", nargs=argparse.REMAINDER, metavar="REMOTE_CMD",
                        help="remote command, ideally as ONE quoted shell string; "
                             "a leading '--' separator is stripped")

    sub.add_parser("hold", parents=[common],
                   help="keep the door open with 4 s heartbeats forever; re-handshakes on "
                        "connection loss; runs until SIGINT/SIGTERM")
    return parser


def main():
    args = build_parser().parse_args()
    if args.log:
        setup_log_file(args.log)
    log(f"[*] BerryShell-V4 mode={args.mode} ip={args.ip} user={args.user} key={args.key} pub={args.pub}")
    if not ensure_keys(args):
        return 2
    if args.mode == "auth":
        return mode_auth(args)
    if args.mode == "exec":
        return mode_exec(args)
    if args.mode == "hold":
        return mode_hold(args)
    return 2


if __name__ == "__main__":
    install_signal_handlers()
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        log("[!] Interrupted.")
        sys.exit(130)
    except Exception as e:
        log(f"[!] Error: {e}")
        sys.exit(1)
