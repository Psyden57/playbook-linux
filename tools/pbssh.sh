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
# anywhere.  PB_KEY overrides the private key (default: local/secrets/rsa);
# PB_USER overrides the login (default: root).  **Root works on 2.1.0.1917**
# (verified session 38: `uid:0 euid:0`), but only after the door-hold handshake
# has uploaded the key: a cold boot's first attempts get
# "Permission denied (publickey,password)" until `door-hold.sh` has connected.
# Do not switch to devuser to work around that — wait for the hold.
#
# Usage:  tools/pbssh.sh 'echo OK'
#         PB_USER=devuser tools/pbssh.sh 'uname -a'
#         tools/pbssh.sh 'dd if=/dev/rpmb/BOOTCTR_FCTRY bs=4 count=1 2>/dev/null' | xxd -p
#         PB_LINK=usb tools/pbssh.sh 'uname -a'      # dropbear on our Linux image
#
# TWO different ssh servers share the address 169.254.0.1, and their
# capabilities differ, so the legacy options are only offered when they are
# needed:
#
#   PB_LINK=door (default) -- the QNX door on TabletOS.  A 2014 sshd that only
#       speaks the old algorithms, so the ssh-rsa/dh-group1/cbc options below
#       are required.
#   PB_LINK=usb -- de-dropbear on our Linux image, reached over the RNDIS link
#       (session 48).  It negotiates modern algorithms and prefers ed25519, so
#       passing the legacy options would only weaken it.  It is also the only
#       path that exists when the unit runs our image at all -- there is no
#       door there (trap 88).
#
# `usb` is the default when *nothing answers on the door's qconn port* -- the
# door exists on TabletOS and our image has none (trap 88).  Probing the
# service is right where probing the interface is not: the door on TabletOS is
# reached over the very same RNDIS link, so the interface alone cannot tell the
# two apart.  PB_LINK overrides.
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
KEY=${PB_KEY:-$ROOT/local/secrets/rsa}
PB_HOST=${PB_HOST:-169.254.0.1}
PB_USER=${PB_USER:-root}

[ -f "$KEY" ] || {
    echo "pbssh: no SSH key at $KEY" >&2
    echo "pbssh: put the device key there or set PB_KEY" >&2
    exit 1
}

if [ -z "$PB_LINK" ]; then
    if timeout 3 sh -c "exec 3<>/dev/tcp/$PB_HOST/9090" 2>/dev/null; then
        PB_LINK=door
    else
        PB_LINK=usb
    fi
fi

if [ "$PB_LINK" = usb ]; then
    exec ssh -i "$KEY" \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o IdentitiesOnly=yes \
        -o LogLevel=ERROR \
        -o ConnectTimeout=8 \
        "$PB_USER@$PB_HOST" "$@"
fi

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
    "$PB_USER@$PB_HOST" "$@"
