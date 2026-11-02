#!/bin/sh
# Durable SCP wrapper for the PlayBook's QNX door.
#
# Same legacy SSH options as tools/pbssh.sh (the device's sshd only speaks
# ssh-rsa / diffie-hellman-group1-sha1 / aes*-cbc), with the address fixed and
# the device side written as ':/path' instead of 'root@169.254.0.1:/path' - the
# address is the part people mistype, and a typo sends the copy to the wrong
# host.
#
#   tools/pbscp.sh local/file.bin :/tmp/          push
#   tools/pbscp.sh :/tmp/file.bin ./              pull
#   tools/pbscp.sh a.bin b.bin :/tmp/             several sources
#   tools/pbscp.sh -O -r somedir :/accounts/devuser/   directory (see below)
#
# DIRECTORIES NEED -O.  OpenSSH's default (SFTP) backend refuses to upload a
# directory whose destination does not already exist ("path canonicalization
# failed"), and on this image /tmp is a symlink to /dev/shmem, which cannot
# create directories at all (`mkdir /tmp/x` -> ENOENT).  The legacy protocol
# works because the device does have scp:
#   tools/pbscp.sh -O -r somedir :/accounts/devuser/
# For single files the default backend is fine, including into /tmp.
#
# Env: PB_KEY (default local/secrets/rsa), PB_HOST (169.254.0.1),
#      PB_USER (root), PB_SCP_OPTS (extra options, word-split).
#
# Requires the door: tools/door-hold.sh running and TabletOS up (the door takes
# 1-2 min to come back after a TabletOS boot).
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
KEY=${PB_KEY:-$ROOT/local/secrets/rsa}
PB_HOST=${PB_HOST:-169.254.0.1}
PB_USER=${PB_USER:-root}

[ -f "$KEY" ] || {
    echo "pbscp: no SSH key at $KEY" >&2
    echo "pbscp: put the device key there or set PB_KEY" >&2
    exit 1
}

if [ $# -lt 2 ]; then
    echo "usage: pbscp.sh <src> [<src>...] <dst>   (a ':' prefix means the device)" >&2
    echo "  push: pbscp.sh local.bin :/tmp/" >&2
    echo "  pull: pbscp.sh :/tmp/x ./" >&2
    exit 2
fi

# Rewrite every ':path' operand as 'user@host:path'; pass everything else
# through untouched (so -r and friends still work).
n=$#
i=0
device=0
while [ $i -lt $n ]; do
    a=$1
    shift
    case $a in
    :*)
        a="$PB_USER@$PB_HOST:${a#:}"
        device=1
        ;;
    esac
    set -- "$@" "$a"
    i=$((i + 1))
done

[ "$device" = 1 ] || {
    echo "pbscp: no ':' operand - refusing a host-to-host copy" >&2
    echo "pbscp: write the device side as ':/path'" >&2
    exit 2
}

exec scp -i "$KEY" \
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
    $PB_SCP_OPTS "$@"
