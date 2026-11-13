#!/usr/bin/env bash
# Build and run the registered o3Chords host tests exactly once via CTest.
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
CMAKE="${CMAKE:-$(command -v cmake || true)}"
if [ -z "$CMAKE" ]; then
  echo "cmake not found" >&2
  exit 1
fi
CTEST="${CTEST:-$(command -v ctest || true)}"
if [ -z "$CTEST" ]; then
  CTEST="$(dirname "$CMAKE")/ctest"
fi

"$CMAKE" -B "$BUILD_DIR" -S "$SCRIPT_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-Wno-unused-variable"
"$CMAKE" --build "$BUILD_DIR" -j4
[ "${1:-}" = "--build-only" ] && exit 0
"$CTEST" --test-dir "$BUILD_DIR" --output-on-failure
