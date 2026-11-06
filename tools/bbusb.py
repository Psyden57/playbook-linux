#!/usr/bin/env python3
# derived from bb10mt by oleksandr
# https://github.com/bb10root/bb10mt

"""PlayBook USB protocol module — ours, in `tools/`.

Provenance: this is a Python port of bb10mt's `bbusb.pas` and `ucrypto.pas`
(`HashPassV2` is ported directly; the framing, packet numbering and bootrom
handshake follow the same source).  bb10mt is **GPL-3.0**, so this file is a
derivative work — see the licence addendum in `docs/project/DECISIONS.md`.  It used
to live under `local/vendor/bbusb/`, which was wrong: the vendor directory is for
third-party binaries, and this is our code.

The ramloader blobs it uploads are a different matter: `loader_06001A06-00.bin`
(LDR_01) is RIM material, so it stays out of the tree under
`local/loaders/`.  Override the location with `$BBUSB_LOADERS` or `--loaders`.

udev, so the device is usable without root:

    /etc/udev/rules.d/99-usb.rules
    SUBSYSTEM=="usb", ATTR{idVendor}=="0fca", ATTR{idProduct}=="0001", MODE="0666", GROUP="plugdev"
    SUBSYSTEM=="usb", ATTR{idVendor}=="0fca", ATTR{idProduct}=="8017", MODE="0666", GROUP="plugdev"
    SUBSYSTEM=="usb", ATTR{idVendor}=="0fca", ATTR{idProduct}=="8001", MODE="0666", GROUP="plugdev"

    sudo udevadm control --reload-rules && sudo udevadm trigger
    sudo usermod -a -G plugdev $USER
"""

import struct, zlib, time, re, os, sys, stat, hashlib
from typing import List, Tuple, Optional, Callable

try:
    import usb.core, usb.util
except ImportError:          # container commands (pack/unpack/split/parse) still work
    usb = None

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(TOOLS_DIR)

# Where the ramloader blobs live.  First existing candidate wins; `tools/loaders`
# is checked first so a checkout can ship them beside the module if it ever can.
LOADER_DIRS = [
    os.path.join(TOOLS_DIR, 'loaders'),
    os.path.join(REPO_ROOT, 'local', 'loaders'),
    os.path.join(REPO_ROOT, 'local', 'vendor', 'bbusb', 'loaders'),   # legacy path
]


def find_loaders_dir():
    """Resolve the ramloader directory (env override, then the known locations)."""
    env = os.environ.get('BBUSB_LOADERS')
    if env:
        return env
    for cand in LOADER_DIRS:
        if os.path.isdir(cand):
            return cand
    return LOADER_DIRS[0]


DEFAULT_LOADERS = find_loaders_dir()

# Constants
MAX_PACKET = 0x10000
MAX_FLASH_BLOCK = 0x3FF4
BOOT_MODES = [
    b'RIM REINIT',
    b'RIM-BootLoader', 
    b'RIM-RAMLoader',
    b'RIM UPL',
    b'RIM-BootNUKE'
]

# Bootrom commands
CMD_PING = 0xF000
CMD_READ_METRICS = 0xF001
CMD_EXIT = 0xF002
CMD_FRESHNESS_SEAL = 0xF003
CMD_WRITE_RAM = 0xF004
CMD_EXECUTE_RAM = 0xF005
CMD_PASSWORD = 0xF006
CMD_CHANGE_BPS = 0xF007
CMD_DEVICE_NUKE = 0xF008
CMD_WRITE_RAM_SETUP = 0xF009
CMD_WRITE_RAM_VERIFY = 0xF00A
CMD_READ_MODEL_CODE = 0xF00B
CMD_X1 = 0xF00C
CMD_X2 = 0xF00D

# Ramloader commands
CMD_BUGDISP_LOG = 0xB0
CMD_BLOCK_SIZE = 0xB1          # -> 0xC8 (report 100)
CMD_MCT = 0xD9                 # -> 0xC9, 240 B (report 100)
CMD_ACTION_LOG = 0xB3          # -> 0xB4, {u32 len, text} (report 165)
RESP_ACTION_LOG = 0xB4
CMD_FLASH_REGIONS_INFO = 0xB4
CMD_DEVICE_PIN = 0xE7
CMD_SEND_BLOCK = 0xF7
CMD_COMPLETE = 0x40C0
CMD_GRS_WIPE = 0xC8
CMD_REBOOT = 0x80EF
CMD_BLOCKED_OS = 0xEC
CMD_SIGNATURE_TRAILER = 0x40F9

# Loader command 0xDE (BOOT_MODE). Payload is {u32 action, u32 value}.
# Response payload is {u32 status, u32 mode, u32 count}.
CMD_BOOT_MODE = 0xDE
BOOT_MODE_READ = 0
BOOT_MODE_SWITCH = 1
BOOT_MODE_SET_COUNT = 2
BOOT_MODE_PRODUCT = 0
BOOT_MODE_FACTORY = 1

BOOT_MODE_STATUS = {
    0: 'ok',
    1: 'not allowed',
    2: 'boot count out of range',
    3: 'cannot set in this device mode',
}

BOOT_MODE_NAMES = {
    0: 'product',
    1: 'factory',
    2: 'expired',
}


class PasswordError(Exception):
    """The password stage failed.  NEVER retried automatically: the device tracks
    attempts persistently and ten wrong ones wipe UFS/RCFS/IFS and the password
    NVRAM record (SAFETY.md §7)."""


class DeviceError(Exception):
    """Any other device-stage failure (timeouts, unexpected responses)."""


def hash_pass_v2(challenge: bytes, salt: bytes, password: str, iterations: int) -> bytes:
    """Port of uCrypto.HashPassV2: Iterations SHA-512 rounds, then a second
    pass with the 4-byte challenge prefixed to the digest."""
    hd = password.encode('utf-8')
    count = 0
    challenger = True
    while True:
        buf = struct.pack('<I', count & 0xFFFFFFFF) + salt + hd
        hd = hashlib.sha512(buf).digest()
        if count == iterations - 1 and challenger:
            count = -1
            challenger = False
            if challenge:
                hd = challenge + hd
        count += 1
        if count >= iterations:
            return hd


class BBUSB:
    def __init__(self):
        self.device = None
        self.id = 0
        self.packet_num = [0, 0, 0]
        self.mode = 0xFF
    
    def __del__(self):
        self.close()
    
    def open(self, product_ids: List[int], timeout_ms: int = 5000) -> bool:
        """Try to open USB device with specified product IDs within timeout period."""
        if usb is None:
            raise RuntimeError('pyusb is not installed; only the container '
                               'commands (pack/unpack/split/parse) work without it')
        t = 0

        if self.device is not None:
            try:
                usb.util.dispose_resources(self.device)
            except:
                pass
            self.device = None

        while t < timeout_ms:
            try:
                devices = usb.core.find(find_all=True)

                for device in devices:
                    if device.idVendor != 0x0FCA or device.idProduct not in product_ids: continue
                    try:
                        self.id = device.idProduct
                        self.device = device

                        try:
                            cfg = device.get_active_configuration()
                            current_config = cfg.bConfigurationValue
                        except:
                            current_config = 0
                        
                        try:
                            if device.is_kernel_driver_active(0):
                                device.detach_kernel_driver(0)
                        except:
                            pass
                        
                        try:
                            if device.is_kernel_driver_active(1):
                                device.detach_kernel_driver(1)
                        except:
                            pass
                        
                        if current_config != 1:
                            try:
                                device.set_configuration(1)
                            except:
                                continue
                        
                        try:
                            usb.util.claim_interface(device, 0)
                        except:
                            continue
                        
                        try:
                            usb.util.claim_interface(device, 1)
                        except:
                            pass
                        
                        return True
                        
                    except Exception as e:
                        if self.device is not None:
                            try:
                                usb.util.dispose_resources(
                                    self.device)
                            except:
                                pass
                            self.device = None
                        continue
                                    
            except Exception as e:
                pass

            time.sleep(0.1)
            t += 100

        return False
    
    def close(self):
        """Close the USB device"""
        if self.device:
            try:
                usb.util.release_interface(self.device, 0)
                usb.util.release_interface(self.device, 1)
                usb.util.dispose_resources(self.device)
            except:
                pass
            self.device = None
        self.id = 0
    
    def read_data(self, timeout=1000) -> Tuple[int, bytes]:
        """Read data from the device"""
        if not self.device:
            raise Exception("Device not opened")
        
        if self.id == 1 or self.id == 0x8001:
            endpoint = 0x82
        else:
            endpoint = 0x81
        
        try:
            data = self.device.read(endpoint, MAX_PACKET, timeout=timeout)
            
            if len(data) >= 4:
                channel = struct.unpack('<H', data[0:2])[0]
                payload = bytes(data[4:])
                return channel, payload
            else:
                return 0, b''
                
        except usb.core.USBTimeoutError:
            raise Exception("Timeout reading from device")
        except Exception as e:
            raise Exception(f"Can't read from device: {e}")
    
    def send_data(self, channel: int, data: bytes, timeout=1000) -> int:
        """Send data to the device"""
        if not self.device:
            raise Exception("Device not opened")
        
        data_size = len(data)
        size = data_size + 4
        
        if size == 0:
            raise Exception("Zero-length packet")
        
        pkt = bytearray(size)
        struct.pack_into('<H', pkt, 0, channel)
        struct.pack_into('<H', pkt, 2, size)
        
        if data_size > 0:
            pkt[4:] = data
        
        if self.id == 1 or self.id == 0x8001:
            endpoint = 0x02
        else:
            endpoint = 0x01
        
        try:
            transferred = self.device.write(endpoint, pkt, timeout=timeout)
            return transferred
        except Exception as e:
            raise Exception(f"USB transfer failed: {e}")
    
    def channel0(self, cmd: int, data: bytes, timeout=1000) -> Tuple[int, bytes]:
        """Channel 0 communication"""
        size = len(data)
        pkt = bytearray(size + 4)
        pkt[0] = cmd
        pkt[1] = self.mode
        struct.pack_into('>H', pkt, 2, self.packet_num[0])
        
        if size > 0:
            pkt[4:] = data
        
        self.send_data(0, pkt, timeout=timeout)
        channel, response = self.read_data(timeout=timeout)
        
        if len(response) >= 4:
            cmd = response[0]
            self.mode = response[1]
            result = response[4:] if len(response) > 4 else b''
        else:
            result = b''
        
        self.packet_num[0] += 1
        return cmd, result
    
    def channel1(self, data: bytes, timeout=1000) -> bytes:
        """Channel 1 communication"""
        data_size = len(data)
        size = data_size + 10
        pkt = bytearray(size)
        
        pkt[10:] = data
        
        struct.pack_into('<I', pkt, 4, size)
        
        struct.pack_into('<H', pkt, 8, self.packet_num[1])
        self.packet_num[1] += 1
        
        crc = zlib.crc32(pkt[4:]) & 0xFFFFFFFF
        struct.pack_into('<I', pkt, 0, crc)
        
        self.send_data(1, pkt, timeout=timeout)
        _, pkt = self.read_data(timeout=timeout)
        
        result = pkt[10:] if len(pkt) > 10 else b''
        
        self.read_data(timeout=timeout)
        
        return result
    
    def channel2(self, cmd: int, data: bytes, timeout=1000) -> Tuple[int, bytes]:
        """Channel 2 communication"""
        data_size = len(data)
        size = data_size + 8
        pkt = bytearray(size)
        
        struct.pack_into('<H', pkt, 0, size)
        struct.pack_into('<H', pkt, 2, cmd)
        
        if data_size > 0:
            pkt[4:4+data_size] = data
        
        crc = zlib.crc32(pkt[0:data_size+4]) & 0xFFFFFFFF
        struct.pack_into('<I', pkt, data_size + 4, crc)
        
        self.send_data(2, pkt, timeout=timeout)
        channel, response = self.read_data(timeout=timeout)
        
        if channel == 2 and len(response) >= 8:
            resp_cmd = struct.unpack('<H', response[2:4])[0]
            payload_size = len(response) - 8
            
            if payload_size > 0:
                crc1 = zlib.crc32(response[0:payload_size+4]) & 0xFFFFFFFF
                crc2 = struct.unpack('<I', response[payload_size+4:payload_size+8])[0]
                
                if crc1 == crc2:
                    result = response[4:4+payload_size]
                else:
                    result = b''
            else:
                result = b''
            
            self.read_data(timeout=timeout)
            return resp_cmd, result
        
        return 0, b''
    
    def ping0(self):
        """Send ping on channel 0"""
        cmd = 1
        self.channel0(cmd, bytes([0x14, 0x05, 0x83, 0x19, 0x00, 0x00, 0x00, 0x00]))
        
    def reboot(self):
        """Reboot the device"""
        cmd = 3
        self.channel0(cmd, b'')
        self.packet_num[0] = 0
    
    def set_mode_raw(self, mode: int) -> Tuple[int, bytes]:
        """Send the mode-switch command and return the raw (cmd, payload)."""
        if mode >= len(BOOT_MODES):
            return 0, b''
        data = bytearray(17)
        boot_mode = BOOT_MODES[mode]
        data[0:len(boot_mode)] = boot_mode
        data[16] = 1
        return self.channel0(7, data)

    def set_mode(self, mode: int) -> bool:
        """Set device mode"""
        resp_cmd, _ = self.set_mode_raw(mode)
        return resp_cmd == 8
    
    def password_info(self, password: str = '', debug: bool = False,
                      max_attempts: int = 3) -> bool:
        """Complete the bootrom/loader password stage.

        Raises `PasswordError` on rejection or an unexpected response — it never
        returns False, because every caller so far then did something worse (a retry
        or a reboot) and this stage is the one place where a retry costs attempts on
        a device that wipes after ten.

        Behaviour:
          * a supplied password is sent **at most once** (`--password`);
          * interactively, the device's own challenge is re-presented with the
            remaining budget, up to `max_attempts` times;
          * any response that is neither "accepted" (`0x10`) nor "here is a
            challenge" (`0x0E`) aborts with the code in the message.

        The two policy rules are bb10mt's, not ours: stop once the device reports two
        attempts spent (`bbusb.pas:493`), and never send an empty line as a guess
        (`bbusb.pas:497-500`).  Where we differ it is stricter: bb10mt loops forever on
        an unexpected response, we abort with the code.
        """
        cmd = 0x0A
        sent = b''
        used = 0
        warned = False
        supplied = bool(password)   # CLI-supplied: send once, never retry
        for _ in range(8):
            resp_cmd, result = self.channel0(cmd, sent, timeout=3000)
            if debug:
                print(f"  password_info: cmd 0x{resp_cmd:02X} len={len(result)} "
                      f"{result.hex()}")
            if resp_cmd == 0x10:
                try:
                    self.read_data(timeout=1000)
                except Exception:
                    pass
                return True
            if resp_cmd != 0x0E or len(result) < 24:
                raise PasswordError(
                    'password stage: unexpected response 0x%02X (len %d) — the device '
                    'is not offering a challenge.  Do not retry blindly; power-cycle '
                    'the unit.' % (resp_cmd, len(result)))
            remaining, budget = result[0], result[1]
            if remaining == 0:
                raise PasswordError('password stage: the device reports 0 attempts '
                                    'left (%d/%d)' % (remaining, budget))
            # Upstream policy (bbusb.pas:493): give up once the device reports that
            # two attempts have been spent.  Keep it — a tool that keeps going walks
            # the unit toward the wipe.
            if budget - remaining >= 2:
                raise PasswordError(
                    'password stage: the device reports %d/%d (%d attempt(s) spent); '
                    'stopping at 2, as bb10mt does' % (remaining, budget,
                                                       budget - remaining))
            challenge = result[4:8]
            salt = result[12:20]
            iterations = struct.unpack_from('<I', result, 20)[0]

            if supplied:
                if used:
                    raise PasswordError(
                        'password rejected (%d/%d left); a supplied --password is '
                        'sent only once and never retried' % (remaining, budget))
            else:
                if used >= max_attempts:
                    raise PasswordError(
                        'password rejected %d time(s) (%d/%d left); stopping rather '
                        'than spending more attempts' % (used, remaining, budget))
                if not warned:
                    print('WARNING: 10 wrong password attempts WIPE this unit '
                          '(UFS/RCFS/IFS + NVRAM).  Typed input is not logged.',
                          file=sys.stderr)
                    warned = True
                # An empty line must NOT be sent as a guess (bbusb.pas:497-500
                # re-reads until something is typed); a stray Enter would otherwise
                # spend an attempt.
                password = ''
                while not password:
                    try:
                        password = input('Enter password (%d/%d): '
                                         % (remaining, budget)).strip()
                    except EOFError:
                        raise PasswordError('password required and stdin is not '
                                            'interactive; pass --password')
            hd = hash_pass_v2(challenge, salt, password, iterations)
            sent = b'\x00\x00\x40\x00' + hd
            cmd = 0x0F
            used += 1
        raise PasswordError('password stage did not converge after %d exchanges'
                            % used)

    def get_metrics(self) -> bytes:
        """Get device metrics"""
        data = struct.pack('<H', CMD_READ_METRICS)
        return self.channel1(data)

    def switch_channel(self):
        """Send the channel switch handshake used before a loader upload."""
        self.send_data(1, bytes([6, 6]))
        for _ in range(2):
            try:
                self.read_data(timeout=1000)
            except Exception:
                pass
        self.packet_num[1] = 0
    
    def get_model_id(self) -> int:
        """Get device model ID"""
        data = struct.pack('<H', CMD_READ_MODEL_CODE)
        response = self.channel1(data)
        
        if len(response) < 14:
            raise Exception(f'Unexpected response size: {len(response)} bytes')
        
        return struct.unpack('<I', response[10:14])[0]

    def get_var(self, var_id: int, buf_size: int) -> bytes:
        """Read a bootrom variable (channel 0, cmd 5); cmd 6 is the answer."""
        cmd = 5
        data = struct.pack('<HH', buf_size, var_id)
        resp, result = self.channel0(cmd, data)
        return result if resp == 6 else b''
    
    def send_loader(self, addr: int, loader: bytes, callback: Optional[Callable[[int, int], None]] = None,
                    chunk_size: int = 2024):
        """Send loader to device. OMAP44xx (0x80100000) needs 260-byte chunks."""
        CHUNK_SIZE = chunk_size
        
        data = bytearray(10)
        struct.pack_into('<H', data, 0, CMD_WRITE_RAM_SETUP)
        struct.pack_into('<I', data, 2, addr)
        struct.pack_into('<I', data, 6, len(loader))
        self.channel1(data)
        
        bytes_sent = 0
        total_size = len(loader)
        
        while bytes_sent < total_size:
            chunk_size = min(CHUNK_SIZE, total_size - bytes_sent)
            pkt = bytearray(chunk_size + 10)
            
            struct.pack_into('<H', pkt, 0, CMD_WRITE_RAM)
            struct.pack_into('<I', pkt, 2, addr + bytes_sent)
            struct.pack_into('<I', pkt, 6, chunk_size)
            pkt[10:] = loader[bytes_sent:bytes_sent + chunk_size]
            
            self.channel1(pkt)
            bytes_sent += chunk_size
            
            if callback:
                callback(bytes_sent, total_size)
        
        if callback:
            callback(total_size, total_size)
        
        verify_data = struct.pack('<H', CMD_WRITE_RAM_VERIFY)
        self.channel1(verify_data)
    
    def run_loader(self, addr: int):
        """Execute loader at specified address"""
        data = bytearray(6)
        struct.pack_into('<H', data, 0, CMD_EXECUTE_RAM)
        struct.pack_into('<I', data, 2, addr)
        self.channel1(data)
        self.read_data()
    
    def bugdisp_log(self) -> bytes:
        """Retrieve the Bugdisp Log (0xB0).

        The log arrives as 0xB5 chunks terminated by 0xD0.  A loader that has
        already served its log once answers `0x15` (or anything else) instead of
        `0xD0`; that is a normal end, not an error, so it ends the drain quietly
        and leaves `self.bugdisp_truncated` set.  The result is cached in
        `self.last_bugdisp_log` so callers do not have to ask a second time.
        """
        result_data = b''
        self.bugdisp_truncated = False
        while True:
            # CFP sends 0xB0 with a zero-length payload (0x44E890: push 0xd0
            # expected-response, push 0xb0, and a length of 0).  We used to send
            # eight zero bytes; on the live unit that returns the loader's
            # "This log is encrypted." placeholder.
            cmd, data = self.channel2(CMD_BUGDISP_LOG, b'')
            if cmd == 0xB5:
                result_data += data
            elif cmd == 0xD0:
                break
            else:
                self.bugdisp_truncated = True
                self.bugdisp_last_cmd = cmd
                break
        self.last_bugdisp_log = result_data
        return result_data

    def flash_regions_info(self) -> bytes:
        """Retrieves Flash Regions Info."""
        _, data = self.channel2(CMD_FLASH_REGIONS_INFO, b'')
        return data

    def blocked_os(self) -> bytes:
        """Retrieves Blocked OS CFP (Controlled File Protocol) information."""
        cmd, data = self.channel2(CMD_BLOCKED_OS, b'')
        if cmd == 0xFD:
            return data
        else:
            return b''

    def device_pin(self) -> int:
        """Retrieves the Device PIN."""
        cmd, buff = self.channel2(CMD_DEVICE_PIN, b'')
        if cmd == 0xD1 and len(buff) >= 4:
            return struct.unpack('<I', buff[0:4])[0]
        else:
            return 0

    def send_block(self, data: bytes) -> bool:
        """Sends a data block to the device."""
        cmd, _ = self.channel2(CMD_SEND_BLOCK, data)
        return cmd == 0xDF

    def complete(self) -> bool:
        """Sends the 'Complete' command."""
        cmd, _ = self.channel2(CMD_COMPLETE, b'')
        return cmd == 0x4006

    def grs_wipe(self) -> bool:
        """Initiates a GRS (General Reset System) Wipe."""
        dummy_buff = bytes([0] * MAX_FLASH_BLOCK)
        cmd, _ = self.channel2(CMD_GRS_WIPE, dummy_buff)
        return cmd == 0xD8

    def send_signature(self, data: bytes) -> bool:
        """Sends a signature block."""
        cmd, _ = self.channel2(CMD_SIGNATURE_TRAILER, data)
        return cmd == 0x4006

    def boot_mode(self, action: int, value: int = 0, timeout=2000) -> Tuple[int, bytes]:
        """Sends loader command 0xDE (BOOT_MODE).

        Returns (response_cmd, payload).  A loader that does not implement the
        requested action does not answer at all, so a timeout is reported as
        (0, b'').
        """
        payload = struct.pack('<II', action & 0xFFFFFFFF, value & 0xFFFFFFFF)
        try:
            return self.channel2(CMD_BOOT_MODE, payload, timeout=timeout)
        except Exception:
            return 0, b''

    @staticmethod
    def parse_boot_mode(data: bytes) -> Optional[Tuple[int, int, int]]:
        """Parses the 12-byte BOOT_MODE reply as (status, mode, count)."""
        if len(data) < 12:
            return None
        return struct.unpack('<III', data[:12])

    def read_boot_mode(self, timeout=2000) -> Optional[Tuple[int, int, int]]:
        """Reads the boot mode and factory counter (action 0)."""
        cmd, data = self.boot_mode(BOOT_MODE_READ, 0, timeout)
        if cmd != 0xFA:
            return None
        return self.parse_boot_mode(data)

    def set_boot_mode(self, factory: bool, timeout=2000) -> Optional[Tuple[int, int, int]]:
        """Switches between FACTORY and PRODUCT boot mode (action 1)."""
        value = BOOT_MODE_FACTORY if factory else BOOT_MODE_PRODUCT
        cmd, data = self.boot_mode(BOOT_MODE_SWITCH, value, timeout)
        if cmd != 0xFA:
            return None
        return self.parse_boot_mode(data)

    def set_boot_count(self, count: int, timeout=2000) -> Optional[Tuple[int, int, int]]:
        """Sets the factory boot counter (action 2). Requires 1..50."""
        if not 1 <= count <= 50:
            raise ValueError('boot count must be 1..50')
        cmd, data = self.boot_mode(BOOT_MODE_SET_COUNT, count, timeout)
        if cmd != 0xFA:
            return None
        return self.parse_boot_mode(data)

    def reboot_loader(self) -> bool:
        """Reboots the device in ramloader"""
        cmd, _ = self.channel2(CMD_REBOOT, b'', timeout=50)
        return cmd == 0x80C7

def find_loader(targetId, directory='loaders'):
    pattern = re.compile(r'^loader_([0-9A-Fa-f]{8})-.*\.bin$')
    
    for filename in os.listdir(directory):
        match = pattern.match(filename)
        if not match or match.group(1).upper() != targetId.upper(): continue
        
        filePath = os.path.join(directory, filename)
        with open(filePath, 'rb') as f:
            return f.read()
    
    return None


def restart_program():
    script_path = os.path.abspath(sys.argv[0])
    st = os.stat(script_path)
    os.chmod(script_path, st.st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    os.execv(script_path, sys.argv)

def hexlog(bytearr: bytearray):
    print(str(bytearr.hex()))


def _bootrom_handshake(bb: BBUSB, password: str = '', debug: bool = False) -> int:
    """Ping, read BROM metrics, switch to bootloader mode, pass password stage."""
    bb.ping0()
    rom_info = bb.get_var(2, 2000)
    if len(rom_info) < 20:
        raise Exception(f"invalid bootrom metrics ({len(rom_info)} bytes)")
    bb.last_bootrom_metrics = rom_info   # only readable in bootrom mode
    model_id = struct.unpack_from('<I', rom_info, 16)[0]
    if debug:
        br_ver = struct.unpack_from('<I', rom_info, 12)[0]
        print(f"  metrics {len(rom_info)} bytes, BR_ver 0x{br_ver:08X}, model 0x{model_id:08X}")
    resp_cmd, payload = bb.set_mode_raw(1)
    if debug:
        print(f"  set_mode(1): cmd 0x{resp_cmd:02X} len={len(payload)} {payload.hex()}")
    if resp_cmd not in (8, 9):
        raise Exception(f"set_mode(1) failed (cmd 0x{resp_cmd:02X})")
    time.sleep(0.3)
    bb.password_info(password, debug)          # raises PasswordError on failure
    return model_id


def _open_ramloader(bb: BBUSB, loaders_dir: str, from_bootrom: bool,
                    addr_override: Optional[int], password: str = '',
                    debug: bool = False) -> bool:
    """Open the device in ramloader mode, optionally uploading the loader first."""
    if not bb.open([0x0001, 0x8001], timeout_ms=60000):
        print("No BlackBerry device found")
        return False

    if bb.id == 0x0001:
        if not from_bootrom:
            print("Device is in bootrom; pass --from-bootrom to upload a ramloader")
            return False
        print("Connected in bootrom")

        model_id = None
        for attempt in (1, 2):
            try:
                model_id = _bootrom_handshake(bb, password, debug)
                break
            except PasswordError as e:
                # Do NOT retry and do NOT reboot.  A reboot here spends another
                # attempt on a device that has just rejected one, and ten wrong
                # attempts wipe the unit (SAFETY.md §7).  Leave the device alone.
                print('password stage failed: %s' % e, file=sys.stderr)
                print('Not retrying: attempts are limited and ten wrong ones WIPE '
                      'the unit.  Power-cycle the unit (booting the OS refreshes '
                      'the budget), then try again.', file=sys.stderr)
                return False
            except Exception as e:
                if attempt == 2:
                    print(f"Bootrom handshake failed: {e}")
                    print("Power-cycle the device and retry.")
                    return False
                print(f"Bootrom handshake failed ({e}); rebooting")
                try:
                    bb.reboot()
                except Exception:
                    pass
                bb.close()
                time.sleep(2.5)
                if not bb.open([0x0001], timeout_ms=30000):
                    print("Device did not come back after reboot")
                    return False

        print(f"Model ID: 0x{model_id:08X}")
        try:
            bb.switch_channel()
            bb.get_metrics()
        except Exception as e:
            print(f"Channel switch/metrics failed: {e}")
            return False

        ramloader = find_loader(f"{model_id:08X}", loaders_dir)
        if not ramloader:
            print(f"No ramloader for {model_id:08X} in {loaders_dir}")
            return False

        addr = addr_override
        if addr is None:
            addr = struct.unpack_from('<I', ramloader, 8)[0]
        chunk_size = 260 if addr == 0x80100000 else 2024
        print(f"Sending ramloader to 0x{addr:08X} ({chunk_size}-byte chunks)")
        bb.send_loader(addr, ramloader, chunk_size=chunk_size)
        bb.run_loader(addr)
        time.sleep(1)

        for _ in range(300):
            bb.close()
            if bb.open([0x8001], timeout_ms=200):
                break
            time.sleep(0.1)
        else:
            print("Timeout waiting for the ramloader")
            return False

    print("Connected in ramloader")
    try:
        if not bb.set_mode(2):
            # A loader left running by an earlier session is already in mode 2 and
            # refuses the switch again; that is not fatal, the password stage decides.
            print('set_mode(2) refused; assuming the loader is already active')
        bb.password_info(password, debug)       # raises PasswordError on failure
        bb.last_bugdisp_log = bb.bugdisp_log()
        bb.last_flash_regions = bb.flash_regions_info()
    except Exception as e:
        print(f"Ramloader init failed: {e}")
        print("Power-cycle the device and retry.")
        return False
    return True


def boot_mode_cli(argv) -> int:
    """Read or change BOOT_MODE (loader command 0xDE)."""
    import argparse

    ap = argparse.ArgumentParser(description="BlackBerry ramloader BOOT_MODE (0xDE)")
    ap.add_argument('--boot-mode', action='store_true', help='read boot mode and factory counter')
    ap.add_argument('--factory', action='store_true', help='switch to FACTORY boot mode')
    ap.add_argument('--product', action='store_true', help='switch to PRODUCT boot mode')
    ap.add_argument('--set-count', type=int, metavar='N', help='set factory boot count (1..50)')
    ap.add_argument('--force', action='store_true', help='allow boot-mode writes')
    ap.add_argument('--from-bootrom', action='store_true', help='upload the ramloader first')
    ap.add_argument('--loaders', default=DEFAULT_LOADERS,
                    help='ramloader directory (default: auto-detected)')
    ap.add_argument('--addr', type=lambda s: int(s, 0), help='override ramloader load address')
    ap.add_argument('--debug', action='store_true', help='print raw bootrom responses')
    ap.add_argument('--password', default='', help='device password for the bootrom challenge')
    a = ap.parse_args(argv)

    writing = a.factory or a.product or (a.set_count is not None)
    if writing and not a.force:
        ap.error("refusing to write BOOT_MODE without --force")
    if not (a.boot_mode or writing):
        a.boot_mode = True

    bb = BBUSB()
    try:
        if not _open_ramloader(bb, a.loaders, a.from_bootrom, a.addr,
                               a.password or '', a.debug):
            return 1

        if a.factory or a.product:
            info = bb.set_boot_mode(a.factory)
        elif a.set_count is not None:
            info = bb.set_boot_count(a.set_count)
        else:
            info = bb.read_boot_mode()

        if info is None:
            print("No BOOT_MODE response (loader does not implement this action)")
            return 2

        status, mode, count = info
        print(f"status={status} ({BOOT_MODE_STATUS.get(status, 'unknown')})")
        print(f"mode={mode} ({BOOT_MODE_NAMES.get(mode, 'unknown')})")
        print(f"count={count}")
        return 0
    finally:
        try:
            bb.reboot_loader()
        except Exception:
            pass
        bb.close()


# =========================================================================== #
# QCFM container support — transcribed from bb10mt's qcfm.pas (GPL-3.0; see
# docs/project/DECISIONS.md).  No USB here: pack/unpack/split/parse work on a
# machine with no device and no pyusb.
# =========================================================================== #

import argparse
import json

BS = 0x10000                      # 64 KiB, the loader's unit
MFCQ_V1_HDR = 32                  # TMultiHeaderFileV1
QCFP = 40                         # TControlFileV1
RRV1 = 8                          # TRunRecordV1
MFCQ_V2_HDR = 28                  # TMultiHeaderFileV2
PFCQ = 44                         # TControlFileV2
RRCQ = 16                         # TRunRecordV2
SIG_LEN = 560

# qcfm.pas: image* constants (only the ones a BB10 image uses)
TYPE_BY_EXT = {
    '.nvram': 0x03, '.ufs': 0x05, '.mbr': 0x06, '.sig': 0x07,
    '.ifs': 0x08, '.rcfs': 0x09,
    '.radio.mbr': 0x0A, '.radio.sig': 0x0B, '.radio.rcfs': 0x0C,
    '.sig2': 0x0D, '.radio.sig2': 0x0E,
    '.calwork': 0x0F, '.calbackup': 0x10, '.dmi': 0x11,
    '.dmi.mbr': 0x12, '.dmi.fsys': 0x13, '.os': 0x18, '.dmi.sig2': 0x19,
}

# qcfm.pas `signature_data`: the QNXH-OS-1 / QNXL-OS-1 trailer RIM ships.  The
# last four bytes are replaced by a CRC at pack time, so their value here is the
# source placeholder.
SIGNATURE = bytes.fromhex(
    '514e58482d4f532d310000000000000000000000000000000000000000000000'
    '514e584888000000d142e927e7aba7fd1d8b6ac6f158a44692da24ab22f1eade'
    '0612a9053746f3be0139afc7f4a1f793afb614337b6947c8bad90f9b9fff7ad3'
    '05d478f16be51c813000000046a16c1e52a3a7c058680d3ed685e05b09e36da9'
    'bf200d3b52c6df3a4fb94876f168cc5ff8a6cacef3e519bbefe6f8d9b09a252b'
    'f2fb04bf0cbf989e154d010853010000bc00000001000100fd0ba6b5514e584c'
    '2d4f532d310000000000000000000000000000000000000000000000514e584c'
    '8800000099c4ecac02e1e2e27e3b669f624a410595b6752e728a38780354c86f'
    '803a5f5f6f4d63af0894b45f3f2891b7cf6a6e95feda6436f4576084cc3f269b'
    '2f537e7080000000da818405e4fe84adfe9a40c1babe0bc9a8fab72373e45207'
    '3a22e3eee119cf2751af8a9489855f01ecd0751d42e73940dfcf417581af5811'
    'ef66f2c8489d229276000000bc000000010001000e1cb7c6514e582d4f532d31'
    '000000000000000000000000000000000000000000000000514e580080000000'
    'bbaabfcee4a09d149fdcb4cadf35b7f3b5a2c16c3a920010f0b17e3ff023e2ba'
    'fdd7a80de7b5a3695e722dff8bf7a799747564486a2e6261c7f55534418f7a90'
    'a305c63e414784d35105464cf9a0b9521e1a218bee9416bbcaf316afe0228e16'
    '62425b81437eb06ef9fee39886df8f25e0daf59c00d874300b011dd85f918a9d'
    'b4000000010001001f2dc8d72398f24d'
)


def _get_value(n):
    """qcfm.pas: nrecords for the V1 record, padded to a 4-entry stride."""
    return 3 if n <= 3 else 7 + ((n - 4) // 4) * 4


def _is_full_ff(block):
    """umisc.pas IsFullFFBlock_Branchless, called as `(Buf, High(Buf))` — so it
    examines len(block)-1 bytes, not the whole block.  The off-by-one is
    reproduced deliberately: it decides which blocks go into the container."""
    body = block[:len(block) - 1] if len(block) > 1 else b''
    return len(body) > 0 and body.count(0xFF) == len(body)


def analyze_blocks(data, bs=BS):
    """qcfm.pas AnalyzeFileBlocks: runs of consecutive non-0xFF blocks.

    A full-0xFF block closes the current run and is not included; a short final
    block also closes it.  Returns [(block_index, count), ...].
    """
    ranges = []
    start = 0
    count = 0
    in_range = False
    nblocks = (len(data) + bs - 1) // bs
    for i in range(nblocks):
        block = data[i * bs:(i + 1) * bs]
        full_ff = _is_full_ff(block)
        if not full_ff:
            if not in_range:
                start = i
                count = 1
                in_range = True
            else:
                count += 1
        if len(block) < bs or full_ff:
            if in_range:
                ranges.append((start, count))
                in_range = False
    if in_range:
        ranges.append((start, count))
    return ranges


def _crc(*parts):
    v = 0
    for p in parts:
        v = zlib.crc32(p, v) & 0xFFFFFFFF
    return v


def build(files, ver=3, sign=True, fast=False, bs=BS, signature=None):
    """Build a QCFM container.  `files` is [(path, delta, flags), ...].

    ver is the same bitmask bb10mt takes: 1 = V1 `qcfp`, 2 = V2 `pfcq`, 3 = both.
    `sign=True` appends the 560-byte `QNXH` trailer in the form the source and the
    vendor container use.  bb10mt's `-s true` writes a different, 8-byte *dummy*
    signature on purpose — that is the one to use when flashing a modified UFS
    (skip the `.sig` component); see DEVELOPMENT.md §9.
    Returns (bytes, info dict).
    """
    if ver & ~3:
        raise ValueError('ver must be 1, 2 or 3')
    sig = SIGNATURE if signature is None else signature
    total_files = len(files)

    mhf1_flags = MFCQ_V1_HDR if ver > 1 else 0
    v1_blob = b''
    v2_records = b''

    for path, delta, flags in files:
        data = open(path, 'rb').read()
        if fast:
            nblocks = (len(data) + bs - 1) // bs
            ranges = [(0, nblocks)] if nblocks else []
        else:
            ranges = analyze_blocks(data, bs)
        ext = os.path.splitext(path)[1].lower()
        itype = TYPE_BY_EXT.get(ext, 0)

        if ver & 1:
            nrec = _get_value(len(ranges))
            runs = b''
            for idx, cnt in ranges:
                runs += struct.pack('<iI', delta + idx, cnt)
            runs += b'\x00' * ((nrec - len(ranges)) * RRV1)
            datacrc = _crc(runs) if runs else 0
            rec = struct.pack('<4sIIIIIIII4s', b'qcfp', 0, 1, nrec, bs,
                              0, 0, datacrc, flags, b'\x00' * 4)
            body = rec + runs
            chk = _crc(body[8:])
            rec = struct.pack('<4sIIIIIIII4s', b'qcfp', chk, 1, nrec, bs,
                              0, 0, datacrc, flags, b'\x00' * 4)
            v1_blob += rec + runs

        if ver & 2:
            rec = struct.pack('<4sIIIIIIIIII', b'pfcq', 0x20000,
                              PFCQ + len(ranges) * RRCQ, itype, PFCQ,
                              len(ranges), 0, 0, 0, 0, bs)
            for idx, cnt in ranges:
                rec += struct.pack('<4sIII', b'rrcq', RRCQ, idx, cnt)
            v2_records += rec

    if ver > 1:
        if ver == 2:
            headersz = (MFCQ_V1_HDR + MFCQ_V2_HDR +
                        total_files * (PFCQ + RRCQ))
            # mhf1.nheaders := 0 for a V2-only container (no qcfp records)
            mhf1_nheaders = 0
        else:
            headersz = 0x10000
            mhf1_nheaders = total_files
        mhf1_headersz = headersz + 0x20
        mhf2 = struct.pack('<4sIIIIII', b'mfcq', 0, 0x20000, MFCQ_V2_HDR,
                           total_files, headersz, 0)
    else:
        headersz = 0x10000
        mhf1_headersz = 0x10000
        mhf1_nheaders = total_files
        mhf2 = b''

    header = bytearray(MFCQ_V1_HDR)
    header += v1_blob
    v2_start = len(header)
    header += mhf2 + v2_records
    if len(header) < mhf1_headersz:
        header += b'\x00' * (mhf1_headersz - len(header))
    header[0:MFCQ_V1_HDR] = struct.pack('<4sIIIIII4s', b'mfcq', 0, 1,
                                        mhf1_nheaders, mhf1_headersz,
                                        0,
                                        v2_start if ver > 1 else 0,
                                        b'\x00' * 4)
    # Field order is magic, checksum, version, nheaders, headersz,
    # datachecksum, flags, dummy.  `flags` carries the V2 header offset
    # (BuildHeadersAndMap: `mhf1.flags := xPos`), and datachecksum stays 0 as in
    # the vendor container (verified: 2.1.0.1917 has flags 0x1C0, datacrc 0).
    struct.pack_into('<I', header, 0x18, v2_start if ver > 1 else 0)

    payload = b''
    for path, delta, flags in files:
        data = open(path, 'rb').read()
        if fast:
            nblocks = (len(data) + bs - 1) // bs
            ranges = [(0, nblocks)] if nblocks else []
        else:
            ranges = analyze_blocks(data, bs)
        for idx, cnt in ranges:
            chunk = data[idx * bs:(idx + cnt) * bs]
            payload += chunk + b'\x00' * (cnt * bs - len(chunk))

    out = bytes(header) + payload
    if sign:
        trailer = sig[:SIG_LEN - 4]
        crc = _crc(out, trailer)
        out += trailer + struct.pack('<I', crc)

    info = {
        'size': len(out),
        'headersz': mhf1_headersz,
        'v2_header_offset': v2_start if ver > 1 else None,
        'payload': len(payload),
        'signed': bool(sign),
        'files': [],
    }
    return out, info


def parse(data):
    """Read a `.signed` container back: headers, records and payload extent."""
    if len(data) < MFCQ_V1_HDR or data[0:4] != b'mfcq':
        raise ValueError('not an MFCQ container (magic %r)' % data[0:4])
    _, chk, ver, nheaders, headersz, datacrc, flags, _dummy = \
        struct.unpack_from('<4sIIIIII4s', data, 0)
    info = {
        'size': len(data),
        'v1': {'version': ver, 'nheaders': nheaders, 'headersz': headersz,
               'datachecksum': f'0x{datacrc:08X}', 'checksum': f'0x{chk:08X}',
               'v2_offset': flags, 'records': []},
        'v2': None,
        'payload_offset': headersz,
        'payload_size': max(0, len(data) - headersz -
                            (SIG_LEN if data[-SIG_LEN:-SIG_LEN + 8].startswith(
                                b'QNXH-OS-') else 0)),
        'signature': None,
    }

    off = MFCQ_V1_HDR
    for _ in range(nheaders):
        if data[off:off + 4] != b'qcfp':
            break
        (_m, _c, v, nrec, bs, dev, part, dcs, rec_flags,
         _d) = struct.unpack_from('<4sIIIIIIII4s', data, off)
        runs = []
        for i in range(nrec):
            r_off, r_cnt = struct.unpack_from('<iI', data, off + QCFP + i * RRV1)
            if r_cnt or r_off:
                runs.append({'offset': r_off, 'count': r_cnt})
        info['v1']['records'].append({
            'version': v, 'nrecords': nrec, 'blocksize': bs, 'device': dev,
            'partition': part, 'flags': rec_flags, 'runs': runs,
            'datachecksum': f'0x{dcs:08X}', 'checksum': f'0x{_c:08X}',
        })
        off += QCFP + nrec * RRV1

    v2_at = flags if flags else None
    if v2_at and data[v2_at:v2_at + 4] == b'mfcq':
        _m, _hc, v2ver, v2len, v2n, v2hz, _dc = \
            struct.unpack_from('<4sIIIIII', data, v2_at)
        info['v2'] = {'offset': v2_at, 'version': f'0x{v2ver:08X}',
                      'length': v2len, 'nheaders': v2n, 'headersz': v2hz,
                      'records': []}
        p = v2_at + MFCQ_V2_HDR
        for _ in range(v2n):
            if data[p:p + 4] != b'pfcq':
                break
            (_m, pv, plen, ptype, rrec, pn, hwv, hwvn,
             sigoff, sigsize, bs) = struct.unpack_from('<4sIIIIIIIIII', data, p)
            runs = []
            r = p + rrec
            for _i in range(pn):
                if data[r:r + 4] != b'rrcq':
                    break
                _rm, rlen, r_off, r_cnt = struct.unpack_from('<4sIII', data, r)
                runs.append({'offset': r_off, 'count': r_cnt})
                r += RRCQ
            info['v2']['records'].append({
                'version': f'0x{pv:08X}', 'length': plen,
                'type': f'0x{ptype:02X}', 'blocksize': bs,
                'sig_offset': sigoff, 'sig_size': sigsize, 'runs': runs})
            p += plen

    if data[-SIG_LEN:-SIG_LEN + 8] == b'QNXH-OS-':
        sig = data[-SIG_LEN:]
        calc = _crc(data[:-SIG_LEN], sig[:SIG_LEN - 4])
        info['signature'] = {
            'present': True,
            'stored_crc': f'0x{struct.unpack_from("<I", sig, SIG_LEN - 4)[0]:08X}',
            'computed_crc': f'0x{calc:08X}',
            'crc_ok': struct.unpack_from('<I', sig, SIG_LEN - 4)[0] == calc,
        }
    return info


TYPE_TO_EXT = {
    0x03: '.nvram', 0x05: '.ufs', 0x06: '.mbr', 0x07: '.sig', 0x08: '.ifs',
    0x09: '.rcfs', 0x0A: '.radio.mbr', 0x0B: '.radio.sig', 0x0C: '.radio.rcfs',
    0x0D: '.sig2', 0x0E: '.radio.sig2', 0x0F: '.calwork', 0x10: '.calbackup',
    0x11: '.dmi', 0x12: '.dmi.mbr', 0x13: '.dmi.fsys', 0x18: '.os',
    0x19: '.dmi.sig2',
}

AUTOLOADER_SIG = 0x97C5D59C      # START_SIGNATURE_DWORD, three in a row
AUTOLOADER_MAX_FILES = 10
PFCQ_SIGNATURE = 0x71636670      # 'pfcq'


def _read_container_header(f):
    """Read only the header region of a container.  Keeps the 1 GB vendors out of RAM."""
    f.seek(0)
    first = f.read(MFCQ_V1_HDR)
    if len(first) < MFCQ_V1_HDR or first[:4] != b'mfcq':
        raise ValueError('not an MFCQ container (magic %r)' % first[:4])
    magic, chk, ver, nheaders, headersz, datacrc, flags, _d = \
        struct.unpack('<4sIIIIII4s', first)
    rest = headersz - MFCQ_V1_HDR
    head = first + (f.read(rest) if rest > 0 else b'')
    return head, {'headersz': headersz, 'nheaders': nheaders,
                  'v2_offset': flags, 'size': os.fstat(f.fileno()).st_size}


def extract(path, outdir='.', prefix=None, log=print, only=None):
    """Unpack a container's components — the equivalent of bb10mt `unpack`.

    qcfm.pas `Chunk2Stream` semantics: each file's runs are placed at
    `blocksize * (run.offset - first_run_offset)` and the **gaps between runs are
    filled with 0xFF**, so a sparse component comes back at full size with its
    holes erased.  Also writes the `.lst` lines (`name=firstRunOffset,flags`) that
    `build -l` consumes.  `only` is a set of component indices to write (the rest
    are skipped over, not copied) — useful when a 1 GB container holds a 2.5 GB UFS.
    """
    out = []
    lst = []
    with open(path, 'rb') as f:
        head, h = _read_container_header(f)
        off = MFCQ_V1_HDR
        recs = []
        for _ in range(h['nheaders']):
            if head[off:off + 4] != b'qcfp':
                break
            (_m, _c, v, nrec, bs, dev, part, dcs, rf,
             _d) = struct.unpack_from('<4sIIIIIIII4s', head, off)
            runs = []
            for i in range(nrec):
                r_off, r_cnt = struct.unpack_from('<iI', head, off + QCFP + i * RRV1)
                if r_cnt:
                    runs.append((r_off, r_cnt))
            recs.append({'blocksize': bs, 'flags': rf, 'runs': runs})
            off += QCFP + nrec * RRV1

        types = []
        v2_at = h['v2_offset']
        if v2_at and head[v2_at:v2_at + 4] == b'mfcq':
            _m, _hc, _v, _l, n2, _hz, _dc = struct.unpack_from('<4sIIIIII', head, v2_at)
            p = v2_at + MFCQ_V2_HDR
            for _ in range(n2):
                if head[p:p + 4] != b'pfcq':
                    break
                (_m, _pv, plen, ptype, _rr, _pn, _hwv, _hn, _so, _ss,
                 _bs) = struct.unpack_from('<4sIIIIIIIIII', head, p)
                types.append(ptype)
                p += plen

        stem = prefix or os.path.splitext(os.path.basename(path))[0]
        pos = h['headersz']          # payload runs in the order the records list them
        for i, rec in enumerate(recs):
            if not rec['runs']:
                continue
            ext = TYPE_TO_EXT.get(types[i] if i < len(types) else 0, '.bin')
            name = '%s.%d%s' % (stem, i, ext)
            outpath = os.path.join(outdir, name)
            base = rec['runs'][0][0]
            if only is not None and i not in only:
                pos += sum(c for _, c in rec['runs']) * rec['blocksize']
                lst.append('%s=%d,%d' % (name, base, rec['flags']))
                continue
            with open(outpath, 'wb') as o:
                for run_off, run_cnt in rec['runs']:
                    target = rec['blocksize'] * (run_off - base)
                    while o.tell() < target:            # gap -> 0xFF, per Chunk2Stream
                        gap = target - o.tell()
                        o.write(b'\xff' * min(gap, rec['blocksize']))
                    remaining = run_cnt * rec['blocksize']
                    f.seek(pos)
                    while remaining > 0:
                        chunk = f.read(min(1 << 20, remaining))
                        if not chunk:
                            raise ValueError('container truncated')
                        o.write(chunk)
                        remaining -= len(chunk)
                        pos += len(chunk)
            lst.append('%s=%d,%d' % (name, base, rec['flags']))
            out.append({'name': name, 'size': os.path.getsize(outpath),
                        'type': types[i] if i < len(types) else None,
                        'runs': rec['runs']})
            log('%s (%d bytes, %d runs)' % (name, out[-1]['size'], len(rec['runs'])))
    return out, lst


def _pe_end(f):
    """pefile.pas GetPEEndOffset: end of the last raw section (or the section table)."""
    size = os.fstat(f.fileno()).st_size
    f.seek(0)
    dos = f.read(0x40)
    if len(dos) < 0x40 or dos[:2] != b'MZ':
        return 0
    e_lfanew = struct.unpack_from('<I', dos, 0x3C)[0]
    f.seek(e_lfanew)
    if f.read(4) != b'PE\x00\x00':
        return 0
    coff = f.read(20)
    nsec, = struct.unpack_from('<H', coff, 2)
    opt_size, = struct.unpack_from('<H', coff, 16)
    first = e_lfanew + 24 + opt_size
    f.seek(first)
    maxoff = first
    for _ in range(nsec):
        sh = f.read(40)
        if len(sh) < 40:
            break
        size_raw, = struct.unpack_from('<I', sh, 16)
        ptr_raw, = struct.unpack_from('<I', sh, 20)
        if ptr_raw and size_raw:
            maxoff = max(maxoff, ptr_raw + size_raw)
    return min(maxoff, size)


def autoloader_files(path):
    """Parse a Windows autoloader `.exe`: PE stub + triple signature + offset table."""
    with open(path, 'rb') as f:
        size = os.fstat(f.fileno()).st_size
        pe_end = _pe_end(f)
        if not pe_end:
            raise ValueError('not a PE file')
        f.seek(pe_end)
        window = f.read(min(size - pe_end, 1 << 20))
        triple = struct.pack('<III', AUTOLOADER_SIG, AUTOLOADER_SIG, AUTOLOADER_SIG)
        idx = -1
        for i in range(0, max(0, len(window) - 12), 4):
            if window[i:i + 12] == triple:
                idx = i
                break
        if idx < 0:
            raise ValueError('BlackBerry autoloader signature not found after the PE')

        after = pe_end + idx + 12
        version, count_at = 1, after
        f.seek(count_at)
        count, = struct.unpack('<Q', f.read(8))
        if not 1 <= count <= AUTOLOADER_MAX_FILES:
            version, count_at = 2, after + 80
            f.seek(count_at)
            count, = struct.unpack('<Q', f.read(8))
            if not 1 <= count <= AUTOLOADER_MAX_FILES:
                raise ValueError('no valid file count in the autoloader header')

        f.seek(count_at + 8)
        offs = list(struct.unpack('<%dQ' % count, f.read(8 * count)))
        if any(o >= size for o in offs):
            raise ValueError('autoloader offset table out of range')
        offs.append(size)

        files = []
        for i in range(count):
            off, n = offs[i], offs[i + 1] - offs[i]
            f.seek(off)
            head = f.read(min(64, n))
            kind = 0
            for j in range(0, max(0, min(len(head), 64) - 16)):
                if struct.unpack_from('<I', head, j)[0] == PFCQ_SIGNATURE:
                    kind = head[j + 12]
                    break
            label = {5: '@User', 6: '@OS', 8: '@IFS', 12: '@Radio'}.get(kind, '')
            files.append({'index': i, 'offset': off, 'size': n, 'type': kind,
                          'name': '%d%s.signed' % (i, label)})
    return {'pe_end': pe_end, 'version': version, 'count': count,
            'signature_at': pe_end + idx, 'files': files}


def split(path, outdir='.', log=print):
    """Extract an autoloader's appended files — the equivalent of bb10mt `split`."""
    hdr = autoloader_files(path)
    stem = os.path.splitext(os.path.basename(path))[0]
    written = []
    with open(path, 'rb') as f:
        for entry in hdr['files']:
            name = '%s.%s' % (stem, entry['name'])
            outpath = os.path.join(outdir, name)
            f.seek(entry['offset'])
            remaining = entry['size']
            with open(outpath, 'wb') as o:
                while remaining > 0:
                    chunk = f.read(min(1 << 20, remaining))
                    if not chunk:
                        raise ValueError('autoloader truncated')
                    o.write(chunk)
                    remaining -= len(chunk)
            written.append({'name': name, 'size': entry['size'], 'type': entry['type']})
            log('%s (%d bytes)' % (name, entry['size']))
    return hdr, written

# =========================================================================== #
# Device commands, CFP-style: info / flash / reset-osregion
#
# `info` is read-only by construction; `flash` and `reset-osregion` write and are
# dry-run unless --apply.  See docs/project/DEVELOPMENT.md §10-§13.
# =========================================================================== #

MCT_MAGIC = 0x92BE564A

# Labels come from the live MCT (2026-11-01) cross-checked against CFP's own
# renderer (report 161 §4).  Types in MCT_NON_RANGE do not carry a block range.
MCT_TYPES = {
    0x09: 'MCT CRC',
    0x18: 'Bootrom',
    0x1B: 'OS Fixed',
    0x1C: 'FS Fixed (UFS)',
    0x1D: 'Flash chip',
    0x1E: 'OS NV (NVRAM)',
    0x23: 'RAM chip',
    0x25: 'MMC Enhanced',
    0x26: 'NAND config',
    0x2B: 'Boot0 MMC',
    0x2C: 'Boot1 MMC',
    0x31: 'User MMC',
    0x32: 'unknown',
    0x33: 'MFG data (tentative)',
    0x34: 'MBR',
    0x35: 'QNX region',
    0x36: 'unknown 0x4845-0x4904',
}
MCT_NON_RANGE = {0x09, 0x26, 0x32}

BOOT_MODE_NAMES = {0: 'product', 1: 'factory', 2: 'expired'}
BOOT_MODE_STATUS = {
    0: 'ok', 1: 'not allowed', 2: 'boot count out of range',
    3: 'cannot set in this device mode',
}

# (name, op, expected response op, request payload, note) — reports 162/164.
INFO_READS = [
    ('osregion_record', 0xE9, 0xFB, struct.pack('<I', 0),
     'NVRAM 0x2074, 24-byte record (the reply carries 0x1C)'),
    ('csd_extcsd', 0xB5, 0xD3, b'', 'CSD/ExtCSD image, 492 B on the wire'),
    ('dram_info', 0xBF, 0xD9, b'', 'DRAM identity, 36 B'),
    ('hardware_override_id', 0xBD, 0xBE, b'', 'hardware override ID'),
    ('vendor_id', 0xDB, 0xCB, b'', 'vendor ID'),
    ('installer_metrics', 0xE1, 0xC8, b'', 'installer metrics, 0x3FF0 B'),
    ('os_metrics', 0xD8, 0xC8, b'', 'OS metrics, 0x3FF0 B'),
    ('dsp_os_metrics', 0xB2, 0xC8, b'',
     'DSP OS metrics — a stub in LDR_01 (0x80117DBC): zeros'),
]

OSREGION_RECORD = 0x2074
ACTION_DELETE_OSREGION = 4

REGION0_START = 0x815          # V1 qcfp offsets are relative to this
MBR_START = 0x805
RAW_SHIFT = 0x15               # raw byte = (block - 0x15) * 0x10000
PREFLASH_DEFAULT = 0x15
PREFLASH_PB_2C0A = 0x40
EXTENTS = {                    # measured MCT extents, 64 KiB loader blocks
    '0x1B OS Fixed': (0x815, 0x4804),
    '0x1C FS Fixed': (0x4905, 0xFFFFF),
}


# ---------------------------------------------------------------- MCT parsing
def printable_runs(buf, minlen=4):
    """(offset, text) for every printable run, so an unknown blob can be mapped
    from the data instead of guessed offsets."""
    out = []
    i, n = 0, len(buf)
    while i < n:
        if 32 <= buf[i] < 127:
            j = i
            while j < n and 32 <= buf[j] < 127:
                j += 1
            if j - i >= minlen:
                out.append((i, buf[i:j].decode('ascii')))
            i = j
        else:
            i += 1
    return out


def parse_mct(buf):
    """Walk the MCT TLVs: {u8 type, u8 length}, length includes the two header
    bytes.  Range payloads are {u16 ?, u32 start, u32 end} in 64 KiB blocks —
    except the types in MCT_NON_RANGE, which are not ranges.

    The header is 8 bytes (magic, minor, major, pad) — measured live on
    2026-11-01, where the walk consumed all 240 bytes exactly.  The size is still
    *searched* (8/7/12/16) and the winner reported, so a different reply cannot
    silently misparse.
    """
    out = {'size': len(buf), 'magic': None, 'header_size': None, 'tlvs': []}
    if len(buf) < 8:
        out['error'] = 'too short'
        return out
    magic = struct.unpack_from('<I', buf, 0)[0]
    out['magic'] = '0x%08X' % magic
    if magic != MCT_MAGIC:
        out['error'] = 'bad magic (not an MCT)'
        return out
    out['minor'] = struct.unpack_from('<H', buf, 4)[0]
    out['major'] = buf[6]

    def walk(start):
        tlvs = []
        off = start
        while off + 2 <= len(buf):
            t, ln = buf[off], buf[off + 1]
            if t == 0 and ln == 0:
                return tlvs if not any(buf[off:]) else None
            if ln < 2 or off + ln > len(buf):
                return None
            tlvs.append((t, buf[off + 2:off + ln], off))
            off += ln
        return tlvs if off == len(buf) else None

    best = None
    for header in (8, 7, 12, 16):
        tlvs = walk(header)
        if tlvs is None:
            continue
        if best is None or len(tlvs) > len(best[1]):
            best = (header, tlvs)

    if best is not None:
        header, tlvs = best
        out['header_size'] = header
        for t, payload, off in tlvs:
            rec = {'offset': '0x%02X' % off, 'type': '0x%02X' % t,
                   'name': MCT_TYPES.get(t, '?'), 'length': len(payload) + 2,
                   'payload': payload.hex()}
            if len(payload) == 10 and t not in MCT_NON_RANGE:
                _dummy, start_b, end_b = struct.unpack('<HII', payload)
                rec['range'] = {'start_block': '0x%05X' % start_b,
                                'end_block': '0x%05X' % end_b,
                                'start_byte': '0x%09X' % (start_b * 0x10000),
                                'end_byte': '0x%09X' % (end_b * 0x10000)}
                if t == 0x35:
                    # live: region 1 arrives as 0x0100, so the id is the second
                    # byte, not the u16 (report 100's "id 0 / id 1")
                    rec['partition_id'] = payload[1]
            out['tlvs'].append(rec)
    if out['header_size'] is None:
        out['error'] = 'no clean TLV walk for header sizes 8/7/12/16'
    return out


def parse_flash_regions(buf):
    """0xB4 returns 188 B; only the eMMC identity strings are established."""
    printable = bytes(c if 32 <= c < 127 else 0x2E for c in buf)
    runs = [r for r in printable.split(b'.') if len(r) >= 4]
    return {'size': len(buf), 'strings': [r.decode('ascii') for r in runs],
            'hex': buf.hex()}


def read_action_log(bb, timeout=3000):
    """Loader action log — opcode 0xB3, NOT 0xB0.

    0xB0 is the *bugdisp crash* log.  The action log is plain ASCII in NVRAM record
    `0x2033` (report 165): no crypto anywhere in the path.

    **The request payload MUST be 132 bytes** — `{u32 action, char text[128]}` — even
    for a read (`action = 0`).  The loader's 0xB3 handler does no length check and
    would `strlen()` past a shorter frame; the append path (`action = 1`) would then
    write that into NVRAM `0x2033`.  We only ever send `action = 0`.

    Reply: `{u32 textLen, char text[textLen]}` — a packed array of **NUL-terminated**
    records, newest first (report 166).  CFP walks it with `cursor += strlen+1`.
    """
    payload = struct.pack('<I', 0) + b'\x00' * 128
    cmd, data = bb.channel2(CMD_ACTION_LOG, payload, timeout=timeout)
    if cmd != RESP_ACTION_LOG:
        raise RuntimeError('0xB3 -> unexpected response 0x%02X' % cmd)
    if len(data) < 4:
        raise RuntimeError('0xB3 reply too short (%d bytes)' % len(data))
    n = struct.unpack_from('<I', data, 0)[0]
    text = data[4:4 + min(n, max(0, len(data) - 4))]
    return text.decode('ascii', 'replace')


ACTION_LOG_ERROR_CODES = {0: 'Success!'}   # only 0 is readable in CFP's table


def parse_action_log(text):
    """Split the packed 0xB3 payload into entries (report 166).

    Records are NUL-terminated.  Two shapes:

        [SDBE]:HEX8                 a short code, printed verbatim
        HEX8:tail                    a timestamped record: the hex is a Unix epoch
                                     rendered with localtime, `tail` is appended

    A trailing `--<decimal>` is CFP's compact status: it becomes
    `-ErrorCode:<n>-<text>` with the text from CFP's table (`0` = "Success!").  Note
    the `-` that survives in "…DEV-ErrorCode:0-Success!" — CFP only cuts back the
    digits plus one dash.
    """
    out = []
    for raw in text.split('\x00'):
        raw = raw.strip('\r\n')
        if not raw:
            continue
        m = re.match(r'\A([SDBE]):([0-9A-F]{8})\Z', raw)
        if m:
            out.append({'kind': 'code', 'text': raw, 'code': raw[0],
                        'value': int(m.group(2), 16)})
            continue
        m = re.match(r'\A([0-9A-F]{8}):(.*)\Z', raw, re.S)
        if m:
            epoch = int(m.group(1), 16)
            tail = m.group(2)
            try:
                when = time.strftime('%a %b %e %H:%M:%S %Y', time.localtime(epoch))
            except (ValueError, OSError):
                when = m.group(1)
            status = None
            mm = re.search(r'--(\d+)\Z', tail)
            if mm:
                status = int(mm.group(1))
                tail = (tail[:mm.start()] + '-ErrorCode:%d-%s'
                        % (status, ACTION_LOG_ERROR_CODES.get(status, '?')))
            out.append({'kind': 'entry', 'epoch': epoch, 'when': when,
                        'status': status, 'text': when + ':' + tail, 'raw': raw})
            continue
        out.append({'kind': 'unknown', 'text': raw})
    return out


def parse_lal(buf):
    """Loader action log: ASCII lines mixed with binary records."""
    printable = bytes(c if 32 <= c < 127 else 0x0A for c in buf)
    lines, seen = [], set()
    for chunk in printable.split(b'\n'):
        s = chunk.strip().decode('ascii', 'replace')
        if len(s) >= 16 and s not in seen:
            seen.add(s)
            lines.append(s)
    return lines


# ---------------------------------------------------------------------------
# Decoded-structure tables.  These blobs are *decoded* structs, not raw
# registers, so offsets cannot be derived from the JEDEC byte layout.  Both
# tables were solved by aligning CFP's own printed field list (report 161 data,
# docs/reports/data/cfp-2026-11-01/CFP-flashinfo.txt) against the live capture
# (docs/reports/data/live-info-2026-11-01/): every entry below was matched to a
# value CFP prints, in order, with 1/2/4-byte little-endian widths.
# ---------------------------------------------------------------------------
CSD_EXTCSD_FIELDS = [
    (0x000, 1, 'csd_version_number'), (0x001, 1, 'specification_version'),
    (0x002, 1, 'taac'), (0x003, 1, 'nsac'), (0x004, 4, 'tran_speed'),
    (0x018, 4, 'c_size'), (0x01C, 2, 'ccc'), (0x01E, 1, 'read_bl_len'),
    (0x01F, 1, 'vdd_r_curr_min'), (0x020, 1, 'vdd_r_curr_max'),
    (0x021, 1, 'vdd_w_curr_min'), (0x022, 1, 'vdd_w_curr_max'),
    (0x023, 4, 'c_size_mult'), (0x028, 4, 'wp_grp_enable'),
    (0x02D, 1, 'wp_grp_size'), (0x02E, 1, 'r2w_factor'),
    (0x02F, 4, 'write_bl_len'), (0x038, 4, 'copy'),
    (0x045, 1, 'erase_grp_size'), (0x046, 2, 'erase_grp_mult'),
    (0x048, 4, 'sector_count'), (0x04C, 4, 'out_of_interrupt_time'),
    (0x059, 1, 'card_type'), (0x05A, 1, 'csd_structure'),
    (0x05B, 4, 'ext_csd_rev'), (0x05F, 2, 'hs_timing'),
    (0x062, 1, 'trim_mult'), (0x063, 1, 'sec_feature_support'),
    (0x064, 1, 'sec_erase_mult'), (0x065, 1, 'sec_trim_mult'),
    (0x066, 1, 'boot_info'), (0x067, 1, 'boot_size_mult'),
    (0x068, 1, 'acc_size'), (0x069, 1, 'hc_erase_grp_size'),
    (0x06A, 1, 'erase_timeout_mult'), (0x06B, 1, 'rel_wr_sec'),
    (0x06C, 1, 'hc_wp_grp_size'), (0x06D, 1, 's_c_vdd'),
    (0x06E, 1, 's_c_vddq'), (0x06F, 4, 's_a_timeout'),
    (0x073, 2, 'erase_group_def'), (0x075, 1, 'user_wp'),
    (0x078, 1, 'fw_config'), (0x079, 2, 'rpmb_size_mult'),
    (0x07B, 1, 'partition_support'), (0x07C, 4, 'max_enh_size_mult'),
    (0x080, 1, 'partitions_attribute'), (0x081, 4, 'partition_setting_completed'),
    (0x095, 4, 'sec_bad_blk_mgmnt'), (0x099, 1, 'bkops_support'),
]

# Verified field-for-field against CFP's `info` block on 2026-11-01.
# NOTE: report 162's metric offsets are 4 lower for the upper block
# (options 0x84 there vs 0x88 here) — this blob starts 4 bytes earlier.
BOOTROM_METRICS_FIELDS = [
    (0x04, 2, 'metrics_version_minor'), (0x06, 2, 'metrics_version_major'),
    (0x0C, 4, 'br_version_packed'), (0x10, 4, 'hardware_id'),
    (0x54, 16, 'build_user'), (0x64, 16, 'build_date'), (0x74, 12, 'build_time'),
    (0x88, 4, 'supported_options'), (0xA0, 4, 'drivers'),
    (0xA4, 4, 'ldr_blocks'), (0xA8, 4, 'bootrom_size'),
    (0xB0, 4, 'processor'), (0xB4, 4, 'flash_id'),
]
BOOTROM_STRINGS = ((0x14, 32, 'hardware_id_string'),)


def decode_csd_extcsd(data):
    """The 492-byte CSD/ExtCSD blob as the 50 fields whose offsets are solved."""
    out = {}
    for off, width, name in CSD_EXTCSD_FIELDS:
        if off + width <= len(data):
            out[name] = int.from_bytes(data[off:off + width], 'little')
    return out


def _cstr(data, off, maxlen):
    raw = data[off:off + maxlen]
    return raw.split(b'\x00')[0].decode('ascii', 'replace')


def decode_bootrom_metrics(data):
    """The 736-byte bootrom metrics (channel 0, cmd 5, var 2).

    Only fields verified against CFP's `info` block are decoded: security,
    hardware-OS-id and BR-id were *not* located in this blob (report 162 notes
    that some of those live on CFP's metrics object instead).
    """
    out = {}
    for off, width, name in BOOTROM_METRICS_FIELDS:
        if off + width > len(data):
            continue
        if name in ('build_user', 'build_date', 'build_time'):
            out[name] = _cstr(data, off, width)
        else:
            out[name] = int.from_bytes(data[off:off + width], 'little')
    for off, maxlen, name in BOOTROM_STRINGS:
        if off < len(data):
            out[name] = _cstr(data, off, maxlen)
    v = out.get('br_version_packed')
    if v is not None:
        # 0x051B0014 -> CFP prints "5.27.0.20", i.e. bytes b3.b2.b1.b0
        out['br_version'] = '%d.%d.%d.%d' % ((v >> 24) & 0xFF, (v >> 16) & 0xFF,
                                             (v >> 8) & 0xFF, v & 0xFF)
    if 'metrics_version_major' in out:
        out['metrics_version'] = '%d.%d' % (out['metrics_version_major'],
                                            out.get('metrics_version_minor', 0))
    return out


# os_metrics (0xD8, 16,368 B) — the OS-version block.  Verified field-for-field
# against CFP's OS block 2026-11-01: metrics 3.18, OS 2.0.0.4869, address
# 0x800FF800-0x803BF60F (the field holds the *exclusive* end), build user/date/time,
# hardware id and its string, and the IMS image magic 0xC3D0E193.
OS_METRICS_FIELDS = [
    (0x00, 2, 'metrics_minor'), (0x02, 2, 'metrics_major'),
    (0x08, 4, 'os_start'), (0x0C, 4, 'os_end_exclusive'),
    (0x7C, 4, 'hardware_id'), (0x80, 2, 'os_build'), (0x83, 1, 'os_major'),
    (0x9C, 4, 'image_magic'),
]
OS_METRICS_STRINGS = (
    (0x14, 16, 'build_user'), (0x24, 16, 'build_date'),
    (0x30, 12, 'build_time'), (0x3C, 32, 'hardware_id_string'),
)


def decode_os_metrics(data):
    """0xD8: the OS-version block, decoded against CFP's own output."""
    out = {}
    for off, width, name in OS_METRICS_FIELDS:
        if off + width <= len(data):
            out[name] = int.from_bytes(data[off:off + width], 'little')
    for off, maxlen, name in OS_METRICS_STRINGS:
        if off < len(data):
            out[name] = _cstr(data, off, maxlen)
    if 'metrics_major' in out:
        out['metrics_version'] = '%d.%d' % (out['metrics_major'], out['metrics_minor'])
    if 'os_build' in out:
        out['os_version'] = '%d.0.0.%d' % (out.get('os_major', 0), out['os_build'])
    if 'os_start' in out and 'os_end_exclusive' in out:
        out['os_address'] = '0x%08X-0x%08X' % (out['os_start'], out['os_end_exclusive'] - 1)
    if 'image_magic' in out:
        out['image_magic_hex'] = '0x%08X' % out['image_magic']
    return out


DRAM_VENDORS = {1: 'Samsung', 2: 'Toshiba', 3: 'Elpida', 4: 'Micron',
                5: 'Hynix', 6: 'Nanya'}


def decode_dram(data):
    """0xBF, 36 B.  Proven against CFP's own DRAM block on 2026-11-01:
    word2 = size in MB (0x400 = 1024), word4 = vendor id (3 = Elpida),
    word5 = revision (0x100)."""
    w = struct.unpack_from('<%dI' % (len(data) // 4), data)
    return {'size_mb': w[2], 'vendor_id': w[4],
            'vendor': DRAM_VENDORS.get(w[4], '?'), 'revision': '0x%X' % w[5],
            'words': ['0x%08X' % x for x in w]}


def decode_vendor_id(data):
    """0xDB, 4 B: {u16 ?, u16 branding id}.  Live 0x01F8 = 504, which is CFP's
    "Branding: VendorID 504"."""
    if len(data) < 4:
        return {'raw': data.hex()}
    a, b = struct.unpack_from('<HH', data)
    return {'word0': a, 'branding_id': b, 'raw': data.hex()}


def byte_entropy(data):
    """Bits per byte.  ~8.0 over a large buffer means encrypted or compressed."""
    if not data:
        return 0.0
    counts = {}
    for b in data:
        counts[b] = counts.get(b, 0) + 1
    n = len(data)
    return -sum((c / n) * __import__('math').log2(c / n) for c in counts.values())


def decode_osregion(data):
    """The 24-byte OSREGION record: word1 = version, word2 = boot counts.

    Live 2026-11-01: [0, 3, 0x017F0000, 0,0,0,0] vs CFP's "Boot OS Count
    383,0,0 / 3,0,0 (Version,Requested Region,Actual Region)" — so the high u16 of
    word2 is the region-0 boot count.
    """
    if len(data) < 12:
        return {'words': ['0x%08X' % w for w in struct.unpack_from('<%dI' % (len(data) // 4), data)]}
    w = struct.unpack_from('<7I', data)
    return {'words': ['0x%08X' % x for x in w],
            'version': w[1],
            'region0_boots': w[2] >> 16,
            'region1_boots': w[2] & 0xFFFF,
            'all_zero': all(x == 0 for x in w)}


# ------------------------------------------------------------------- flashing
def split_signature(container):
    """FlashFile: a trailing 560-byte 'QNXH' block is the signature, not payload."""
    if len(container) > SIG_LEN and container[-SIG_LEN:-SIG_LEN + 4] == b'QNXH':
        return container[:-SIG_LEN], container[-SIG_LEN:]
    return container, None


def flash_frames(payload):
    """{u32 seq, u32 len} + chunk, chunks of MAX_FLASH_BLOCK-8, per ramloader.pas."""
    seq, off = 0, 0
    while off < len(payload):
        chunk = payload[off:off + MAX_FLASH_BLOCK - 8]
        yield seq, chunk
        off += len(chunk)
        seq += 1


def gate_runs(runs):
    """Destinations outside the measured extents -> list of (run, dest, last)."""
    bad = []
    for run in runs:
        dest = REGION0_START + run['offset']
        last = dest + run['count'] - 1
        if not any(lo <= dest and last <= hi for lo, hi in EXTENTS.values()):
            bad.append((run, dest, last))
    return bad


def plan_container(container):
    """Every write the loader would perform, from the V1 records."""
    info = parse(container)
    runs = []
    for rec in info['v1']['records']:
        for run in rec['runs']:
            dest = REGION0_START + run['offset']
            runs.append({'offset': run['offset'], 'count': run['count'],
                         'dest_block': dest,
                         'dest_raw': (dest - RAW_SHIFT) * 0x10000})
    return info, runs


def send_container(bb, payload, sig, preflash_val=None, verbose=False):
    """PreFlash -> 0xF7 frames -> 0x40F9 signature -> 0x40C0 complete."""
    if preflash_val is None:
        try:
            model = bb.get_model_id()
        except Exception:
            model = None
        preflash_val = (PREFLASH_PB_2C0A
                        if model is not None and (model & 0xFFFF) == 0x2C0A
                        else PREFLASH_DEFAULT)
        if model is not None:
            print('model id 0x%08X -> PreFlash 0x%X' % (model, preflash_val))

    desc = bytearray(36)
    desc[0] = preflash_val
    desc[1] = 0x28
    desc[6] = 0x02
    desc[28] = 0x01
    desc[32] = 0x02
    cmd, resp = bb.channel2(0x20EE, bytes(desc), timeout=15000)
    print('PreFlash 0x20EE (%d) -> cmd 0x%X len %d' % (preflash_val, cmd, len(resp)))
    if cmd not in (0x39, 0x20DE):
        raise SystemExit('PreFlash failed (cmd 0x%X)' % cmd)

    off = 0
    seq = 0
    for seq, chunk in flash_frames(payload):
        cmd, data = bb.channel2(0xF7, struct.pack('<II', seq, len(chunk)) + chunk,
                                timeout=5000)
        if cmd != 0xDF:
            raise SystemExit('0xF7 seq %d -> cmd 0x%X data %s'
                             % (seq, cmd, data[:8].hex()))
        off += len(chunk)
        if verbose or (seq + 1) % 8 == 0 or off == len(payload):
            print('  %d/%d payload bytes, seq %d' % (off, len(payload), seq))
    if sig:
        cmd, data = bb.channel2(0x40F9, struct.pack('<H', SIG_LEN) + sig, timeout=15000)
        print('0x40F9 signature -> cmd 0x%X len %d' % (cmd, len(data)))
    for op in (0x40C0, 0xC0):
        try:
            cmd, data = bb.channel2(op, b'', timeout=60000)
            print('COMPLETE 0x%X -> cmd 0x%X len %d' % (op, cmd, len(data)))
            if cmd in (0x4006, 0x402E, 6, 0x4015):
                break
        except Exception as e:
            print('COMPLETE 0x%X failed: %s' % (op, e))
    return seq


# --------------------------------------------------------------- device plumbing
def _add_device_args(ap):
    ap.add_argument('--from-bootrom', action='store_true',
                    help='upload the ramloader first (device in BootROM mode)')
    ap.add_argument('--loaders', default=None,
                    help='ramloader directory (default: auto-detected)')
    ap.add_argument('--addr', type=lambda s: int(s, 0),
                    help='override the ramloader load address')
    # NO DEFAULT ON PURPOSE.  A guessed password spends one of the device's
    # attempts, and ten wrong attempts WIPE the unit (UFS/RCFS/IFS plus NVRAM
    # records, including the password record).  If omitted we pass nothing and the
    # library prompts only when the device actually challenges.
    ap.add_argument('--password', default=None,
                    help='device password; omitted = prompt only if challenged')
    ap.add_argument('--debug', action='store_true')


def _connect(args, reboot_on_exit=True):
    bb = BBUSB()
    if not _open_ramloader(bb, args.loaders or DEFAULT_LOADERS, args.from_bootrom,
                           args.addr, args.password or '', args.debug):
        raise SystemExit('loader init failed (wrong/absent password is one cause, '
                         'but check the USB state first)')
    return bb


def _loader_present(pid=0x8001):
    if usb is None:
        return False
    try:
        return usb.core.find(idVendor=0x0fca, idProduct=pid) is not None
    except Exception:
        return False


def leave_loader(bb=None, attempts=3, verbose=True):
    """Reset the unit out of the ramloader so it boots media.

    CFP calls this "Requesting BootImage Exit".  A single `0x80EF` is often accepted
    without the unit actually resetting (a lesson from `pre_os_ifs_flash.py`), so this
    retries on fresh handles, checks whether `0fca:8001` disappeared, and says so.
    """
    if bb is not None:
        try:
            bb.reboot_loader()
        except Exception:
            pass
        try:
            bb.close()
        except Exception:
            pass
    if not _loader_present():
        if verbose:
            print('loader exit: device released', file=sys.stderr)
        return True
    for attempt in range(1, attempts + 1):
        b2 = BBUSB()
        if not b2.open([0x8001], timeout_ms=2000):
            if not _loader_present():
                if verbose:
                    print('loader exit: device released', file=sys.stderr)
                return True
            continue
        try:
            for op in (0x80EF, 0xEF, 0x80EF):
                try:
                    b2.channel2(op, b'', timeout=800)
                except Exception:
                    pass
                time.sleep(0.4)
        finally:
            try:
                b2.close()
            except Exception:
                pass
        time.sleep(1.0)
        if not _loader_present():
            if verbose:
                print('loader exit: device released (attempt %d)' % attempt,
                      file=sys.stderr)
            return True
    if verbose:
        print('loader exit: unit STILL in the ramloader — run `bbusb.py reset-loader`',
              file=sys.stderr)
    return False


def _leave(bb, reboot=True, verbose=True):
    if reboot:
        leave_loader(bb, verbose=verbose)
        return
    try:
        bb.close()
    except Exception:
        pass


# ------------------------------------------------------------------------ info
def cmd_info(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py info',
                                 description='read-only device info (CFP info analogue)')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--raw-out', metavar='DIR', help='save every raw response')
    ap.add_argument('--mct-file', metavar='FILE', help='parse an MCT dump offline')
    ap.add_argument('--keep-loader', action='store_true',
                    help='do not reset the unit out of the ramloader afterwards')
    _add_device_args(ap)
    args = ap.parse_args(argv)

    if args.mct_file:
        with open(args.mct_file, 'rb') as f:
            mct = parse_mct(f.read())
        print_mct(mct)
        return 0 if 'error' not in mct else 2

    result = {}
    raw = {}

    def keep(name, data):
        raw[name] = data.hex()
        if args.raw_out:
            os.makedirs(args.raw_out, exist_ok=True)
            with open(os.path.join(args.raw_out, name + '.bin'), 'wb') as f:
                f.write(data)

    bb = _connect(args)
    try:
        # The boot-ROM metrics are a *bootrom-mode* read (channel 0, cmd 5, var 2),
        # captured during the handshake — asking for them after the loader upload
        # times out.  Only br_version and model_id are proven offsets; the rest is
        # printed as strings + hex rather than guessed.
        m = getattr(bb, 'last_bootrom_metrics', None)
        if m is None:
            try:
                m = bb.get_var(2, 2000)
            except Exception:
                m = None
        if m:
            keep('bootrom_metrics', m)
            entry = {'len': len(m), 'hex_head': m[:32].hex()}
            if len(m) >= 20:
                entry['br_version'] = '0x%08X' % struct.unpack_from('<I', m, 12)[0]
                entry['model_id'] = '0x%08X' % struct.unpack_from('<I', m, 16)[0]
            entry.update(decode_bootrom_metrics(m))
            strings = printable_runs(m)
            if strings:
                entry['strings'] = [{'offset': '0x%02X' % off, 'text': txt}
                                    for off, txt in strings[:16]]
            result['metrics'] = entry
        else:
            result['metrics_error'] = ('not available in this session — the boot ROM '
                                       'metrics are read before the loader upload; '
                                       'connect with --from-bootrom')

        try:
            cmd, data = bb.channel2(CMD_BLOCK_SIZE, b'', timeout=3000)
            if cmd == 0xC8 and len(data) >= 12:
                units = struct.unpack_from('<I', data, 8)[0]
                result['block_size'] = {'block_size': struct.unpack_from('<I', data, 4)[0],
                                        'units': units, 'approx_bytes': units * struct.unpack_from('<I', data, 4)[0]}
                keep('block_size', data)
        except Exception as e:
            result['block_size_error'] = str(e)

        try:
            data = bb.flash_regions_info()
            result['flash_regions'] = parse_flash_regions(data)
            keep('flash_regions', data)
        except Exception as e:
            result['flash_regions_error'] = str(e)

        try:
            cmd, data = bb.channel2(CMD_MCT, b'', timeout=3000)
            mct = parse_mct(data)
            mct['raw_cmd'] = '0x%02X' % cmd
            result['mct'] = mct
            keep('mct', data)
        except Exception as e:
            result['mct_error'] = str(e)

        try:
            pin = bb.device_pin()
            # live 2026-11-01: the u32 is 0x50175127 and CFP prints 50175127, so
            # the PIN is the value's *hex digits*, not its decimal.
            result['pin'] = {'raw': '0x%08X' % pin, 'pin': '%08X' % pin}
        except Exception as e:
            result['pin_error'] = str(e)

        try:
            bm = bb.read_boot_mode()
            if bm is not None:
                status, mode, count = bm
                result['boot_mode'] = {'status': status,
                                       'status_name': BOOT_MODE_STATUS.get(status, 'unknown'),
                                       'mode': mode,
                                       'mode_name': BOOT_MODE_NAMES.get(mode, 'unknown'),
                                       'factory_count': count}
        except Exception as e:
            result['boot_mode_error'] = str(e)

        # The real loader action log: 0xB3, plain ASCII from NVRAM 0x2033 (report 165).
        try:
            text = read_action_log(bb)
            keep('action_log', text.encode('ascii', 'replace'))
            result['action_log_raw_len'] = len(text)
            result['action_log'] = parse_action_log(text)
        except Exception as e:
            result['action_log_error'] = str(e)

        # 0xB0 is a *different* log: the bugdisp crash log, served encrypted with a
        # plaintext marker record.  Kept for completeness, clearly labelled.
        bug = getattr(bb, 'last_bugdisp_log', b'')
        if not bug:
            try:
                bug = bb.bugdisp_log()
            except Exception:
                pass
        if bug:
            keep('bugdisp_log', bug)
            result['bugdisp_log'] = parse_lal(bug)
            result['bugdisp_log_hex'] = bug[:64].hex()
            result['bugdisp_log_len'] = len(bug)
            body = bug[0x100:] if len(bug) > 0x200 else bug
            if len(body) > 256:
                result['bugdisp_log_entropy'] = round(byte_entropy(body), 2)

        result['probes'] = {}
        for name, op, expect, request, note in INFO_READS:
            entry = {'op': '0x%02X' % op, 'expected': '0x%02X' % expect,
                     'request': request.hex(), 'note': note}
            try:
                cmd, data = bb.channel2(op, request, timeout=3000)
                entry.update({'resp': '0x%02X' % cmd, 'len': len(data),
                              'ok': cmd == expect, 'hex_head': data[:40].hex()})
                if name == 'osregion_record':
                    entry.update(decode_osregion(data))
                elif name == 'dram_info' and len(data) >= 24:
                    entry.update(decode_dram(data))
                elif name == 'vendor_id':
                    entry.update(decode_vendor_id(data))
                elif name == 'csd_extcsd':
                    entry['decoded'] = decode_csd_extcsd(data)
                elif name == 'os_metrics':
                    entry.update(decode_os_metrics(data))
                keep(name, data)
            except Exception as e:
                entry['error'] = str(e)
            result['probes'][name] = entry
    finally:
        _leave(bb, reboot=not getattr(args, 'keep_loader', False))

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print_info(result)
    return 0


def print_mct(mct):
    print('MCT: %d bytes, magic %s, minor 0x%04X, major %d, header %s'
          % (mct['size'], mct['magic'], mct.get('minor', 0), mct.get('major'),
             mct['header_size']))
    if 'error' in mct:
        print('  error: %s' % mct['error'])
    w = max((len(t['name']) for t in mct['tlvs']), default=8)
    for t in mct['tlvs']:
        line = '  %s  %-*s  len %3d' % (t['type'], w, t['name'], t['length'])
        if 'range' in t:
            r = t['range']
            line += '  blocks %s..%s' % (r['start_block'], r['end_block'])
            if 'partition_id' in t:
                line += '  id %d' % t['partition_id']
        print(line)


def print_info(r):
    print('=' * 68)
    print('PlayBook device info (read-only; CFP-info analogue)')
    print('=' * 68)
    m = r.get('metrics')
    if m:
        print('\nBootrom (%d-byte metrics blob):' % m['len'])
        if m.get('hardware_id_string'):
            print('  Hardware ID   %s' % m['hardware_id_string'])
        if m.get('hardware_id') is not None:
            print('  Hardware ID#  0x%08X' % m['hardware_id'])
        if m.get('br_version'):
            print('  Bootrom       %s' % m['br_version'])
        if m.get('metrics_version'):
            print('  Metrics ver   %s' % m['metrics_version'])
        if m.get('build_user'):
            print('  Built         %s %s by %s'
                  % (m.get('build_date', '?'), m.get('build_time', '?'),
                     m['build_user']))
        if m.get('supported_options') is not None:
            print('  Options       0x%X   Drivers 0x%X   Processor 0x%X'
                  % (m['supported_options'], m.get('drivers', 0),
                     m.get('processor', 0)))
        if m.get('ldr_blocks') is not None:
            print('  LDR blocks    0x%08X   Bootrom size 0x%X   FlashID 0x%X'
                  % (m['ldr_blocks'], m.get('bootrom_size', 0), m.get('flash_id', 0)))
        print('  (security / HW OS id / BR id are not located in this blob)')
    elif 'metrics_error' in r:
        print('\nBootrom metrics unavailable: %s' % r['metrics_error'])

    bs = r.get('block_size')
    if bs:
        print('\nBlock size:  0x%X (%d B), %d units = %.1f GB'
              % (bs['block_size'], bs['block_size'], bs['units'],
                 bs['approx_bytes'] / 1e9))
    pin = r.get('pin')
    if pin:
        print('PIN:         %s (%s)' % (pin['pin'], pin['raw']))
    bm = r.get('boot_mode')
    if bm:
        print('\nBoot Mode:   %s (Remaining boot count: %d)  status %d (%s)'
              % (bm['mode_name'], bm['factory_count'], bm['status'], bm['status_name']))

    fr = r.get('flash_regions')
    if fr:
        print('\nFlash regions (0xB4, %d bytes): %s' % (fr['size'], ' | '.join(fr['strings'][:6])))
        print('  raw: %s…' % fr['hex'][:64])
    if 'mct' in r:
        print()
        print_mct(r['mct'])

    al = r.get('action_log')
    if al:
        print('\nLoader action log (0xB3, newest first): %d entries, %d bytes'
              % (len(al), r.get('action_log_raw_len', 0)))
        for e in al[:12]:
            print('  %s' % e['text'])
        if len(al) > 12:
            print('  … %d more' % (len(al) - 12))
    elif 'action_log_error' in r:
        print('\nLoader action log unavailable: %s' % r['action_log_error'])

    bug = r.get('bugdisp_log')
    if bug:
        print('\nBugdisp crash log (0xB0 — a different log, encrypted at rest):')
        for line in bug[:4]:
            print('  %s' % line)
        line = '  reply: %d bytes' % r.get('bugdisp_log_len', 0)
        if 'bugdisp_log_entropy' in r:
            line += ', body entropy %.2f bits/byte' % r['bugdisp_log_entropy']
            if r['bugdisp_log_entropy'] > 7.5:
                line += ' -> ciphertext; CFP writes it verbatim to c:\\log.bug'
        print(line)
        if r.get('loader_action_log_hex'):
            print('  raw head: %s…' % r['loader_action_log_hex'])

    probes = r.get('probes') or {}
    if probes:
        print('\nRead-only probes (ops from reports 162/164):')
        for name, e in probes.items():
            if 'error' in e:
                print('  %-22s %s -> error: %s' % (name, e['op'], e['error']))
                continue
            flag = '' if e.get('ok') else '  <- unexpected response'
            print('  %-22s %s -> %s  %5d B%s' % (name, e['op'], e['resp'], e['len'], flag))
            if 'version' in e:
                print('    OSREGION: version %d, region-0 boots %d, region-1 boots %d%s'
                      % (e['version'], e['region0_boots'], e['region1_boots'],
                         '  (deleted)' if e.get('all_zero') else ''))
            elif 'size_mb' in e:
                print('    DRAM: %d MB, vendor %d (%s), revision %s'
                      % (e['size_mb'], e['vendor_id'], e['vendor'], e['revision']))
            elif 'branding_id' in e:
                print('    vendor ids: word0 %d, branding %d' % (e['word0'], e['branding_id']))
            elif 'os_version' in e:
                print('    OS %s, metrics %s, image magic %s'
                      % (e['os_version'], e.get('metrics_version', '?'),
                         e.get('image_magic_hex', '?')))
                print('    address %s   built %s %s by %s'
                      % (e.get('os_address', '?'), e.get('build_date', '?'),
                         e.get('build_time', '?'), e.get('build_user', '?')))
                print('    hardware %s (%s)'
                      % (e.get('hardware_id_string', '?'),
                         '0x%08X' % e['hardware_id'] if 'hardware_id' in e else '?'))
            elif 'decoded' in e:
                items = ['%s=0x%X' % (k, v) for k, v in e['decoded'].items()]
                line = '    '
                for item in items:
                    if len(line) + len(item) + 1 > 76:
                        print(line)
                        line = '    '
                    line += item + ' '
                if line.strip():
                    print(line)
            elif 'hex_head' in e:
                print('    %s…' % e['hex_head'])


# ----------------------------------------------------------------------- flash
def cmd_flash(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py flash',
                                 description='flash a container or a bare component')
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument('--signed', metavar='FILE', help='MFCQ container, sent as-is')
    src.add_argument('--component', metavar='FILE',
                     help='bare .ifs/.rcfs/.mbr/.ufs, wrapped into a container')
    ap.add_argument('--dest-block', type=lambda s: int(s, 0),
                    help='destination 64 KiB block for --component (e.g. 0x825)')
    ap.add_argument('--apply', action='store_true', help='actually write')
    ap.add_argument('--keep-loader', action='store_true')
    ap.add_argument('--no-signature', action='store_true')
    ap.add_argument('--preflash', type=lambda s: int(s, 0))
    ap.add_argument('--verbose', action='store_true')
    _add_device_args(ap)
    args = ap.parse_args(argv)

    if args.component:
        if args.dest_block is None:
            ap.error('--component needs --dest-block (e.g. 0x825 for the region-0 IFS)')
        delta = args.dest_block - REGION0_START
        if delta < 0:
            ap.error('--dest-block must be >= 0x815')
        print('building container: %s -> block 0x%X (Delta %d)'
              % (args.component, args.dest_block, delta))
        container, bi = build([(args.component, delta, 0)], ver=3, sign=True)
        print('container %d bytes, headersz 0x%X' % (len(container), bi['headersz']))
        source = args.component
    else:
        container = open(args.signed, 'rb').read()
        source = args.signed

    payload, sig = split_signature(container)
    _info, runs = plan_container(container)
    print('%s: %d bytes (%d payload, signature %s)'
          % (source, len(container), len(payload),
             '%d B' % len(sig) if sig else 'none'))
    for run in runs:
        print('  write block 0x%05X..0x%05X (raw 0x%08X..0x%08X) offset %d'
              % (run['dest_block'], run['dest_block'] + run['count'] - 1,
                 run['dest_raw'], run['dest_raw'] + run['count'] * 0x10000 - 1,
                 run['offset']))
    bad = gate_runs(runs)
    for run, dest, last in bad:
        print('  GATE REJECT: offset %d -> block 0x%X..0x%X outside every extent'
              % (run['offset'], dest, last))
    if bad:
        raise SystemExit('gate check failed; refusing before USB')
    if not args.apply:
        print('dry run: stopping before USB (pass --apply to write)')
        return 0

    bb = _connect(args)
    try:
        send_container(bb, payload, None if args.no_signature else sig,
                       args.preflash, args.verbose)
    finally:
        _leave(bb, reboot=not args.keep_loader)
    print('done')
    return 0


# -------------------------------------------------------------- reset-osregion
def cmd_reset_osregion(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py reset-osregion',
                                 description='delete NVRAM 0x2074 (CFP RESETQNXOSREGION)')
    ap.add_argument('--apply', action='store_true', help='actually delete')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--keep-loader', action='store_true',
                    help='do not reset the unit out of the ramloader afterwards')
    _add_device_args(ap)
    args = ap.parse_args(argv)

    log = (lambda *a: None) if args.json else print
    result = {}
    bb = _connect(args)
    try:
        cmd, data = bb.channel2(0xE9, struct.pack('<I', 0), timeout=3000)
        before = decode_osregion(data)
        result['before'] = before
        log('NVRAM 0x%04X before: %s' % (OSREGION_RECORD, ' '.join(before['words'])))
        if not args.apply:
            log('dry run: not sending 0xE8/4 (pass --apply to delete the record)')
            result['applied'] = False
        else:
            cmd, data = bb.channel2(0xE8, struct.pack('<I', ACTION_DELETE_OSREGION),
                                    timeout=10000)
            result['status'] = '0x%02X' % cmd
            log('0xE8 arg 4 -> 0x%02X%s' % (cmd, ' (as CFP expects)' if cmd in (6, 0x4006) else
                                            ' (0x2E is sent on BOTH paths — not decisive)'))
            cmd2, data2 = bb.channel2(0xE9, struct.pack('<I', 0), timeout=3000)
            after = decode_osregion(data2)
            result['after'] = after
            log('NVRAM 0x%04X after:  %s' % (OSREGION_RECORD, ' '.join(after['words'])))
            result['applied'] = True
            result['record_changed'] = before['words'] != after['words']
            log('record changed — the delete was accepted' if result['record_changed']
                else 'record UNCHANGED — do not assume recovery')
    finally:
        _leave(bb, reboot=not getattr(args, 'keep_loader', False),
               verbose=not args.json)
    if args.json:
        print(json.dumps(result, indent=2))
    return 0



def _cmd_parse(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py parse',
                                 description='inspect a QCFM (.signed) container')
    ap.add_argument('file')
    ap.add_argument('--json', action='store_true')
    a = ap.parse_args(argv)
    info = parse(open(a.file, 'rb').read())
    if a.json:
        print(json.dumps(info, indent=2))
        return 0
    v1 = info['v1']
    print('%s: %d bytes, payload at 0x%X (%d bytes)'
          % (a.file, info['size'], v1['headersz'], info['payload_size']))
    print('  V1 mfcq: version %d, nheaders %d, v2 offset 0x%X'
          % (v1['version'], v1['nheaders'], v1['v2_offset']))
    for r in v1['records']:
        print('  qcfp: nrecords %d, blocksize 0x%X, flags 0x%X'
              % (r['nrecords'], r['blocksize'], r['flags']))
        for run in r['runs']:
            print('    run offset %d (0x%X) count %d'
                  % (run['offset'], run['offset'], run['count']))
    if info['v2']:
        print('  V2 mfcq at 0x%X: version %s, headersz 0x%X'
              % (info['v2']['offset'], info['v2']['version'], info['v2']['headersz']))
        for r in info['v2']['records']:
            print('  pfcq: type %s, blocksize 0x%X, length %d'
                  % (r['type'], r['blocksize'], r['length']))
            for run in r['runs']:
                print('    run offset %d count %d' % (run['offset'], run['count']))
    sig = info['signature']
    print('  signature: ' + ('none' if not sig else
                             'crc %s %s' % (sig['stored_crc'],
                                            'OK' if sig['crc_ok'] else 'MISMATCH')))
    return 0


def _lst_entries(path):
    entries = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        name, _, params = line.partition('=')
        delta, flags = 0, 0
        if params:
            parts = params.split(',')
            delta = int(parts[0], 0)
            flags = int(parts[1], 0) if len(parts) > 1 else 0
        entries.append((name, delta, flags))
    return entries


def _cmd_pack(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py pack',
                                 description='build a QCFM container (bb10mt pack)')
    ap.add_argument('-l', '--list', required=True, help='file=delta,flags per line')
    ap.add_argument('-c', '--out', required=True)
    ap.add_argument('-v', '--versions', default='1,2',
                    help='1 = V1 qcfp, 2 = V2 pfcq, 1,2 = both (default)')
    ap.add_argument('-s', '--sign', action='store_true',
                    help='append the 560-byte QNXH trailer')
    ap.add_argument('-f', '--fast', action='store_true',
                    help='one run per file instead of the 0xFF block analysis')
    a = ap.parse_args(argv)
    ver = 0
    for v in a.versions.split(','):
        ver |= int(v)
    data, info = build(_lst_entries(a.list), ver=ver, sign=a.sign, fast=a.fast)
    with open(a.out, 'wb') as f:
        f.write(data)
    print('%s: %d bytes, headersz 0x%X, payload %d, signed %s'
          % (a.out, info['size'], info['headersz'], info['payload'], info['signed']))
    return 0


def _cmd_unpack(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py unpack',
                                 description='extract components (bb10mt unpack)')
    ap.add_argument('file')
    ap.add_argument('-o', '--outdir', default='.')
    ap.add_argument('--prefix', help='output name stem')
    ap.add_argument('--only', help='comma-separated component indices')
    a = ap.parse_args(argv)
    only = {int(v) for v in a.only.split(',')} if a.only else None
    out, lst = extract(a.file, a.outdir, a.prefix, only=only)
    if not out:
        raise SystemExit('no components found in %s' % a.file)
    print('lst lines (feed to `bbusb.py pack -l`):')
    for line in lst:
        print('  ' + line)
    return 0


def _cmd_split(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py split',
                                 description='extract an autoloader .exe (bb10mt split)')
    ap.add_argument('file')
    ap.add_argument('-o', '--outdir', default='.')
    a = ap.parse_args(argv)
    hdr, _written = split(a.file, a.outdir)
    print('PE ends at 0x%X; header v%d; %d file(s)'
          % (hdr['pe_end'], hdr['version'], hdr['count']))
    return 0


def cmd_reset_loader(argv):
    ap = argparse.ArgumentParser(prog='bbusb.py reset-loader',
                                 description='send 0x80EF/0xEF to an enumerated '
                                             'ramloader so the unit boots media')
    ap.add_argument('--pid', type=lambda s: int(s, 0), default=0x8001)
    ap.add_argument('--attempts', type=int, default=3)
    a = ap.parse_args(argv)
    for attempt in range(1, a.attempts + 1):
        bb = BBUSB()
        if not bb.open([a.pid], timeout_ms=2000):
            print('no ramloader enumerated')
            return 0
        print('opened ramloader id=0x%x (attempt %d)' % (bb.id, attempt))
        try:
            for op in (0x80EF, 0xEF, 0x80EF):
                try:
                    code, _ = bb.channel2(op, b'', timeout=800)
                    print('  0x%X -> 0x%X' % (op, code))
                except Exception as e:
                    print('  0x%X -> %s' % (op, e))
                time.sleep(0.4)
        finally:
            bb.close()
        time.sleep(1.0)
    return 0


# ----------------------------------------------------------------------- main
def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in ('-h', '--help'):
        print('usage: bbusb.py <command> [options]\n\n'
              'device commands (need the unit):\n'
              '  info                 read-only device info (CFP `info` analogue)\n'
              '  flash                flash a container or a bare component\n'
              '  reset-osregion       delete NVRAM 0x2074 (CFP RESETQNXOSREGION)\n'
              '  boot-mode            read/switch BOOT_MODE and the factory counter\n'
              '  reset-loader         reset a unit left in the ramloader (0fca:8001)\n'
              '\ncontainer commands (no device needed):\n'
              '  parse FILE           inspect a .signed container\n'
              '  pack -l LST -c OUT   build a container (bb10mt `pack`)\n'
              '  unpack FILE -o DIR   extract components (bb10mt `unpack`)\n'
              '  split EXE -o DIR     extract an autoloader (bb10mt `split`)\n'
              '\n`bbusb.py <command> --help` for the options.')
        return 0
    cmd, rest = argv[0], argv[1:]
    if cmd == 'info':
        return cmd_info(rest)
    if cmd == 'flash':
        return cmd_flash(rest)
    if cmd in ('reset-osregion', 'resetqnxosregion'):
        return cmd_reset_osregion(rest)
    if cmd == 'boot-mode':
        return boot_mode_cli(rest)
    if cmd in ('reset-loader', 'force-reset'):
        return cmd_reset_loader(rest)
    if cmd == 'parse':
        return _cmd_parse(rest)
    if cmd == 'pack':
        return _cmd_pack(rest)
    if cmd == 'unpack':
        return _cmd_unpack(rest)
    if cmd == 'split':
        return _cmd_split(rest)
    print('unknown command %r (try --help)' % cmd, file=sys.stderr)
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
