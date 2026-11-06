#!/usr/bin/env bash
# Usage: ./build.sh <target> [idf.py args...]     e.g. ./build.sh esp32c3 flash monitor
# One build dir + sdkconfig per target, kept on the external drive.
set -e
cd "$(dirname "$0")"
TARGET="${1:?usage: ./build.sh <esp32|esp32c3|...> [idf.py args]}"; shift
source ./env.sh >/dev/null
B="${BUILD_ROOT:-$ESPRESSIF_ROOT/builds}/nuki_matter-$TARGET"
if [ ! -f "$B/sdkconfig" ]; then
    idf.py -B "$B" -D SDKCONFIG="$B/sdkconfig" set-target "$TARGET"
fi
idf.py -B "$B" -D SDKCONFIG="$B/sdkconfig" "${@:-build}"
