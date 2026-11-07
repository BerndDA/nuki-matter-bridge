#!/usr/bin/env bash
# Usage: ./build.sh <target> [idf.py args...]     e.g. ./build.sh esp32 menuconfig
# Builds into build/<target>/ (own sdkconfig per target). With BUILD_STORAGE set in
# config.local, build/<target> is a symlink into that directory (e.g. an external drive).
set -e
cd "$(dirname "$0")"
TARGET="${1:?usage: ./build.sh <esp32|...> [idf.py args]}"; shift
source ./env.sh >/dev/null

LINK="build/$TARGET"
mkdir -p build
if [ -n "$BUILD_STORAGE" ] && [ ! -e "$LINK" ]; then
    mkdir -p "$BUILD_STORAGE/nuki_matter-$TARGET"
    ln -s "$BUILD_STORAGE/nuki_matter-$TARGET" "$LINK"
fi
mkdir -p "$LINK"
B="$(cd -P "$LINK" && pwd)"  # real path: CMake remembers the directory it was configured in

if [ ! -f "$B/sdkconfig" ]; then
    idf.py -B "$B" -D SDKCONFIG="$B/sdkconfig" set-target "$TARGET"
fi
idf.py -B "$B" -D SDKCONFIG="$B/sdkconfig" "${@:-build}"
