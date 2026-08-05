#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cemu="$root/extern/Cemu"
patch="$root/patches/cemu/0001-wwhd-recompiled-port.patch"
expected=b8f2cf4b431df7c1669ec926a5ea8b9fc146f310
tmp=$(mktemp -d)
clean="$tmp/cemu"
worktree_added=false

cleanup()
{
    if $worktree_added; then
        git -C "$cemu" worktree remove --force "$clean" >/dev/null 2>&1 || true
    fi
    rm -rf "$tmp"
}
trap cleanup EXIT

[[ $(git -C "$cemu" rev-parse HEAD) == "$expected" ]] || {
    echo "Cemu must be pinned to $expected" >&2
    exit 1
}

if ! git -C "$cemu" diff --cached --quiet HEAD --ignore-submodules=none; then
    echo "Error: Cemu has staged changes in the index" >&2
    exit 1
fi
# A temporary index exposes untracked files without changing the user's index.
GIT_INDEX_FILE="$tmp/candidate.index" git -C "$cemu" read-tree HEAD
GIT_INDEX_FILE="$tmp/candidate.index" git -C "$cemu" add -N -A
GIT_INDEX_FILE="$tmp/candidate.index" git -C "$cemu" diff \
    --binary --ignore-submodules=none HEAD -- > "$tmp/candidate.patch"
[[ -s "$tmp/candidate.patch" ]] || {
    echo "Cemu patch would be empty" >&2
    exit 1
}

git -C "$cemu" worktree add --detach "$clean" "$expected"
worktree_added=true
git -C "$clean" apply "$tmp/candidate.patch"
GIT_INDEX_FILE="$tmp/roundtrip.index" git -C "$clean" read-tree HEAD
GIT_INDEX_FILE="$tmp/roundtrip.index" git -C "$clean" add -N -A
GIT_INDEX_FILE="$tmp/roundtrip.index" git -C "$clean" diff \
    --binary --ignore-submodules=none HEAD -- > "$tmp/roundtrip.patch"
cmp -s "$tmp/candidate.patch" "$tmp/roundtrip.patch" || {
    echo "Cemu patch failed clean-checkout round trip" >&2
    exit 1
}

mkdir -p "$(dirname "$patch")"
cp "$tmp/candidate.patch" "$patch"
