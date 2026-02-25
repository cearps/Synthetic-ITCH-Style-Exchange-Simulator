# Build, Test, and Run

## Prerequisites

| Requirement | Minimum | Notes |
|---|---|---|
| CMake | 3.15+ | |
| C++17 compiler | MSVC 2017+, GCC 7+, Clang 5+ | |
| Git | any | FetchContent pulls GoogleTest, GLFW, ImGui, ImPlot |
| OpenGL | 3.x+ | Required only for `qrsdp_ui` |

Optional:

- Docker Desktop (for headless Linux builds / CI / streaming platform)
- librdkafka (`apt install librdkafka-dev` or `brew install librdkafka`) — only needed when building with `BUILD_KAFKA_SUPPORT=ON`

## Build Targets

| Target | Description | CMake option |
|---|---|---|
| `simulator_lib` | Static library (all QRSDP components) | always built |
| `qrsdp_cli` | Single-session CLI (quick runs, debugging) | always built |
| `qrsdp_run` | Multi-day session runner (generates datasets) | always built |
| `qrsdp_log_info` | Log file inspector (prints header, stats, samples) | always built |
| `tests` | Google Test suite (98 cases) | `BUILD_TESTING=ON` (default) |
| `qrsdp_ui` | ImGui real-time debugging UI | `BUILD_QRSDP_UI=ON` (default) |

Kafka support is compiled separately and off by default (no librdkafka needed for core development):

| Option | Default | Description |
|---|---|---|
| `BUILD_KAFKA_SUPPORT` | `OFF` | Enable KafkaSink + MultiplexSink (requires librdkafka) |

---

## Building

### Windows (MSVC / Visual Studio)

Visual Studio generators produce multi-config builds. Use `--config Release` (or `Debug`) at build time rather than at configure time.

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

Outputs:

```
build/Release/qrsdp_cli.exe
build/Release/tests.exe
build/tools/qrsdp_ui/Release/qrsdp_ui.exe
```

> **Tip:** If rebuilding fails with `LNK1168: cannot open ... for writing`, close the running executable first.

### macOS (Xcode or Makefiles)

**Xcode generator** (multi-config, default on macOS when Xcode is installed):

```bash
mkdir build && cd build
cmake .. -G Xcode
cmake --build . --config Release
```

**Unix Makefiles** (single-config, lighter weight):

```bash
mkdir build && cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Outputs (Makefiles):

```
build/qrsdp_cli
build/qrsdp_run
build/qrsdp_log_info
build/tests
build/tools/qrsdp_ui/qrsdp_ui
```

Outputs (Xcode):

```
build/Release/qrsdp_cli
build/Release/qrsdp_run
build/Release/qrsdp_log_info
build/Release/tests
build/tools/qrsdp_ui/Release/qrsdp_ui
```

> **Note:** macOS links OpenGL via the system framework. No extra install is needed; CMake's `find_package(OpenGL)` handles it.

### Linux (GCC / Clang)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Outputs:

```
build/qrsdp_cli
build/qrsdp_run
build/qrsdp_log_info
build/tests
build/tools/qrsdp_ui/qrsdp_ui
```

> **Note:** The UI target requires OpenGL dev headers. On Debian/Ubuntu:
> `sudo apt install libgl-dev libglfw3-dev` (GLFW is also fetched by CMake, but the system GL headers are still needed).

### Headless Build (skip the UI)

To build without `qrsdp_ui` (e.g. on a headless server with no GPU):

```bash
cmake .. -DBUILD_QRSDP_UI=OFF
```

### Docker (Linux, headless only)

```bash
# Build the C++ simulator
docker compose -f docker/docker-compose.yml build build

# Run the test suite
docker compose -f docker/docker-compose.yml run --rm test

# Generate 5 trading days of data
docker compose -f docker/docker-compose.yml run --rm simulator

# Launch Jupyter notebooks (accessible at http://localhost:8888)
docker compose -f docker/docker-compose.yml up notebooks
```

The build and test images use GCC on Ubuntu 22.04 with the UI disabled (no display server). The `simulator` service runs `qrsdp_run --seed 42 --days 5` by default, writing to a shared `output` volume. The `notebooks` service provides a Jupyter environment with all Python dependencies pre-installed, mounting the same output volume for analysis.

### Streaming Platform (Kafka + ClickHouse)

The streaming platform runs as a Docker Compose profile. It builds the producer
with Kafka support enabled, streams events through Kafka, and ClickHouse
ingests them natively via its Kafka engine.

```bash
# Start the streaming platform (Kafka + ClickHouse + real-time producer)
docker compose -f docker/docker-compose.yml --profile platform up -d --build

# Check event counts in ClickHouse
curl 'http://localhost:8123/?query=SELECT+count()+FROM+exchange_events'

# Interactive ClickHouse SQL
docker exec -it docker-clickhouse-1 clickhouse-client

# Stop the platform (add -v to wipe data)
docker compose -f docker/docker-compose.yml --profile platform down
```

The platform profile launches three services:
- **kafka** — single-node KRaft broker (no ZooKeeper)
- **clickhouse** — OLAP store with Kafka engine consuming events automatically
- **kafka-producer** — runs with `--realtime --speed 100 --days 0` (continuous streaming, ~4 min per simulated trading day)

Kafka and ClickHouse connection settings are centralised in YAML anchors at the
top of `docker-compose.yml` for easy swap to managed services.

---

## Running

### Single Session — `qrsdp_cli`

Runs a single QRSDP session and prints a one-line summary. Optionally writes a `.qrsdp` event log file.

```
Usage: qrsdp_cli [seed] [seconds] [output.qrsdp]
```

| Arg | Default | Description |
|---|---|---|
| `seed` | 42 | RNG seed (`uint64_t`) |
| `seconds` | 30 | Simulated trading session length |
| `output.qrsdp` | *(none)* | If provided, writes a binary event log file |

```bash
# Quick 30-second run, no file output
./build/qrsdp_cli 42 30

# Write a single-day event log
./build/qrsdp_cli 42 23400 output/day1.qrsdp
```

Example output:

```
seed=42  seconds=30  events=2892  close=9998  shifts=0
wrote output/day1.qrsdp  (1 chunks)
```

### Multi-Day Run — `qrsdp_run`

Runs multiple consecutive trading sessions with continuous price chaining (each day opens at the previous day's close). Writes one `.qrsdp` file per day, a `manifest.json`, and a `performance-results.md` into the output directory.

```
Usage: qrsdp_run [options]
  --seed <n>              Base seed (default: 42)
  --days <n>              Number of trading days; 0 = run indefinitely (default: 5)
  --seconds <n>           Seconds per session (default: 23400)
  --p0 <ticks>            Opening price in ticks (default: 10000)
  --output <dir>          Output directory (default: output/run_<seed>)
  --start-date <str>      First trading date (default: 2026-01-02)
  --chunk-size <n>        Records per chunk (default: 4096)
  --perf-doc <path>       Write performance doc (default: <output>/performance-results.md)
  --depth <n>             Initial depth per level (default: 5)
  --levels <n>            Levels per side (default: 5)
  --securities <spec>     Comma-separated symbol:p0 pairs (e.g. AAPL:10000,MSFT:15000)
  --kafka-brokers <host>  Kafka bootstrap servers (empty = file-only, no Kafka)
  --kafka-topic <name>    Kafka topic name (default: exchange.events)
  --realtime              Pace events to simulated inter-arrival times
  --speed <f>             Speed multiplier for real-time mode (default: 100.0)
  --help                  Show this help
```

```bash
# Generate 5 trading days (6.5 hours each) with defaults
./build/qrsdp_run

# Generate 20 days with a specific seed and output location
./build/qrsdp_run --seed 123 --days 20 --output output/run_123

# Short test run (30 seconds per day, 2 days)
./build/qrsdp_run --seed 42 --days 2 --seconds 30

# Multi-security run (each symbol runs in parallel on its own thread)
./build/qrsdp_run --seed 42 --days 5 --securities "AAPL:10000,MSFT:15000,GOOG:20000"

# Real-time pacing (100x speed — 6.5h session in ~4 min)
./build/qrsdp_run --realtime --speed 100 --days 1 --seconds 23400

# Continuous mode (runs indefinitely until Ctrl-C)
./build/qrsdp_run --realtime --speed 1000 --days 0

# Stream to Kafka (requires BUILD_KAFKA_SUPPORT=ON and a running broker)
./build/qrsdp_run --kafka-brokers localhost:9092 --kafka-topic exchange.events \
    --realtime --speed 100 --days 0 --securities "AAPL:10000,MSFT:15000"
```

Example output (single-security):

```
=== qrsdp_run ===
seed=42  days=5  seconds=23400  p0=10000  output=output/run_42

--- Summary ---
  2026-01-02  seed=42  events=2262506  ratio=2.05x  3960426 ev/s  open=10000 close=9781
  2026-01-05  seed=43  events=2260762  ratio=2.05x  5612140 ev/s  open=9781 close=9953
  ...

Total: 11310302 events in 2.14 s (5276892 ev/s)
Wrote output/run_42/performance-results.md
Wrote output/run_42/manifest.json
```

Output directory structure (single-security):

```
output/run_42/
  manifest.json
  performance-results.md
  2026-01-02.qrsdp
  2026-01-05.qrsdp
  2026-01-06.qrsdp
  2026-01-07.qrsdp
  2026-01-08.qrsdp
```

#### Multi-Security Mode

When `--securities` is provided, each symbol gets its own subdirectory and runs on an independent thread with its own RNG, order book, and intensity model. Seeds are derived as `base_seed + security_index * 1024 + day_index`, ensuring full independence between securities.

Output directory structure (multi-security):

```
output/run_42/
  manifest.json
  performance-results.md
  AAPL/
    2026-01-02.qrsdp
    2026-01-05.qrsdp
  MSFT/
    2026-01-02.qrsdp
    2026-01-05.qrsdp
  GOOG/
    2026-01-02.qrsdp
    2026-01-05.qrsdp
```

The manifest format upgrades from v1.0 (flat `sessions[]`) to v1.1 (nested `securities[].sessions[]`). The Python reader auto-detects the version and provides `iter_securities()` and symbol-filtered `iter_days()` for multi-security runs.

### Log Inspector — `qrsdp_log_info`

Reads a `.qrsdp` binary event log and prints the file header, summary statistics, event type distribution, and sample records.

```
Usage: qrsdp_log_info <file.qrsdp> [num_samples]
```

| Arg | Default | Description |
|---|---|---|
| `file.qrsdp` | *(required)* | Path to a `.qrsdp` event log file |
| `num_samples` | 10 | Number of sample records to print |

```bash
# Inspect a log file
./build/qrsdp_log_info output/run_42/2026-01-02.qrsdp

# Show 20 sample records
./build/qrsdp_log_info output/run_42/2026-01-02.qrsdp 20
```

Example output:

```
=== File Header ===
  version:             1.0
  record_size:         26 bytes
  seed:                42
  p0_ticks:            10000
  session_seconds:     23400
  chunk_capacity:      4096
  has_index:           yes

=== Summary ===
  chunks:              553
  total_records:       2262506
  duration:            23399.996 s
  events/sec:          96.7

=== Event Distribution ===
  ADD_BID            515055  ( 22.8%)
  ADD_ASK            514506  ( 22.7%)
  CANCEL_BID         170963  (  7.6%)
  CANCEL_ASK         170892  (  7.6%)
  EXECUTE_BUY        444760  ( 19.7%)
  EXECUTE_SELL       446330  ( 19.7%)
```

### Debugging UI — `qrsdp_ui`

Real-time visualisation of price, book depth, intensities, drift diagnostics, and event counts. Supports both the Legacy (SimpleImbalance) and HLR2014 (CurveIntensity) models.

```bash
./build/tools/qrsdp_ui/qrsdp_ui
```

> The UI requires a display and GPU with OpenGL 3.x support. It will not run over SSH or in Docker without a display server.

### Tests

```bash
./build/tests
# or via CTest
cd build && ctest --output-on-failure

# Run only session runner tests
./build/tests --gtest_filter='*SessionRunner*:*DateHelper*'
```

---

## Python Notebooks

Interactive analysis notebooks live in the `notebooks/` directory. They read the `.qrsdp` binary event logs produced by the C++ simulator and provide zoomable price charts, statistical analysis, and session dashboards using Plotly.

### Setup

```bash
cd notebooks
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
jupyter notebook
```

### Generating Data

```bash
# Quick test (5 days)
./build/qrsdp_run --seed 42 --days 5 --seconds 23400

# Full year (252 trading days, ~580M events, ~6.8 GB)
./build/qrsdp_run --seed 42 --days 252 --seconds 23400
```

### Notebooks

| Notebook | Description |
|---|---|
| `01_price_visualisation.ipynb` | Zoomable candlestick charts (resolution auto-switches on zoom), bid-ask spread, multi-day overview |
| `02_stylised_facts.ipynb` | Event type distribution, inter-arrival times, return distributions, autocorrelation |
| `03_session_summary.ipynb` | Per-day and multi-day stats dashboard: event counts, prices, compression, shifts |
| `04_multi_security_comparison.ipynb` | Cross-security price paths (raw and normalised), return distributions, spread overlay, aggregate stats |
| `06_live_streaming_query.ipynb` | Real-time queries against live ClickHouse data: event counts, price series, OHLC bars, auto-refreshing dashboard |

### Python Modules

| Module | Description |
|---|---|
| `qrsdp_reader.py` | Binary `.qrsdp` reader: header parsing, LZ4 chunk decompression, manifest iteration (v1.0 + v1.1 multi-security) |
| `book_replay.py` | Minimal order book replay matching C++ shift mechanics, produces mid-price/spread time series |
| `ohlc.py` | Multi-resolution OHLC bar computation (1s, 10s, 1min, 5min) with zoom-level resolution selector |

---

## Source Layout

```
src/
  core/          event_types.h, records.h
  rng/           irng.h, mt19937_rng.h/.cpp
  book/          i_order_book.h, multi_level_book.h/.cpp
  model/         i_intensity_model.h, simple_imbalance_intensity,
                 curve_intensity_model, hlr_params, intensity_curve
  calibration/   intensity_estimator, intensity_curve_io
  sampler/       i_event_sampler.h, i_attribute_sampler.h,
                 competing_intensity_sampler, unit_size_attribute_sampler
  io/            i_event_sink.h, in_memory_sink, binary_file_sink,
                 event_log_reader, event_log_format.h,
                 multiplex_sink, kafka_sink (BUILD_KAFKA_SUPPORT)
  producer/      i_producer.h, qrsdp_producer, session_runner
  main.cpp       Single-session CLI entry point (qrsdp_cli)
  run_main.cpp   Multi-day session runner entry point (qrsdp_run)
  log_info_main.cpp  Log inspector entry point (qrsdp_log_info)

third_party/
  lz4/           Vendored LZ4 compression (BSD licence)

pipeline/
  clickhouse/    init.sql (Kafka engine + MergeTree schema), init.sh (entrypoint)

tests/qrsdp/    test files, 98 test cases
tools/qrsdp_ui/  ImGui + ImPlot real-time debugging UI
```

## Test Categories

- **Records** — packed struct layout, flag constants
- **Interfaces** — pure-virtual compilation checks
- **Book** — seed, apply, shift, cascade, reinitialize
- **Intensity** — SimpleImbalance formula verification
- **CurveIntensity** — HLR2014 per-level curves
- **Calibration** — intensity estimation, curve JSON I/O
- **Sampler** — exponential delta-t, categorical type selection
- **AttributeSampler** — level selection for adds/cancels
- **Producer** — determinism, invariants, shift detection, reinit
- **BinaryFileSink** — header, chunked writes, round-trip, index footer
- **EventLogReader** — header parsing, chunk reads, range queries, scan fallback
- **MultiplexSink** — fanout to multiple sinks, one sink failure doesn't block others
- **SessionRunner** — single-day, continuous chaining, seed strategy, business dates, manifest, multi-security run, seed independence, multi-security manifest, single-security backward compat, real-time pacing, continuous mode
