#!/usr/bin/env bash
# Start the full development stack: C++ build, API server, and frontend.
#
# Usage: ./scripts/start-dev.sh
#
# Prerequisites:
#   - CMake 3.15+, GCC/Clang, Python 3.12+
#   - Node.js 18+ and npm
#
# Ctrl+C stops all services.

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

API_PID=""
FE_PID=""

cleanup() {
  echo ""
  echo "Shutting down..."
  [[ -n "$API_PID" ]] && kill "$API_PID" 2>/dev/null || true
  [[ -n "$FE_PID" ]] && kill "$FE_PID" 2>/dev/null || true
  wait 2>/dev/null
  echo "Done."
}
trap cleanup EXIT SIGINT SIGTERM

echo "=== Step 1: Build C++ engine ==="
cd "$REPO_ROOT"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_QRSDP_UI=OFF \
  -DCMAKE_CXX_FLAGS="-Wno-error=unused-result -Wno-error=switch" 2>&1 | tail -3
cmake --build . -j"$(nproc)" 2>&1 | tail -5
echo "Build complete."

echo ""
echo "=== Step 2: Set up Python environment ==="
cd "$REPO_ROOT/notebooks"
if [ ! -d venv ]; then
  python3 -m venv venv
fi
source venv/bin/activate
pip install -q -r requirements.txt
pip install -q fastapi uvicorn[standard] websockets
echo "Python environment ready."

echo ""
echo "=== Step 3: Install frontend dependencies ==="
cd "$REPO_ROOT/frontend"
npm install --silent 2>&1 | tail -3
echo "Frontend dependencies installed."

echo ""
echo "=== Step 4: Start API server (port 8000) ==="
cd "$REPO_ROOT"
source notebooks/venv/bin/activate
python -m uvicorn api.main:app --host 0.0.0.0 --port 8000 &
API_PID=$!
sleep 2
echo "API server running (PID $API_PID)."

echo ""
echo "=== Step 5: Start frontend dev server (port 5173) ==="
cd "$REPO_ROOT/frontend"
npx vite --host 0.0.0.0 --port 5173 &
FE_PID=$!
sleep 2

echo ""
echo "============================================="
echo "  QRSDP Exchange Simulator — Development"
echo "============================================="
echo "  Frontend:  http://localhost:5173"
echo "  API:       http://localhost:8000"
echo "  API docs:  http://localhost:8000/docs"
echo "============================================="
echo "  Press Ctrl+C to stop all services."
echo ""

wait
