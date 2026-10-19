#!/usr/bin/env bash
# Run the unit tests (JSON blanking logic) on a connected device through adb.
#   ANDROID_NDK_HOME=... tools/test.sh [adb-serial]
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"; : "${ANDROID_NDK_HOME:?}"
CXX="$(ls "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/armv7a-linux-androideabi23-clang++* | head -1)"
S=(); [ $# -ge 1 ] && S=(-s "$1")
for t in test_rego_stream test_remote_strip; do
  "$CXX" -std=c++17 -O1 -Wall -static-libstdc++ "$HERE/src/native/$t.cpp" -o "/tmp/$t"
  adb "${S[@]}" push "/tmp/$t" /data/local/tmp/ >/dev/null
  adb "${S[@]}" shell "chmod 755 /data/local/tmp/$t && /data/local/tmp/$t" | tail -1
done
