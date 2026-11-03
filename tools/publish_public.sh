#!/bin/sh
# publish_public.sh — sync a clean snapshot of the private HEAD into the public
# checkout.  Dry run by default; --apply writes.  Never commits, never pushes.
#
# The public tree is a separate git repository (not a branch of this one).
# This script extracts `git archive HEAD` and rsyncs it over the public
# checkout with --delete, preserving the public `.git`.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="$ROOT/../playbook-linux-public"
APPLY=0

usage() {
    cat <<'EOF'
usage: tools/publish_public.sh [--dest DIR] [--apply]

Build a snapshot of the current private HEAD (git archive) and sync it into
the public checkout.  The public .git directory is preserved; files that no
longer exist in the snapshot are deleted.

  --dest DIR   public checkout (default: ../playbook-linux-public)
  --apply      write the snapshot; without it, print the rsync itemised diff
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --dest)
            [ $# -ge 2 ] || { usage >&2; exit 2; }
            DEST=$2
            shift 2
            ;;
        --apply)
            APPLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "publish_public: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

command -v rsync >/dev/null 2>&1 || {
    echo "publish_public: rsync is required" >&2
    exit 1
}
[ -d "$ROOT/.git" ] || {
    echo "publish_public: not a git checkout: $ROOT" >&2
    exit 1
}
[ -d "$DEST/.git" ] || {
    echo "publish_public: not a git checkout: $DEST" >&2
    exit 1
}
DEST=$(CDPATH= cd -- "$DEST" && pwd)

REMOTE=$(git -C "$DEST" remote get-url origin 2>/dev/null || true)
case "$REMOTE" in
    *Psyden57/playbook-linux*) ;;
    *) echo "publish_public: warning: origin is '$REMOTE', expected Psyden57/playbook-linux" >&2 ;;
esac

TMP=$(mktemp -d "${TMPDIR:-/tmp}/pb-public.XXXXXX")
trap 'rm -rf "$TMP"' EXIT INT HUP TERM
git -C "$ROOT" archive HEAD | tar -x -C "$TMP"

echo "snapshot:  $(git -C "$ROOT" rev-parse --short HEAD) ($(find "$TMP" -type f | wc -l) files)"
echo "private:   $ROOT"
echo "public:    $DEST"
echo "origin:    ${REMOTE:-<none>}"

if [ "$APPLY" -ne 1 ]; then
    echo
    echo "dry run — files that would change (empty means already in sync):"
    # --checksum: compare content, not the archive's timestamps.  Filter the
    # timestamp-only itemise lines; build caches are not part of the snapshot.
    rsync -an --checksum --delete \
        --exclude '.git' --exclude '__pycache__' --exclude '*.pyc' \
        --itemize-changes "$TMP/" "$DEST/" | grep -v -E '^(\.d|\.f\.\.t)' || true
    echo
    echo "re-run with --apply to write"
    exit 0
fi

rsync -a --checksum --delete \
    --exclude '.git' --exclude '__pycache__' --exclude '*.pyc' \
    "$TMP/" "$DEST/"
echo
echo "synced. Review, check and commit in the public checkout:"
echo "  cd \"$DEST\""
echo "  python3 -m py_compile tools/*.py"
echo "  python3 tools/tests/test_offline.py"
echo "  python3 tools/docs_audit.py"
echo "  git status --short && git diff --stat"
echo "  git add -A && git commit -m \"docs: ...\" && git push origin main"
