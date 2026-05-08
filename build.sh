#!/usr/bin/env bash
# Build sdr-server (Release by default, pass "Debug" as first arg for debug build)
set -euo pipefail

BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"

echo "Build type: $BUILD_TYPE"

cmake -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -S .

cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)"

echo ""
echo "Binaries:"
echo "  $BUILD_DIR/sdr_server"
echo "  $BUILD_DIR/sdr_server_client"
