# Synthetic ITCH Exchange Simulator

A **deterministic exchange simulator** that generates realistic market data using queue-reactive stochastic models. The system produces order flow (adds, cancels, executions) from first principles, letting price emerge endogenously from limit order book dynamics.

The long-term goal is to encode these events into **ITCH-like binary messages over UDP**, enabling realistic backtesting and systems testing as if consuming a live exchange feed.

---

## Architecture

The system is built around a **Queue-Reactive, State-Dependent Poisson (QRSDP)** event producer:

1. An intensity model computes event arrival rates from the current book state.
2. Events are sampled via competing Poisson processes (exponential inter-arrival, categorical type selection).
3. Each event mutates the limit order book; price shifts emerge when the best level depletes.
4. All events are recorded to an event sink for downstream processing.

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
  io/            Event sink interface + in-memory sink
  producer/      Producer interface + QRSDP session runner

tests/qrsdp/    Google Test suite (50 tests)
tools/qrsdp_ui/  ImGui + ImPlot real-time debugging UI
docs/            Project docs, model reviews, audit reports
```

---

## Building

### Native (Windows / MSVC)

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Docker

```bash
docker-compose -f docker/docker-compose.yml build build
docker-compose -f docker/docker-compose.yml run --rm test
```

### Build Targets

| Target | Description |
|---|---|
| `qrsdp_cli` | Headless CLI — runs a single session, prints summary |
| `qrsdp_ui` | Real-time debugging UI (ImGui/ImPlot/GLFW) |
| `tests` | Google Test suite |

---

## Quick Start

```bash
# Run a 30-second session with seed 42
./build/Release/qrsdp_cli 42 30

# Launch the debugging UI
./build/tools/qrsdp_ui/Release/qrsdp_ui

# Run tests
./build/Release/tests
```

---

## Documentation

- [Documentation Index](docs/README.md)
- [Build, Test, and Run](docs/build-test-run.md)
- [QRSDP Mechanics](docs/producer/QRSDP_MECHANICS.md)
- [Model Math & Code Review](docs/06_model_math_and_code_review.md)
- [SimpleImbalance Audit](docs/07_simple_imbalance_nonsense_audit.md)

---

## Project Status

The QRSDP producer is functional with two intensity models, a real-time debugging UI, and a comprehensive test suite. Next milestone: event log persistence and replay (dev week).

---

## Disclaimer

This project is for **research and educational purposes only**.
It does not represent or reproduce any specific exchange's proprietary systems.
