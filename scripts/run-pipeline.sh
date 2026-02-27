#!/usr/bin/env bash
# Run the streaming pipeline (Kafka + ITCH) at different speeds.
# Usage: ./scripts/run-pipeline.sh [realtime|10x|100x|max] [up|stop|logs]
#   up   - start pipeline (default)
#   stop - stop all services
#   logs - tail itch-listener logs
#
# On Windows: use WSL to run this script, or run the equivalent
# docker compose commands directly in PowerShell.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPOSE_BASE="$REPO_ROOT/docker/docker-compose.yml"

MODE="${1:-100x}"
SUB="${2:-up}"

case "$MODE" in
  realtime) OVERRIDE="$REPO_ROOT/docker/speed-realtime.yml"; LABEL="1x (6.5h per session)"; ;;
  10x)      OVERRIDE="$REPO_ROOT/docker/speed-10x.yml";     LABEL="10x (~39 min per session)"; ;;
  100x)     OVERRIDE="$REPO_ROOT/docker/speed-100x.yml";    LABEL="100x (~4 min per session)"; ;;
  max)      OVERRIDE="$REPO_ROOT/docker/speed-max.yml";     LABEL="max (no pacing)"; ;;
  *)
    echo "Usage: $0 [realtime|10x|100x|max] [up|stop|logs]"
    echo ""
    echo "Speed modes:"
    echo "  realtime  - 1x speed, 6.5h session = 6.5h wall-clock"
    echo "  10x       - 10x speed, ~39 min per session"
    echo "  100x      - 100x speed, ~4 min per session (default)"
    echo "  max       - no pacing, events as fast as possible"
    exit 1
    ;;
esac

cd "$REPO_ROOT"

case "$SUB" in
  up)
    echo "Starting pipeline at $LABEL..."
    docker compose -f "$COMPOSE_BASE" -f "$OVERRIDE" --profile platform up -d
    echo ""
    echo "Pipeline running. View ITCH feed:"
    echo "  docker compose -f docker/docker-compose.yml -f docker/speed-$MODE.yml --profile platform logs -f itch-listener"
    echo ""
    echo "Stop with: $0 $MODE stop"
    ;;
  stop)
    echo "Stopping pipeline..."
    docker compose -f "$COMPOSE_BASE" -f "$OVERRIDE" --profile platform down
    ;;
  logs)
    docker compose -f "$COMPOSE_BASE" -f "$OVERRIDE" --profile platform logs -f itch-listener
    ;;
  *)
    echo "Unknown subcommand: $SUB (use up, stop, or logs)"
    exit 1
    ;;
esac
