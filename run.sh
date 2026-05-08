#!/usr/bin/env bash
# Start the rtl_tcp-compatible server for MSi SDR.
# Phone apps (SDR++, sdrtouch, gqrx, HDSDR) connect directly using rtl_tcp.
#
# Usage: ./run.sh [-p port] [-d device_index] [-g gain_dB] [-r sample_rate] [-f center_freq]
set -euo pipefail

BINARY="build/rtltcp_server"

if [ ! -f "$BINARY" ]; then
  echo "Binary not found — run ./build.sh first"
  exit 1
fi

exec "$BINARY" "$@"
