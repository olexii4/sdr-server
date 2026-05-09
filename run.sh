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

# Extract the port from the args without consuming them
PORT=1234
args=("$@")
i=0
while [ $i -lt ${#args[@]} ]; do
  if [ "${args[$i]}" = "-p" ]; then
    i=$(( i + 1 ))
    PORT="${args[$i]:-1234}"
  fi
  i=$(( i + 1 ))
done

# Kill any existing server on this port so we never hit EADDRINUSE
EXISTING=$(lsof -ti TCP:"$PORT" 2>/dev/null | tr '\n' ' ' | xargs || true)
if [ -n "$EXISTING" ]; then
  echo "Stopping existing server on port $PORT (PIDs: $EXISTING)…"
  # shellcheck disable=SC2086
  kill $EXISTING 2>/dev/null || true
  sleep 4  # macOS needs time to fully release USB device/endpoint state
fi

exec "$BINARY" "$@"
