#!/usr/bin/env bash
# Build libpvhook.so (armeabi-v7a) from src/native with the Android NDK + CMake.
#
#   ANDROID_NDK_HOME=/path/to/ndk  tools/build.sh [outdir]
#
# Needs: Android NDK r25+ (tested with r29), cmake >= 3.18 and ninja (both ship with the
# Android SDK "cmake" package). The result is stripped and written to <outdir>/libpvhook.so
# (default: build/).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$HERE/build}"
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to your NDK directory}"
CMAKE="${CMAKE:-cmake}"
NINJA="$(command -v ninja || true)"
GEN=(); [ -n "$NINJA" ] && GEN=(-G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA")
mkdir -p "$OUT"
"$CMAKE" "${GEN[@]}" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-23 \
  -S "$HERE/src/native" -B "$OUT/cmake"
"$CMAKE" --build "$OUT/cmake"
STRIP="$(ls "$ANDROID_NDK_HOME"/toolchains/llvm/prebuilt/*/bin/llvm-strip* | head -1)"
"$STRIP" -o "$OUT/libpvhook.so" "$OUT/cmake/libpvhook.so"
echo "built: $OUT/libpvhook.so"
