#!/bin/sh
# Durable SSH wrapper for the PlayBook's QNX door.
#
# WHY THIS LIVES HERE AND NOT IN /tmp: /tmp is volatile on this host (a reboot
# wipes it), and every earlier session referred to /tmp/pbssh.sh as if it were
# permanent.  After a host reboot the wrapper was gone, nothing recreated it,
# and the next session had no way to reach the device.  Keep it in the
# workspace; the SSH options below are identical to the ones embedded in
# pb_ring.py and pb_events.py.
#
# Paths are resolved from this script's own location, so a clone works
# anywhere.  PB_KEY overrides the private key (default: local/secrets/rsa).
#
# Usage:  tools/pbssh.sh 'echo OK'
#         tools/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | xxd -p
#
# Requires the door: tools/door-hold.sh running, and TabletOS up (the door
# takes 1-2 min to come back after a TabletOS boot).
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
KEY=${PB_KEY:-$ROOT/local/secrets/rsa}

[ -f "$KEY" ] || {
    echo "pbssh: no SSH key at $KEY" >&2
    echo "pbssh: put the device key there or set PB_KEY" >&2
    exit 1
}

exec ssh -i "$KEY" \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -o IdentitiesOnly=yes \
    -o HostKeyAlgorithms=+ssh-rsa \
    -o PubkeyAcceptedKeyTypes=+ssh-rsa \
    -o PubkeyAcceptedAlgorithms=+ssh-rsa \
    -o KexAlgorithms=+diffie-hellman-group1-sha1 \
    -o MACs=+hmac-sha1,hmac-sha1-96 \
    -o Ciphers=+aes128-cbc,3des-cbc,aes256-cbc \
    -o LogLevel=ERROR \
    -o ConnectTimeout=8 \
    root@169.254.0.1 "$@"
