#!/bin/sh
# Builds the Nintendo Switch client in the Tools/Switch Docker image (devkitA64 + libnx + Clang).
# Usage: Tools/Switch/build.sh [cmake --build arguments, e.g. --target WowNro]
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=wowdecomp-switch

docker image inspect $IMAGE >/dev/null 2>&1 || docker build -t $IMAGE "$ROOT/Tools/Switch"

docker run --rm -v "$ROOT":/src -w /src $IMAGE sh -c '
    set -e
    if [ ! -f build/switch/build.ninja ]; then
        cmake -S . -B build/switch -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/SwitchToolchain.cmake -DCMAKE_BUILD_TYPE=Release
    fi
    cmake --build build/switch "$@"
' sh "$@"
