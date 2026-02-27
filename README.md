# Synthetic ITCH Exchange Simulator

A **deterministic exchange simulator** that generates realistic market data using queue-reactive stochastic models. The system produces order flow (adds, cancels, executions) from first principles, letting price emerge endogenously from limit order book dynamics. Events are encoded into **ITCH 5.0 binary messages** framed in MoldUDP64 packets over UDP, and optionally streamed through Kafka into ClickHouse for analytics.

---

## Architecture

The system is built around a **Queue-Reactive, State-Dependent Poisson (QRSDP)** event producer:

1. An intensity model computes event arrival rates from the current book state.
2. Events are sampled via competing Poisson processes (exponential inter-arrival, categorical type selection).
3. Each event mutates the limit order book; price shifts emerge when the best level depletes.
4. All events are written to chunked, LZ4-compressed binary event logs (`.qrsdp` format).
5. Events are encoded into **ITCH 5.0** messages, framed in **MoldUDP64** packets, and sent over **UDP** (unicast or multicast).
6. Optionally, events are published to **Kafka** and ingested into **ClickHouse** for SQL analytics.
7. Python analysis tools read the logs and produce interactive visualisations.

```
QRSDP Producer ──► Event Log (.qrsdp) ──► Python Notebooks
       │
       ├──► Kafka ──► ITCH Encoder ──► MoldUDP64 ──► UDP ──► Listener
       │
       └──► Kafka ──► ClickHouse (SQL analytics)
```

Two intensity models are implemented:
- **Legacy (SimpleImbalance)**: imbalance-driven rates with flat add, linear cancel, and configurable execution baseline.
- **HLR2014 (CurveIntensity)**: queue-size-dependent intensity curves per level, based on Huang, Lehalle & Rosenbaum (2015) [[1]](#references).

---

## Repository Layout

```
src/
  core/          Event types, records, flag constants
  rng/           RNG interface + Mersenne Twister implementation
  book/          Order book interface + multi-level book
  model/         Intensity models (SimpleImbalance, CurveIntensity, HLR params)
  calibration/   Intensity estimation, curve I/O, and calibration CLI
  sampler/       Event sampler + attribute sampler
  io/            Event sinks (in-memory, binary file, multiplex), log reader
  producer/      Producer interface, QRSDP producer, session runner
  itch/          ITCH 5.0 encoder, MoldUDP64 framer, UDP sender, decoder

tests/           Google Test suite (127 tests across 17 files)
  core/ io/ book/ model/ calibration/ sampler/ producer/ itch/

notebooks/
  qrsdp_reader.py    Python binary log reader (LZ4 chunk decompression, manifest iteration)
  book_replay.py     Minimal order book replay (mid-price, spread reconstruction)
  ohlc.py            Multi-resolution OHLC bar computation
  01_price_visualisation.ipynb   Zoomable candlestick charts, spread, multi-day overview
  02_stylised_facts.ipynb        Event distributions, returns, autocorrelation
  03_session_summary.ipynb       Per-day and multi-day stats dashboard
  04_multi_security_comparison.ipynb  Cross-security price paths, returns, and spread comparison
  05_model_comparison.ipynb           Side-by-side SimpleImbalance vs HLR with calibration

tools/qrsdp_ui/  ImGui + ImPlot real-time debugging UI
pipeline/        ClickHouse init scripts and schema definitions
docs/            Project docs, model reviews, audit reports
docker/          Dockerfiles and docker-compose for build, test, and platform stack
```

---

## Building

### Native (macOS / Linux / Windows)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

### Docker

```bash
docker-compose -f docker/docker-compose.yml build build
docker-compose -f docker/docker-compose.yml run --rm test
docker-compose -f docker/docker-compose.yml run --rm simulator
```

### Build Targets

| Target | Description |
|---|---|
| `qrsdp_cli` | Single-session CLI — runs one session, optionally writes a `.qrsdp` log |
| `qrsdp_run` | Multi-day session runner — generates datasets with continuous price chaining |
| `qrsdp_calibrate` | Calibration CLI — estimates HLR intensity curves from `.qrsdp` event logs |
| `qrsdp_log_info` | Log inspector — prints header, stats, and sample records from a `.qrsdp` file |
| `qrsdp_itch_stream` | ITCH stream consumer — reads Kafka, encodes ITCH 5.0 over UDP |
| `qrsdp_listen` | Reference ITCH listener — receives UDP, decodes and prints ITCH messages |
| `qrsdp_ui` | Real-time debugging UI (ImGui/ImPlot/GLFW) |
| `tests` | Google Test suite (127 tests across 17 files) |

---

## Quick Start

```bash
# Run a single 30-second session
./build/qrsdp_cli 42 30

# Write a single-day event log
./build/qrsdp_cli 42 23400 output/day1.qrsdp

# Generate 5 trading days (6.5 hours each)
./build/qrsdp_run --seed 42 --days 5

# Generate a full year (252 trading days, ~580M events, ~6.8 GB)
./build/qrsdp_run --seed 42 --days 252 --seconds 23400

# Multi-security run (3 symbols, parallel execution)
./build/qrsdp_run --seed 42 --days 5 --securities "AAPL:10000,MSFT:15000,GOOG:20000"

# Run with HLR2014 model (queue-size-dependent curves)
./build/qrsdp_run --seed 42 --days 5 --model hlr

# Calibrate HLR curves from existing data, then run with them
./build/qrsdp_calibrate --input output/run_42/*.qrsdp --output hlr_curves.json
./build/qrsdp_run --seed 100 --days 5 --hlr-curves hlr_curves.json

# Inspect a log file
./build/qrsdp_log_info output/run_42/2026-01-02.qrsdp

# Launch the debugging UI
./build/tools/qrsdp_ui/qrsdp_ui

# Run tests
./build/tests
```

### ITCH Streaming (Docker)

```bash
# Start the full platform stack (Kafka, ClickHouse, ITCH stream, listener)
./scripts/run-pipeline.sh 100x up    # 100x speed (~4 min per session)
# Or: realtime (1x), 10x, max (no pacing)

# Watch decoded ITCH messages in real time
./scripts/run-pipeline.sh 100x logs

# Or use docker compose directly (single commands, no scripts)
docker compose -f docker/docker-compose.yml -f docker/speed-realtime.yml --profile platform up -d   # 1x
docker compose -f docker/docker-compose.yml -f docker/speed-10x.yml --profile platform up -d      # 10x
docker compose -f docker/docker-compose.yml -f docker/speed-100x.yml --profile platform up -d      # 100x
docker compose -f docker/docker-compose.yml -f docker/speed-max.yml --profile platform up -d       # max
docker compose -f docker/docker-compose.yml --profile platform logs -f itch-listener

# Run the listener locally against a bare-metal multicast group
./build/qrsdp_listen --multicast-group 239.1.1.1 --port 5001
```

### Python Notebooks

```bash
cd notebooks
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
jupyter notebook
```

Open any of the five notebooks to explore the generated data interactively. The price visualisation notebook features zoomable candlestick charts that dynamically switch resolution as you zoom in/out.

---

## Documentation

- [Documentation Index](docs/README.md)
- [Build, Test, and Run](docs/build-test-run.md) — CLI usage, Python setup, notebook guide
- [Event Log Format](docs/event-log-format.md) — binary `.qrsdp` file format specification
- [ITCH Streaming Architecture](docs/itch/ITCH_STREAMING.md) — ITCH 5.0 over UDP, unicast/multicast, Docker setup
- [Data Platform](docs/data-platform.md) — Kafka, ClickHouse Kafka engine, Docker runbook
- [QRSDP Mechanics](docs/producer/QRSDP_MECHANICS.md) — method-level producer breakdown
- [HLR Calibration Pipeline](docs/producer/QRSDP_CALIBRATION.md) — calibration CLI, intensity estimation, curve I/O
- [Performance Results](docs/performance-results.md) — throughput benchmarks and compression metrics
- [Model Math & Code Review](docs/06_model_math_and_code_review.md)
- [SimpleImbalance Audit](docs/07_simple_imbalance_nonsense_audit.md)

---

## Key Results

The simulator reproduces several stylised facts of real financial markets:

- **Near-perfect exponential inter-arrival times** (mean/std ratio = 0.996)
- **Fat-tailed returns** at short horizons (excess kurtosis = 0.78 at 1s)
- **Aggregational Gaussianity** (returns converge to normal at longer horizons)
- **Symmetric event generation** (bid/ask balance within 0.1%)

Performance: ~5M events/s write throughput, 2.05x LZ4 compression ratio, ~27 MB per 6.5-hour trading day.

---

## Project Status

The simulator is feature-complete across event generation, persistence, ITCH streaming, and analytics. Completed milestones:

- **QRSDP event producer** with two intensity models (SimpleImbalance and HLR2014 queue-reactive curves)
- **HLR calibration pipeline** for fitting intensity curves from recorded event data
- **Chunked LZ4-compressed binary event logs** (`.qrsdp` format) with indexed random access
- **Multi-day session runner** with multi-security parallel execution and continuous price chaining
- **ITCH 5.0 streaming pipeline**: encoder, MoldUDP64 framer, UDP sender (unicast and multicast), reference listener
- **Data platform**: Kafka dual-write, ClickHouse Kafka engine for SQL analytics
- **5 interactive Jupyter notebooks** (price visualisation, stylised facts, session summary, multi-security comparison, model comparison)
- **Real-time debugging UI** (ImGui/ImPlot)
- **127 tests across 17 files** including unit, integration, and E2E pipeline traceability tests
- **Docker support** for headless builds, testing, simulation, and the full streaming platform stack

---

## References

1. W. Huang, C.-A. Lehalle, and M. Rosenbaum, "Simulating and Analyzing Order Book Data: The Queue-Reactive Model," *Journal of the American Statistical Association*, vol. 110, no. 509, pp. 107–122, 2015. [arXiv:1312.0563](https://arxiv.org/abs/1312.0563)

2. K. Jain, N. Firoozye, J. Kochems, and P. Treleaven, "Limit Order Book Simulations: A Review," *arXiv preprint*, 2024. [arXiv:2402.17359](https://arxiv.org/abs/2402.17359)

---

## Disclaimer

This project is for **research and educational purposes only**.
It does not represent or reproduce any specific exchange's proprietary systems.
