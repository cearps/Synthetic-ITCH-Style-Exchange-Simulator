# Synthetic ITCH Exchange Simulator

This project is a **deterministic exchange simulator** designed to generate **ITCH-like binary market data over UDP**, enabling realistic backtesting and systems testing _as if consuming a live exchange feed_.

The simulator focuses on **market microstructure realism**, **wire-level fidelity**, and **replayability**, while remaining modular enough to evolve from simple models to more sophisticated ones without breaking downstream consumers.

---

## Project Motivation

Most backtests operate on reconstructed books or aggregated data, missing important aspects of real trading systems:

- on-the-wire message ordering,
- binary decoding,
- partial packet loss,
- and the operational realities of market data ingestion.

This project aims to close that gap by simulating an exchange that behaves like a real one from the perspective of a market data consumer.

---

## High-Level Approach

The system is built around a single core principle:

> **The event log is the source of truth.**

Market behaviour is simulated upstream, recorded deterministically, and then encoded and streamed downstream.

At a high level:

1. A market simulator generates order flow.
2. A matching engine processes events and updates the order book.
3. All events are written to a deterministic, append-only event log.
4. Events are encoded into ITCH-style binary messages.
5. Messages are streamed over UDP.
6. Consumers decode the feed and rebuild the order book exactly as they would in production.

Because the event log is authoritative, the simulation engine can be replaced or upgraded without changing the wire protocol or consumers.

---

## Design Goals

- **Deterministic & replayable**: identical seeds produce identical market data
- **Wire-level realism**: binary messages, sequencing, and UDP transport
- **Modular simulation**: order-flow models can evolve independently
- **Consumer-first**: strategies and ingestion pipelines should not know they are consuming synthetic data
- **Testability**: strong invariants and replay-based verification

---

## Intended Use Cases

- Strategy backtesting under realistic feed conditions
- Market data ingestion testing
- Exchange microstructure research
- Systems-level simulation (latency, packet loss, recovery paths)
- Experimentation with alternative market models

---

## Project Status

This repository is in **early development**.  
The initial focus is on correctness, determinism, and a minimal but realistic exchange lifecycle before increasing model complexity.

---

## Disclaimer

This project is for **research and educational purposes only**.  
It does not represent or reproduce any specific exchange’s proprietary systems.

---

## Getting Started

### Building and Running

The project uses **CMake** for builds and **Docker** for containerized development (recommended for consistency).

**Quick Start with Docker:**

```bash
# Build the project
docker-compose -f docker/docker-compose.yml build build

# Build and run tests (first build downloads Google Test - may take a few minutes)
docker-compose -f docker/docker-compose.yml build test
docker-compose -f docker/docker-compose.yml run --rm test

# Run the simulator
docker-compose -f docker/docker-compose.yml run --rm simulator
```

**Native Build (Windows):**

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

For detailed build, test, and run instructions, see [Build, Test, and Run Plan](docs/build-test-run.md).

---

## Documentation

- [Documentation Index](docs/README.md)
- [Build, Test, and Run Plan](docs/build-test-run.md) - Comprehensive guide for building, testing, and running the simulator (essential for AI agents and developers)
- [Architecture Decisions](docs/adr/README.md) - Architecture Decision Records (ADRs)
