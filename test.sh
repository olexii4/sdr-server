#!/usr/bin/env bash
# Build in Debug mode and run the test suite
set -euo pipefail

./build.sh Debug

cd build
ctest --output-on-failure
