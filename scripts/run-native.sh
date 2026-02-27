#!/usr/bin/env bash
# Run the streaming pipeline natively (no Docker).
# Requires: Kafka at localhost:9092, build/ with qrsdp_run, qrsdp_itch_stream, qrsdp_listen.
#
# Usage: ./scripts/run-native.sh [realtime|10x|100x|max]
#
# Opens listener and stream in background, runs producer in foreground.
# Ctrl+C stops all. For separate terminals, see docs/itch/ITCH_STREAMING.md.
#
# On Windows: use WSL to run this script, or run the equivalent
# commands directly in PowerShell.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD="$REPO_ROOT/build"

MODE="${1:-100x}"

case "$MODE" in
  realtime) REALTIME="--realtime"; SPEED="--speed 1"; ;;
  10x)      REALTIME="--realtime"; SPEED="--speed 10"; ;;
  100x)     REALTIME="--realtime"; SPEED="--speed 100"; ;;
  max)      REALTIME=""; SPEED=""; ;;
  *)
    echo "Usage: $0 [realtime|10x|100x|max]"
    echo ""
    echo "Speed modes:"
    echo "  realtime  - 1x speed"
    echo "  10x       - 10x speed"
    echo "  100x      - 100x speed (default)"
    echo "  max       - no pacing"
    exit 1
    ;;
esac

if [[ ! -x "$BUILD/qrsdp_run" ]] || [[ ! -x "$BUILD/qrsdp_itch_stream" ]] || [[ ! -x "$BUILD/qrsdp_listen" ]]; then
  echo "Error: build binaries not found. Run: mkdir build && cd build && cmake .. && cmake --build ."
  exit 1
fi

LISTENER_PID=""
STREAM_PID=""

cleanup() {
  [[ -n "$STREAM_PID" ]] && kill "$STREAM_PID" 2>/dev/null || true
  [[ -n "$LISTENER_PID" ]] && kill "$LISTENER_PID" 2>/dev/null || true
}
trap cleanup EXIT SIGINT SIGTERM

echo "Starting listener on port 5001..."
"$BUILD/qrsdp_listen" --port 5001 --no-multicast &
LISTENER_PID=$!

sleep 1

echo "Starting ITCH stream (Kafka -> UDP unicast)..."
"$BUILD/qrsdp_itch_stream" --kafka-brokers localhost:9092 --unicast-dest 127.0.0.1:5001 &
STREAM_PID=$!

sleep 1

echo "Starting producer (Kafka + $MODE)..."
"$BUILD/qrsdp_run" \
  --seed 42 \
  --days 0 \
  --seconds 23400 \
  --output "$REPO_ROOT/output/native_run_42" \
  --kafka-brokers localhost:9092 \
  --kafka-topic exchange.events \
  --securities "AAPL:10000,MSFT:15000,GOOG:12000" \
  --market-open 09:30 \
  $REALTIME $SPEED
