# Repository management — the private working tree and the public snapshot

The project uses **two separate git repositories** on the maintainer's machine.
They are deliberately not a branch, subtree or submodule of each other.

| | private working tree | public snapshot |
|---|---|---|
| path | `$HOME/SSDdrive/playbook-linux` | `$HOME/SSDdrive/playbook-linux-public` |
| role | full development history; the source of truth | the curated release snapshot |
| remote | none | `git@github.com:Psyden57/playbook-linux.git` |
| history | the full session-by-session history | an initial release plus public doc commits |
| identity | `Psyden57 <42501054+Psyden57@users.noreply.github.com>` | same |
| contents | every tracked file, plus the gitignored `local/` payload | the tracked tree only |

The private history is **never pushed**.  The public repository is a fresh
snapshot, not a mirror, so private commit hashes, private branches and the
gitignored payload do not appear there.

## What is private-only

These are gitignored or untracked, so `git archive HEAD` from the private tree
already contains exactly the public content:

* `local/` — both cross-toolchains, the QNX/device dumps, the sealed IFS and
  other images, the SSH keys, the vendor tools, the kernel build tree, the
  pristine tarball, backups and logs.
* `.commandcode/` — the agent's learned working notes.
* `tools/qnx6-usage` — the host-built binary.  Its source
  `tools/qnx6-usage.c` is tracked and public; rebuild it locally.
* `bench/*.orig-*` — one-off backups.
* `hardware/*` PDFs and the TRM text extraction.  Only
  `hardware/README.md` is tracked.

The bb10mt source is not in either tree; `tools/bbusb.py` is a GPL-3.0
derivative and is published under GPL-3.0-or-later (see `NOTICE`).
`docs/reports/data/` (device captures) **is** public.

If you add a private-only file, add it to `.gitignore` or do not track it.  A
tracked private file will appear in the next snapshot.

## One-time setup (new machine or session)

1. `git` and `rsync` must be installed.
2. The maintainer's SSH key is `~/.ssh/id_ed25519`; add the public half to
   GitHub (Settings → SSH and GPG keys) and test:
   ```bash
   ssh -T git@github.com      # expect: Hi Psyden57! You've successfully authenticated
   ```
3. Set the commit identity (the repository author):
   ```bash
   git config --global user.name "Psyden57"
   git config --global user.email "42501054+Psyden57@users.noreply.github.com"
   ```
4. The public checkout already has the remote.  If it is recreated:
   ```bash
   git remote add origin git@github.com:Psyden57/playbook-linux.git
   ```
5. `gh` is not required; SSH + git is the supported path.

The public repository's **visibility** is a host setting, not a property of
the tree.  The operator changes it in the repository's
Settings → Danger Zone.  Check it with:
```bash
curl -s -o /dev/null -w '%{http_code}\n' https://github.com/Psyden57/playbook-linux
# 404 = private (or not visible to an anonymous client); 200 = public
```

## Normal work: commit in the private tree

Work and commit in `$HOME/SSDdrive/playbook-linux` as usual.  The private tree
may contain the full history, private notes, `local/` and the bench material.
For any change to the tools or the canonical docs, run:

```bash
python3 -m py_compile tools/*.py
python3 tools/tests/test_offline.py
python3 tools/docs_audit.py
```

`git status` must be clean after the commit.

## Publishing: sync the public snapshot

The public tree is a **checkout of the snapshot**, not a branch.  The supported
update path is the helper:

```bash
# 1. in the private tree, with the work committed
tools/publish_public.sh --dest "$HOME/SSDdrive/playbook-linux-public"
#    dry run: prints the HEAD, the file count, and an itemised rsync diff

# 2. review the diff, then write
tools/publish_public.sh --dest "$HOME/SSDdrive/playbook-linux-public" --apply
```

The script:

* extracts `git archive HEAD` from the private tree;
* rsyncs it into the public checkout with `--delete`, preserving `.git`;
* refuses a destination that is not a git checkout;
* warns if the destination's `origin` is not `Psyden57/playbook-linux`;
* **never commits and never pushes**.

Then, in the public checkout:

```bash
cd "$HOME/SSDdrive/playbook-linux-public"
python3 -m py_compile tools/*.py
python3 tools/tests/test_offline.py
python3 tools/docs_audit.py
git status --short && git diff --stat
git add -A
git commit -m "docs: ..."       # a public-facing summary, not bench chatter
git push origin main
```

Finish with `git ls-remote --heads origin` and, if the repository is public,
the Actions tab (`offline checks`).  The CI workflow runs the three checks
above on every push.

### Keep the snapshot mechanical

The public tree must be the private `HEAD`, not a hand-edited copy.  If the
public wording needs to differ, make the wording true in **both** trees and
commit it privately first; otherwise the next snapshot will overwrite it.  Do
not edit the public tree independently.

If you must hotfix the public tree, copy the same change back to the private
tree in the same turn and commit it there.  A public-only change is a bug in
the process.

## What not to do

* **Do not push the private repository** anywhere.  Do not add the public
  remote to the private repository.
* **Do not `git push --force` the public repository** without the operator's
  explicit approval.  The public history is a release history; a force-push
  rewrites it.  If a private or proprietary file was published, stop, tell the
  operator, and plan the removal with them.
* **Do not commit `local/`, `.commandcode/`, the PDFs/TRM, device images, the
  bb10mt source or `tools/qnx6-usage`.**  They are ignored; keep them that way.
* **Do not recreate files the project has deliberately removed.**  The current
  tree is the intended set.
* **Do not assume the public repo is public**; check it as above.  Changing
  visibility is the operator's browser action.
* **Do not put private bench details in public commit messages.**  The tree is
  public; keep the message about the change.

## If a push is rejected

The remote has commits you do not have.  Do **not** force-push.  Fetch and
inspect first:

```bash
cd "$HOME/SSDdrive/playbook-linux-public"
git fetch origin
git log --oneline --decorate origin/main -5
git diff --stat origin/main..main
```

If the remote is GitHub's initial `LICENSE` commit, layer the snapshot on top
(that is how the first release was made).  If the remote contains someone
else's work, stop and ask the operator.

## Per-session checklist

- [ ] private work committed; tools/docs checks green in the private tree
- [ ] `tools/publish_public.sh` dry run reviewed
- [ ] `--apply` run; checks green in the public tree
- [ ] public commit with a clean message; `git push origin main`
- [ ] `git ls-remote` and the CI run checked
