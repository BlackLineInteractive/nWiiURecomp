#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cemu="$root/extern/Cemu"
patch="$root/patches/cemu/0001-wwhd-recompiled-port.patch"
expected=b8f2cf4b431df7c1669ec926a5ea8b9fc146f310
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

[[ -d "$cemu" ]] || { echo "Cemu submodule is missing" >&2; exit 1; }
[[ $(git -C "$cemu" rev-parse HEAD) == "$expected" ]] || {
    echo "Cemu must be pinned to $expected" >&2
    exit 1
}

if ! git -C "$cemu" diff --cached --quiet HEAD --ignore-submodules=none; then
    echo "Error: Cemu has staged changes in the index" >&2
    exit 1
fi
GIT_INDEX_FILE="$tmp/current.index" git -C "$cemu" read-tree HEAD
GIT_INDEX_FILE="$tmp/current.index" git -C "$cemu" add -N -A
GIT_INDEX_FILE="$tmp/current.index" git -C "$cemu" diff \
    --binary --ignore-submodules=none HEAD -- > "$tmp/current.patch"

if cmp -s "$patch" "$tmp/current.patch"; then
    git -C "$cemu" submodule update --init --recursive
    exit 0
fi

[[ ! -s "$tmp/current.patch" ]] || {
    echo "Cemu has changes not described by the tracked patch" >&2
    exit 1
}

git -C "$cemu" submodule update --init --recursive

git -C "$cemu" apply --check "$patch"
git -C "$cemu" apply "$patch"
