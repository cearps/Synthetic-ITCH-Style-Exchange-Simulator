# AGENTS.md

## Cursor Cloud specific instructions

### Overview

This is a **C++17 QRSDP exchange simulator** that generates synthetic market data (ITCH 5.0). The core product is the C++ build; Python notebooks and the Docker streaming platform are optional layers.

### Building (native, headless)

```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_QRSDP_UI=OFF \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_CXX_FLAGS="-Wno-error=unused-result -Wno-error=switch"
cmake --build . -j$(nproc)
```

**GCC 13 compatibility note:** The project targets GCC 11 (Ubuntu 22.04 Docker image). On GCC 13+ (Ubuntu 24.04), two `-Werror` warnings must be downgraded:
- `-Wno-error=unused-result` — test file uses `fread`/`truncate` without checking return values
- `-Wno-error=switch` — test file has unhandled `COUNT` enum in switch statements

The UI target (`qrsdp_ui`) requires OpenGL/GLFW and is skipped on headless VMs (`-DBUILD_QRSDP_UI=OFF`).

### Running tests

```bash
./build/tests --gtest_color=yes
```

All 127 tests should pass. Verbose debug output from book tests (EXECUTE_BUY/SELL messages) is expected and not an error.

### Running the simulator

See `README.md` Quick Start section for CLI usage. Key commands:
- `./build/qrsdp_cli <seed> <seconds> [output.qrsdp]` — single session
- `./build/qrsdp_run --seed 42 --days 5` — multi-day run
- `./build/qrsdp_log_info <file.qrsdp>` — inspect log files

### Python notebooks

```bash
cd notebooks && source venv/bin/activate
```

The venv at `notebooks/venv/` has all dependencies from `notebooks/requirements.txt`. Python reader API: `qrsdp_reader.iter_days()` yields `(date, records)`, `qrsdp_reader.iter_securities()` yields `(symbol, date, records)`.

### Web frontend + API

**API (Python FastAPI):** Wraps the C++ engine. Start with:
```bash
source notebooks/venv/bin/activate
python -m uvicorn api.main:app --host 0.0.0.0 --port 8000
```

**Frontend (React+Vite):** Connects to the API. Start with:
```bash
cd frontend && npm install && npx vite --host 0.0.0.0 --port 5173
```

The Vite dev server proxies `/api` to `localhost:8000`. Both must be running for the full experience. The frontend is at `http://localhost:5173`.

### Streaming platform (optional, Docker)

Kafka + ClickHouse + ITCH streaming runs via Docker Compose `platform` profile. See `docs/build-test-run.md` and `docs/data-platform.md`. Not required for core development.
