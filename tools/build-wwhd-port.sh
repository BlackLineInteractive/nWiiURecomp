#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
read -r root_id _ < <(printf '%s' "$root" | cksum)
root_alias="/tmp/wwhd-recompiled-$UID-$root_id"
if [[ -e "$root_alias" && ! -L "$root_alias" ]]; then
    echo "$root_alias exists and is not a symlink" >&2
    exit 1
fi
ln -sfn "$root" "$root_alias"
trap 'rm -f "$root_alias"' EXIT
game=${1:?usage: tools/build-wwhd-port.sh /absolute/path/to/WWHD}
[[ "$game" = /* ]] || { echo "game path must be absolute" >&2; exit 1; }
[[ -f "$game/code/cking.rpx" ]] || {
    echo "missing $game/code/cking.rpx" >&2
    exit 1
}

build="$root/build-wwhd-port"
build_alias="$root_alias/build-wwhd-port"
generated="$build/generated"
generated_alias="$build_alias/generated"
package="$build/package"
for path in "$build" "$build/tools" "$generated" "$build/recomp" \
    "$build/cemu" "$package"; do
    [[ ! -L "$path" ]] || {
        echo "$path must not be a symlink" >&2
        exit 1
    }
done

"$root/tools/prepare-cemu.sh"
cmake -S "$root_alias" -B "$build_alias/tools" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_alias/tools" -j8 --target nwiiu-recompile
"$build/tools/nWiiURecomp/nwiiu-recompile" \
    "$game/code/cking.rpx" "$generated"
cmake -S "$root_alias" -B "$build_alias/recomp" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNWIIU_GENERATED_PROGRAM="$generated_alias"
cmake --build "$build_alias/recomp" -j8 --target wwhd-module
# vcpkg realpaths its root through the alias back to the spaced repo path,
# which breaks port link lines; keep its working dirs on a space-free path.
vcpkg_cache="$HOME/.cache/wwhd-vcpkg"
mkdir -p "$vcpkg_cache"
export VCPKG_DOWNLOADS="$vcpkg_cache/downloads"
cmake -S "$root_alias/extern/Cemu" -B "$build_alias/cemu" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNWIIU_WWHD_PORT=ON \
    -DNWIIU_RECOMP_INCLUDE="$root_alias/nWiiURecomp/include" \
    -DNWIIU_PORT_ASSET_DIR="$root_alias/media" \
    -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=$vcpkg_cache/buildtrees;--x-packages-root=$vcpkg_cache/packages" \
    -DENABLE_OPENGL=OFF -DENABLE_VULKAN=ON \
    -DENABLE_DISCORD_RPC=OFF -DENABLE_FERAL_GAMEMODE=OFF \
    -DENABLE_BLUEZ=OFF -DENABLE_HIDAPI=OFF -DENABLE_LIBUSB=OFF \
    -DENABLE_SDL=ON -DENABLE_CUBEB=ON
cmake --build "$build_alias/cemu" -j8 --target CemuBin wwhd_port_test

rm -rf "$package"
mkdir -p "$package/THIRD_PARTY_LICENSES"
cp "$build/cemu/bin/wwhd-recompiled" "$package/wwhd-recompiled"
cp "$build/recomp/libwwhd-module.so" "$package/libwwhd-module.so"
cp "$root/media/wwhd-port-icon.png" "$package/wwhd-port-icon.png"
cp "$root/extern/Cemu/LICENSE.txt" \
    "$package/THIRD_PARTY_LICENSES/Cemu-LICENSE.txt"
ln -s "$game" "$package/game"
printf 'Built %s\n' "$package"
