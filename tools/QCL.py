#!/usr/bin/env python3
# Based on https://github.com/zayfod/qcl
import socket
import re
import os
import sys
import argparse
import threading
import time

# --- Config Defaults ---
DEFAULT_HOST = "169.254.0.1"
DEFAULT_PORT = 9090
DEFAULT_MODE = "0666"
# -----------------------

class QConnSocket:
    PROMPT_REGEX = re.compile(rb'<qconn-[a-z]+>\s*$')

    def __init__(self, host, port=8000, mode='0666'):
        self.host = host
        self.port = port
        self.mode = mode
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Disable Nagle's Algorithm for speedup on small packets
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(10.0)
        self.buffer = b""

    def connect(self):
        self.sock.connect((self.host, self.port))

        # QCONN sends 'QCONN', then '<qconn-broker>'
        self.read_until(re.compile(rb'QCONN'))
        self.read_until(self.PROMPT_REGEX)

        # Consume the linemode error with a very short timeout so we don't stall the startup
        try:
            self.sock.settimeout(0.2)
            self.read_until(re.compile(rb'error linemode-or-echo-not-supported'))
            self.read_until(self.PROMPT_REGEX)
        except socket.timeout:
            pass
        finally:
            self.sock.settimeout(10.0)

    def close(self):
        try:
            self.sock.close()
        except:
            pass

    def _recv(self, telnet_mode=True):
        """
        Receives data from the socket. If telnet_mode is True, it automatically
        strips Telnet negotiation bytes and replies to the server to unblock it.
        """
        chunk = self.sock.recv(4096)
        if not chunk:
            raise EOFError("Socket connection closed by remote host.")

        if not telnet_mode:
            return chunk

        filtered = bytearray()
        i = 0
        while i < len(chunk):
            if chunk[i] == 255: # IAC (Telnet Control Byte)
                if i + 1 >= len(chunk): break
                cmd = chunk[i+1]

                # 3-byte commands: WILL (251), WONT (252), DO (253), DONT (254)
                if cmd in (251, 252, 253, 254):
                    if i + 2 >= len(chunk): break
                    opt = chunk[i+2]
                    if cmd == 253: # DO -> reply WONT (252)
                        self.sock.sendall(bytes([255, 252, opt]))
                    elif cmd == 251: # WILL -> reply DONT (254)
                        self.sock.sendall(bytes([255, 254, opt]))
                    i += 3
                elif cmd == 250: # SB (Subnegotiation)
                    end = chunk.find(bytes([255, 240]), i) # Look for SE
                    if end != -1:
                        i = end + 2
                    else: break
                elif cmd == 255: # Escaped 255
                    filtered.append(255)
                    i += 2
                else: # Other 2-byte commands
                    i += 2
            else:
                filtered.append(chunk[i])
                i += 1

        return bytes(filtered)

    def read_until(self, pattern_regex, telnet_mode=True):
        while True:
            match = pattern_regex.search(self.buffer)
            if match:
                res = self.buffer[:match.end()]
                self.buffer = self.buffer[match.end():]
                return res

            chunk = self._recv(telnet_mode)
            if chunk:
                self.buffer += chunk

    def read_exact(self, count, telnet_mode=False):
        while len(self.buffer) < count:
            chunk = self._recv(telnet_mode)
            if chunk:
                self.buffer += chunk

        res = self.buffer[:count]
        self.buffer = self.buffer[count:]
        return res

    def send_cmd(self, cmd_str, telnet_mode=True):
        self.sock.sendall(cmd_str.encode('ascii') + b'\r\n')
        data = self.read_until(self.PROMPT_REGEX, telnet_mode)
        text = data.decode('latin-1')
        lines = text.splitlines()

        result = []
        for l in lines:
            l = l.strip()
            if l and not l.startswith('<qconn-') and l != cmd_str:
                result.append(l)
        return result

    def get_service_version(self, service):
        lines = self.send_cmd(f'versions {service}')
        if lines and lines[0]:
            match = re.search(r'^(\d+)', lines[0])
            if match:
                return int(match.group(1))
        return -1

    def switch_service(self, service_name):
        lines = self.send_cmd(f'service {service_name}')
        if not lines or lines[0] != 'OK':
            raise Exception(f"Failed to switch to {service_name} service.")

    def _oob_kill(self, pid):
        """Out-Of-Band kill to guarantee a process dies and frees qconn FD slots."""
        if not pid: return
        try:
            killer = QConnSocket(self.host, self.port)
            killer.connect()
            killer.switch_service('cntl')
            # Kill process group first (to try and wipe out children like sloginfo)
            killer.send_cmd(f'kill -{pid} 9')
            # Kill the actual PID
            killer.send_cmd(f'kill {pid} 9')
            killer.close()
        except Exception:
            pass

    # --- Commands ---

    def cmd_info(self):
        lines = self.send_cmd('info')
        tokens = []
        for l in lines:
            tokens.extend(l.split())
        for t in sorted(tokens):
            if t: print(t)

    def cmd_versions(self):
        lines = self.send_cmd('versions ?')
        if not lines: return
        match = re.search(r'versions:\s+(.+)', lines[0], re.I)
        if match:
            tokens = match.group(1).split()
            for t in sorted(tokens):
                if t: print(t)

    def cmd_shell(self, raw_mode=False):
        if self.get_service_version('launcher') < 256:
            print("Unsupported launcher service version.")
            return False

        self.switch_service('launcher')
        self.send_cmd('chdir /')

        lines = self.send_cmd("start/flags 8000 /bin/sh ksh -i")
        if not lines or not lines[0].startswith('OK'):
            print(f"Failed to execute shell. Response: {lines}")
            return False

        remote_pid = lines[0].split()[1]

        self.sock.sendall(b'continue\r\n')
        line = b""
        while True:
            line = self.read_until(re.compile(rb'\n')).strip()
            if line: break

        if line != b'OK':
            print(f"Failed to continue command. Got: {line.decode('latin-1', errors='ignore')}")
            self._oob_kill(remote_pid)
            return False

        print("\n" + "="*50)
        mode_str = "Raw Mode" if raw_mode else "Buffered Mode"
        print(f" Interactive QCONN Shell ({mode_str})")
        print(" -> Type 'exit' to quit.")
        print(" -> Press Ctrl+C twice to force quit if locked up.")
        print(" -> Avoid running continous output commands like sloginfo -w or TUI apps like elvis/nano, as they will either not stop, or be broken.")
        print("="*50 + "\n")

        if self.buffer:
            sys.stdout.buffer.write(self.buffer)
            sys.stdout.buffer.flush()
            self.buffer = b""

        self.sock.settimeout(None)
        is_running = [True]

        def read_from_socket():
            while is_running[0]:
                try:
                    chunk = self._recv(telnet_mode=False)
                    if not chunk: break

                    if raw_mode:
                        # Normalize newlines so text doesn't staircase in raw mode
                        chunk = chunk.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                except Exception:
                    break
            if is_running[0]:
                is_running[0] = False
                msg = b"\r\n[!] Remote connection closed. Press any key (or Enter) to exit.\r\n"
                sys.stdout.buffer.write(msg)
                sys.stdout.buffer.flush()

        reader_thread = threading.Thread(target=read_from_socket)
        reader_thread.start()

        last_ctrl_c = 0
        try:
            if not raw_mode:
                while is_running[0]:
                    try:
                        user_input = sys.stdin.readline()
                        if not is_running[0] or not user_input:
                            break
                        if user_input.strip() == "exit":
                            self.sock.sendall(user_input.encode('latin-1'))
                            break
                        self.sock.sendall(user_input.encode('latin-1'))
                    except KeyboardInterrupt:
                        now = time.time()
                        if now - last_ctrl_c < 2.0:
                            print("\n[!] Force quitting shell...")
                            break
                        else:
                            print("\n^C (Press Ctrl+C again within 2s to force quit shell)")
                            last_ctrl_c = now
                            if is_running[0]:
                                try: self.sock.sendall(b'\x03\n')
                                except Exception: pass
            else:
                if os.name == 'nt':
                    import msvcrt
                    while is_running[0]:
                        if msvcrt.kbhit():
                            ch = msvcrt.getch()

                            if ch == b'\x03': # Ctrl+C
                                now = time.time()
                                if now - last_ctrl_c < 2.0:
                                    sys.stdout.buffer.write(b"\r\n[!] Force quitting shell...\r\n")
                                    sys.stdout.buffer.flush()
                                    is_running[0] = False
                                    break
                                else:
                                    sys.stdout.buffer.write(b"\r\n^C (Press Ctrl+C again within 2s to force quit shell)\r\n")
                                    sys.stdout.buffer.flush()
                                    last_ctrl_c = now
                                    self.sock.sendall(b'\x03')
                            elif ch == b'\r': # Enter
                                self.sock.sendall(b'\n')
                            elif ch in (b'\x00', b'\xe0'): # Windows Special Keys (Arrows, etc)
                                ch2 = msvcrt.getch()
                                if ch2 == b'H': self.sock.sendall(b'\x1b[A')   # Up
                                elif ch2 == b'P': self.sock.sendall(b'\x1b[B') # Down
                                elif ch2 == b'M': self.sock.sendall(b'\x1b[C') # Right
                                elif ch2 == b'K': self.sock.sendall(b'\x1b[D') # Left
                                elif ch2 == b'S': self.sock.sendall(b'\x1b[3~')# Delete
                            else:
                                self.sock.sendall(ch)
                        else:
                            time.sleep(0.01)
                else:
                    import tty, termios, select
                    fd = sys.stdin.fileno()
                    old_settings = termios.tcgetattr(fd)
                    try:
                        tty.setraw(fd)
                        while is_running[0]:
                            r, _, _ = select.select([sys.stdin], [], [], 0.05)
                            if r:
                                ch = sys.stdin.read(1).encode('latin-1')
                                if ch == b'\x03':
                                    now = time.time()
                                    if now - last_ctrl_c < 2.0:
                                        sys.stdout.buffer.write(b"\r\n[!] Force quitting shell...\r\n")
                                        sys.stdout.buffer.flush()
                                        is_running[0] = False
                                        break
                                    else:
                                        sys.stdout.buffer.write(b"\r\n^C (Press Ctrl+C again within 2s to force quit shell)\r\n")
                                        sys.stdout.buffer.flush()
                                        last_ctrl_c = now
                                        self.sock.sendall(b'\x03')
                                elif ch == b'\r':
                                    self.sock.sendall(b'\n')
                                else:
                                    self.sock.sendall(ch)
                    finally:
                        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)

        except KeyboardInterrupt:
            pass
        finally:
            is_running[0] = False
            self.sock.close()
            reader_thread.join()
            self._oob_kill(remote_pid)
            return True


    def cmd_exec(self, cmd_path, args):
        if self.get_service_version('launcher') < 256:
            print("Unsupported launcher service version.")
            return False

        self.switch_service('launcher')
        self.send_cmd('chdir /')

        basename = os.path.basename(cmd_path)
        arg_str = ' '.join(args)
        if arg_str: arg_str = ' ' + arg_str

        start_cmd = f"start/flags 8000 {cmd_path} {basename}{arg_str}"
        lines = self.send_cmd(start_cmd)

        if not lines or not lines[0].startswith('OK'):
            print(f"Failed to execute command. Response: {lines}")
            return False

        remote_pid = lines[0].split()[1]

        self.sock.sendall(b'continue\r\n')
        line = b""
        while True:
            line = self.read_until(re.compile(rb'\n')).strip()
            if line: break

        if line != b'OK':
            print(f"Failed to continue command. Got: {line.decode('latin-1', errors='ignore')}")
            self._oob_kill(remote_pid)
            return False

        self.sock.settimeout(None)
        try:
            while True:
                match = self.PROMPT_REGEX.search(self.buffer)
                if match:
                    sys.stdout.buffer.write(self.buffer[:match.start()])
                    sys.stdout.buffer.flush()
                    self.buffer = self.buffer[match.end():]
                    break

                if len(self.buffer) > 50:
                    sys.stdout.buffer.write(self.buffer[:-50])
                    sys.stdout.buffer.flush()
                    self.buffer = self.buffer[-50:]

                chunk = self._recv(telnet_mode=False)
                if not chunk: break
                self.buffer += chunk

        except KeyboardInterrupt:
            print("\n[!] Caught Ctrl+C, killing remote process...")
        except (socket.timeout, EOFError):
            pass
        finally:
            self.sock.settimeout(10.0)
            self._oob_kill(remote_pid)
            return True

    def cmd_put(self, source, target):
        self.switch_service('file')

        target_name = target
        if target.endswith('/') or target.endswith('\\'):
            target_name = os.path.join(target, os.path.basename(source)).replace('\\', '/')

        mode = int(self.mode, 8)
        lines = self.send_cmd(f'o:"{target_name}":{0x0301:x}:{mode:x}')
        if not lines or not lines[0].startswith('o:'):
            print(f"Failed to create target file {target_name}.")
            return False

        match = re.search(r'^o:([0-9a-f-]+)', lines[0], re.I)
        fd = int(match.group(1), 16)

        try:
            with open(source, 'rb') as f:
                offs = 0
                while True:
                    chunk = f.read(16 * 1024)
                    if not chunk: break

                    packet = f'w:{fd:x}:{offs:x}:{len(chunk):x}\r\n'.encode('ascii') + chunk
                    self.sock.sendall(packet)

                    self.read_until(re.compile(rb'\r?\n'), telnet_mode=False)
                    self.read_until(self.PROMPT_REGEX, telnet_mode=False)
                    offs += len(chunk)
        except IOError as e:
            print(f"File error: {e}")
            return False
        finally:
            self.send_cmd(f'c:{fd:x}')
            self.sock.sendall(b'q\r\n')

        return True

    def cmd_get(self, source, target):
        self.switch_service('file')

        lines = self.send_cmd(f'o:"{source}":0')
        if not lines or not lines[0].startswith('o:'):
            print(f"Failed to open source file {source}.")
            return False

        match = re.search(r'^o:([0-9a-f-]+):([0-9a-f-]+):([0-9a-f-]+)', lines[0], re.I)
        fd = int(match.group(1), 16)
        total_size = int(match.group(3), 16)

        if os.path.isdir(target):
            target = os.path.join(target, os.path.basename(source))

        try:
            offs = 0
            with open(target, 'wb') as f:
                while offs < total_size:
                    req_size = min(2048, total_size - offs)
                    self.sock.sendall(f'r:{fd:x}:{offs:x}:{req_size:x}\r\n'.encode('ascii'))

                    line_data = self.read_until(re.compile(rb'\r?\n'))
                    line = line_data.decode('latin-1').strip()
                    r_match = re.search(r'^o:([0-9a-f-]+)', line, re.I)
                    if not r_match:
                        print(f"Failed reading from source. Line: {line}")
                        break

                    read_size = int(r_match.group(1), 16)
                    if read_size == 0: break

                    data = self.read_exact(read_size, telnet_mode=False)
                    f.write(data)
                    offs += read_size

                    self.read_until(self.PROMPT_REGEX, telnet_mode=False)
        except IOError as e:
            print(f"File error: {e}")
            return False
        finally:
            self.send_cmd(f'c:{fd:x}')
            self.sock.sendall(b'q\r\n')

        return True

    def cmd_rm(self, target):
        self.switch_service('file')
        lines = self.send_cmd(f'd:"{target}":1')
        if not lines or not lines[0].startswith('o'):
            print("No such file or directory or operation failed.")
        self.sock.sendall(b'q\r\n')

    def cmd_mkdir(self, target):
        self.switch_service('file')
        mode = int(self.mode, 8) | 0x4000
        lines = self.send_cmd(f'o:"{target}":{0x0101:x}:{mode:x}')
        if not lines or not lines[0].startswith('o:'):
            print(f"Failed to create target directory {target}.")
        self.sock.sendall(b'q\r\n')

    def cmd_chmod(self, target):
        self.switch_service('file')

        lines = self.send_cmd(f'o:"{target}":0')
        if not lines or not lines[0].startswith('o:'):
            print(f"Failed to open target {target}.")
            return False
        match = re.search(r'^o:([0-9a-f-]+):([0-9a-f-]+):', lines[0], re.I)
        fd = int(match.group(1), 16)
        is_dir = (int(match.group(2), 16) & 0x4000) != 0
        self.send_cmd(f'c:{fd:x}')

        open_mode = 0x4000 if is_dir else 2
        lines = self.send_cmd(f'o:"{target}":{open_mode:x}')
        match = re.search(r'^o:([0-9a-f-]+):', lines[0], re.I)
        fd = int(match.group(1), 16)

        mode = int(self.mode, 8)
        self.send_cmd(f'a:{fd:x}:{mode:x}')
        self.send_cmd(f'c:{fd:x}')
        self.sock.sendall(b'q\r\n')

    def cmd_kill(self, pid, signal):
        if self.get_service_version('cntl') < 256:
            print("Unsupported cntl service version.")
            return False

        self.switch_service('cntl')
        lines = self.send_cmd(f'kill {pid} {signal}')
        if not lines or not lines[0].lower().startswith('ok'):
            print("No such process or operation failed.")
        self.sock.sendall(b'quit\r\n')


def main():
    parser = argparse.ArgumentParser(
        description="QNX QCONN service provider client (Python Port)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
Examples:
  qcl.py shell
  qcl.py shell --raw
  qcl.py put test.zip /tmp/
  qcl.py get /tmp/test.zip .
  qcl.py exec ls -la /usr/
  qcl.py info
        """
    )

    parser.add_argument("--host", default=DEFAULT_HOST, help=f"QCONN server hostname/IP (default: {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"QCONN server TCP port (default: {DEFAULT_PORT})")
    parser.add_argument("--mode", default=DEFAULT_MODE, help=f"Permissions used when creating files/folders (default: {DEFAULT_MODE})")

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("info", help="Retrieves target system information")
    subparsers.add_parser("versions", help="Retrieves supported services and versions")

    p_shell = subparsers.add_parser("shell", help="Starts an interactive /bin/sh session over QCONN (Use --raw for raw terminal mode, which allows for visual autocomplete, but is slower)")
    p_shell.add_argument("--raw", action="store_true", help="Use raw terminal mode for tab autocomplete (may lag)")

    p_exec = subparsers.add_parser("exec", help="Executes a command on the target")
    p_exec.add_argument("cmd_path", help="Path to the executable on the remote target")
    p_exec.add_argument("args", nargs=argparse.REMAINDER, help="Arguments to pass to the executable")

    p_put = subparsers.add_parser("put", help="Transfers a file to the target")
    p_put.add_argument("source", help="Local file to transfer")
    p_put.add_argument("target", help="Remote destination path")

    p_get = subparsers.add_parser("get", help="Transfers a file from the target")
    p_get.add_argument("source", help="Remote file to retrieve")
    p_get.add_argument("target", help="Local destination path")

    p_rm = subparsers.add_parser("rm", help="Deletes a remote file/directory")
    p_rm.add_argument("target", help="Remote target path to remove")

    p_mkdir = subparsers.add_parser("mkdir", help="Creates a remote directory")
    p_mkdir.add_argument("target", help="Remote directory path")

    p_chmod = subparsers.add_parser("chmod", help="Changes permissions of a remote file/directory")
    p_chmod.add_argument("target", help="Remote file/directory path")

    p_kill = subparsers.add_parser("kill", help="Sends a signal to a process")
    p_kill.add_argument("pid", type=int, help="Process ID")
    p_kill.add_argument("signal", type=int, help="Signal number")

    args = parser.parse_args()

    client = QConnSocket(args.host, args.port, args.mode)

    try:
        client.connect()
    except Exception as e:
        print(f"Failed to connect to QCONN server at {args.host}:{args.port}. {e}")
        sys.exit(2)

    try:
        if args.command == "info":
            client.cmd_info()
        elif args.command == "versions":
            client.cmd_versions()
        elif args.command == "shell":
            client.cmd_shell(raw_mode=args.raw)
        elif args.command == "exec":
            client.cmd_exec(args.cmd_path, args.args)
        elif args.command == "put":
            client.cmd_put(args.source, args.target)
        elif args.command == "get":
            client.cmd_get(args.source, args.target)
        elif args.command == "rm":
            client.cmd_rm(args.target)
        elif args.command == "mkdir":
            client.cmd_mkdir(args.target)
        elif args.command == "chmod":
            client.cmd_chmod(args.target)
        elif args.command == "kill":
            client.cmd_kill(args.pid, args.signal)
    except Exception as e:
        print(f"Error executing command: {e}")
        sys.exit(3)
    finally:
        client.close()

if __name__ == "__main__":
    main()
