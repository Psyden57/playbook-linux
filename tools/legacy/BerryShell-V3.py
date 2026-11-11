#!/usr/bin/env python3

##
## DISCLAIMER: This was made using AI (Specifically Google's Gemini 3, have not paid a dime for it's use though, used for free from the AI Studio)
##

import socket
import struct
import hashlib
import binascii
import os
import sys
import time
import getpass
import subprocess
import argparse
from cryptography.hazmat.primitives.asymmetric import rsa, padding
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

# Config
ID_RSA_FILE = "rsa"
ID_RSA_PUB_FILE = "rsa.pub"
DEFAULT_IP = "169.254.0.1"
DEFAULT_USER = "root"
DEFAULT_PASSWORD = "0000"
#

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
            print(f"[!] Warning: Could not set permissions on {keyfile}: {e}")

def generate_keys_if_missing():
    if not os.path.exists(ID_RSA_FILE) or not os.path.exists(ID_RSA_PUB_FILE):
        print("[*] Generating new RSA keypair...")
        
        key = rsa.generate_private_key(public_exponent=3, key_size=4096)
        
        with open(ID_RSA_FILE, "wb") as f:
            f.write(key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=serialization.NoEncryption()
            ))
        
        pub = key.public_key()
        with open(ID_RSA_PUB_FILE, "wb") as f:
            f.write(pub.public_bytes(
                encoding=serialization.Encoding.OpenSSH,
                format=serialization.PublicFormat.OpenSSH
            ))
        
    fix_key_permissions(ID_RSA_FILE)

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
    def __init__(self, ip, password):
        self.ip = ip
        self.password = password
        self.sock = None
        self.handshake_key = rsa.generate_private_key(public_exponent=65537, key_size=1024)
        
        with open(ID_RSA_PUB_FILE, "rb") as f:
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
        print(f"[*] Looking for QCONN door on {self.ip}:4455...")
        
        # Infinite retry loop to wait for device to boot/become reachable
        while True:
            try:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.settimeout(5) # Shorter timeout for the retries
                self.sock.connect((self.ip, 4455))
                self.sock.settimeout(10) # Restore normal timeout for handshake
                break # Success, exit retry loop
            except OSError as e:
                # Use \r to overwrite the same line instead of spamming the console
                sys.stdout.write(f"\r[-] Unreachable. Retrying in 3s... ({e})".ljust(80))
                sys.stdout.flush()
                try:
                    time.sleep(3)
                except KeyboardInterrupt:
                    print("\n[*] Cancelled by user.")
                    sys.exit(0)

        print("\n[*] Connected! Authenticating...")
        
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
                        print("[+] Authenticated. Key uploaded.")

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
        except BrokenPipeError:
            print("[!] Connection lost sending heartbeat.")
            raise

    def close(self):
        try:
            if self.sock:
                self.sock.close()
            print("[*] QCONN Door closed.")
        except: pass

def main():
    # Setup Argument Parser
    parser = argparse.ArgumentParser(description="Python BerryShell for BlackBerry PlayBook and BB10 devices")
    parser.add_argument("--direct", action="store_true", help="Skip interactive prompts and use provided args or defaults")
    parser.add_argument("--ip", type=str, default=DEFAULT_IP, help=f"Device IP Address (default: {DEFAULT_IP})")
    parser.add_argument("--user", type=str, default=DEFAULT_USER, help=f"SSH User (default: {DEFAULT_USER})")
    parser.add_argument("--password", type=str, default=DEFAULT_PASSWORD, help="Device PIN/Password")
    
    args = parser.parse_args()

    print("\n--- Python BerryShell ---\n")

    # Load from arguments (defaults apply if not specified)
    target_ip = args.ip
    target_user = args.user
    target_password = args.password

    # Only prompt if --direct wasn't passed
    if not args.direct:
        ip_input = input(f"Device IP [{target_ip}]: ").strip()
        if ip_input: 
            target_ip = ip_input
        
        user_input = input(f"SSH User [{target_user}]: ").strip()
        if user_input: 
            target_user = user_input
        
        # Format the password prompt so they know if a custom one is queued up from flags
        pwd_prompt = f"Device PIN/Password [{target_password}]: " if args.password != DEFAULT_PASSWORD else "Device PIN/Password: "
        password_input = input(pwd_prompt).strip()
        if password_input: 
            target_password = password_input

    generate_keys_if_missing()

    door = QCONNDoor(target_ip, target_password)
    
    try:
        door.conduct_handshake()
        
        print("[*] Preparing SSH...")
        time.sleep(2) 

        ssh_cmd = [
            "ssh",
            "-i", ID_RSA_FILE,
            "-tt",
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
            f"{target_user}@{target_ip}"
        ]

        print(f"[*] Launching Shell for {target_user}...")
        print("-" * 40)
        
        proc = subprocess.Popen(ssh_cmd)

        try:
            while proc.poll() is None:
                time.sleep(4)
                door.send_heartbeat()
        except KeyboardInterrupt:
            proc.terminate()
        
        print("-" * 40)
        print("[*] SSH Session ended.")

    except Exception as e:
        print(f"[!] Error: {e}")
    finally:
        door.close()

if __name__ == "__main__":
    main()