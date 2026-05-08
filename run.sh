#!/usr/bin/env bash
# Run sdr-server and print connection info for local network clients.
# Usage: ./run.sh [path/to/config.conf]
set -euo pipefail

CONFIG="${1:-msisdr.conf}"
BINARY="build/sdr_server"

if [ ! -f "$BINARY" ]; then
  echo "Binary not found — run ./build.sh first"
  exit 1
fi

# Resolve local IP and port from config for display
LOCAL_IP=$(ipconfig getifaddr en0 2>/dev/null \
           || ifconfig en0 2>/dev/null | awk '/inet /{print $2}' \
           || echo "<your-mac-ip>")
PORT=$(grep -E '^\s*port\s*=' "$CONFIG" 2>/dev/null | tail -1 | grep -o '[0-9]*' || echo 8090)

echo "============================================"
echo "  sdr-server starting"
echo "  Config : $CONFIG"
echo "============================================"
echo ""
echo "  Connect from phone / any device on the same WiFi:"
echo ""
echo "    sdrtouch / SDR++:  $LOCAL_IP:$PORT"
echo ""
echo "  Request IQ stream (example: FM band, 48 kHz):"
echo "    ./build/sdr_server_client $LOCAL_IP $PORT 100000000 48000 100000000"
echo ""
echo "============================================"
echo ""

exec "$BINARY" "$CONFIG"
