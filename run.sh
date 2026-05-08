#!/usr/bin/env bash
# Run sdr-server with an optional config file argument
# Usage: ./run.sh [path/to/config.conf]
set -euo pipefail

CONFIG="${1:-src/resources/config.conf}"
BINARY="build/sdr_server"

if [ ! -f "$BINARY" ]; then
  echo "Binary not found — run ./build.sh first"
  exit 1
fi

exec "$BINARY" "$CONFIG"
