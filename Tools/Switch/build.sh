#!/bin/sh
# Builds the Nintendo Switch client in the Tools/Switch Docker image (devkitA64 + libnx + Clang).
# Usage: Tools/Switch/build.sh [cmake --build arguments, e.g. --target WowNro]
#
# Signature checks are off (modified interface files and patch MPQs load); WOWDECOMP_SIGNATURE_CHECKS=1 turns them on.
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=wowdecomp-switch
if [ "${WOWDECOMP_SIGNATURE_CHECKS:-0}" = 1 ]; then DISABLE_CHECKS=OFF; else DISABLE_CHECKS=ON; fi

docker image inspect $IMAGE >/dev/null 2>&1 || docker build -t $IMAGE "$ROOT/Tools/Switch"

docker run --rm -v "$ROOT":/src -w /src -e DISABLE_CHECKS=$DISABLE_CHECKS $IMAGE sh -c '
    set -e
    # Configured every time (it takes a moment) so the build ID in Logs/Console.txt names the current commit.
    if [ ! -f build/switch/build.ninja ]; then
        cmake -S . -B build/switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/SwitchToolchain.cmake -DCMAKE_BUILD_TYPE=Release \
            -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=$DISABLE_CHECKS
    else
        cmake -S . -B build/switch -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=$DISABLE_CHECKS > /dev/null
    fi
    cmake --build build/switch "$@"
' sh "$@"
