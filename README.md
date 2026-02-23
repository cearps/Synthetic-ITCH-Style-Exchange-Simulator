# Synthetic ITCH Exchange Simulator

A **deterministic exchange simulator** that generates realistic market data using queue-reactive stochastic models. The system produces order flow (adds, cancels, executions) from first principles, letting price emerge endogenously from limit order book dynamics.

The long-term goal is to encode these events into **ITCH-like binary messages over UDP**, enabling realistic backtesting and systems testing as if consuming a live exchange feed.

---

## Architecture

The system is built around a **Queue-Reactive, State-Dependent Poisson (QRSDP)** event producer:

1. An intensity model computes event arrival rates from the current book state.
2. Events are sampled via competing Poisson processes (exponential inter-arrival, categorical type selection).
3. Each event mutates the limit order book; price shifts emerge when the best level depletes.
4. All events are written to chunked, LZ4-compressed binary event logs (`.qrsdp` format).
5. Python analysis tools read the logs and produce interactive visualisations.

Two intensity models are implemented:
- **Legacy (SimpleImbalance)**: imbalance-driven rates with flat add, linear cancel, and configurable execution baseline.
- **HLR2014 (CurveIntensity)**: queue-size-dependent intensity curves per level, based on Huang, Lehalle & Rosenbaum (2014).

---

## Repository Layout

```
src/
  core/          Event types, records, flag constants
  rng/           RNG interface + Mersenne Twister implementation
  book/          Order book interface + multi-level book
  model/         Intensity models (SimpleImbalance, CurveIntensity, HLR params)
  calibration/   Intensity estimation and curve I/O
  sampler/       Event sampler + attribute sampler
  io/            Event sinks (in-memory, binary file), log reader, format spec
  producer/      Producer interface, QRSDP producer, session runner

notebooks/
  qrsdp_reader.py    Python binary log reader (LZ4 chunk decompression, manifest iteration)
  book_replay.py     Minimal order book replay (mid-price, spread reconstruction)
  ohlc.py            Multi-resolution OHLC bar computation
  01_price_visualisation.ipynb   Zoomable candlestick charts, spread, multi-day overview
  02_stylised_facts.ipynb        Event distributions, returns, autocorrelation
  03_session_summary.ipynb       Per-day and multi-day stats dashboard

tests/qrsdp/    Google Test suite (87 tests)
tools/qrsdp_ui/  ImGui + ImPlot real-time debugging UI
docs/            Project docs, model reviews, audit reports
docker/          Dockerfiles for headless build, test, and notebook environments
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
| `qrsdp_log_info` | Log inspector — prints header, stats, and sample records from a `.qrsdp` file |
| `qrsdp_ui` | Real-time debugging UI (ImGui/ImPlot/GLFW) |
| `tests` | Google Test suite (87 tests) |

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

# Inspect a log file
./build/qrsdp_log_info output/run_42/2026-01-02.qrsdp

# Launch the debugging UI
./build/tools/qrsdp_ui/qrsdp_ui

# Run tests
./build/tests
```

### Python Notebooks

```bash
cd notebooks
python -m venv venv && source venv/bin/activate
pip install -r requirements.txt
jupyter notebook
```

Open any of the three notebooks to explore the generated data interactively. The price visualisation notebook features zoomable candlestick charts that dynamically switch resolution as you zoom in/out.

---

## Documentation

- [Documentation Index](docs/README.md)
- [Build, Test, and Run](docs/build-test-run.md) — CLI usage, Python setup, notebook guide
- [Event Log Format](docs/event-log-format.md) — binary `.qrsdp` file format specification
- [Chunk Size Tuning](docs/chunk-size-tuning.md) — compression vs granularity tradeoff analysis
- [QRSDP Mechanics](docs/producer/QRSDP_MECHANICS.md)
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

The QRSDP producer is fully functional with two intensity models, a real-time debugging UI, chunked LZ4-compressed binary event logs, a multi-day session runner, Python analysis tools with interactive Jupyter notebooks, and a comprehensive test suite (87 tests). Docker support is included for headless builds and testing.

---

## Disclaimer

This project is for **research and educational purposes only**.
It does not represent or reproduce any specific exchange's proprietary systems.
