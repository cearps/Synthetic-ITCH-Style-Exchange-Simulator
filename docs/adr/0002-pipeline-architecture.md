# ADR 0002: Pipeline Architecture

## Status

Accepted

## Context

Issue #4 requires architecting the overall pipeline. The system needs to generate deterministic ITCH-like market data over UDP with the event log as the source of truth. The architecture should support:

1. Event production (Add/Cancel/Aggressive Take)
2. Matching engine with price-time priority order book
3. Deterministic, append-only event logging
4. ITCH encoding and UDP streaming
5. Potential for swapping components (e.g., different event producers)

Constraints:

- Must be deterministic and replayable
- Windows primary dev environment
- C++ implementation
- Event log is authoritative (ground truth)
- Low coupling to enable future component swapping

## Decision

We will implement a monolithic C++ codebase with clearly defined interfaces for each major component, enabling logical separation while keeping everything in one system for now.

The pipeline will consist of:

1. **Event Producer** - Interface with QR-SDP v0 implementation

   - Generates order flow events (Add, Cancel, Aggressive Take)
   - Interface allows swapping producers without changing downstream

2. **Matching Engine + Limit Order Book (LOB)** - Interface with price-time priority implementation

   - Processes events and updates order book state
   - Produces deterministic book updates

3. **Deterministic Event Log** - Interface with append-only, seed-driven implementation

   - Records all events in deterministic order
   - Serves as ground truth for replay and verification
   - Seed-driven for reproducibility

4. **ITCH Encoder** - Interface for binary message encoding

   - Encodes events into ITCH-style binary format
   - Works from event log stream

5. **UDP Streamer** - Interface for network delivery
   - Streams encoded messages over UDP
   - Can simulate network conditions (packet loss, latency)

Components communicate through well-defined interfaces, keeping coupling low. The event log sits at the center, allowing both real-time streaming (Encoder → UDP Streamer) and offline replay paths.

## Alternatives considered

- Fully modular with separate processes/services
  - Rejected: Adds complexity (IPC, serialization) premature for early stage. Can evolve later.
- Tightly coupled monolithic classes

  - Rejected: Makes future swapping difficult. Interfaces provide clear boundaries without overhead.

- Event-driven architecture with message queues
  - Rejected: Adds indirection complexity. Direct interfaces simpler for current needs.

## Consequences

- **Pros:**

  - Clear component boundaries via interfaces
  - Easy to test components in isolation
  - Straightforward to swap implementations later
  - Single codebase simplifies development and deployment
  - Determinism maintained through event log

- **Cons:**

  - All components compile together (can be refactored to libraries later if needed)
  - Runtime swapping requires recompilation (acceptable for current use case)

- **Follow-on work:**
  - Implement concrete classes behind interfaces
  - Add seed-based deterministic event generation
  - Implement price-time priority matching
  - Design event log format (binary, deterministic encoding)
  - Implement ITCH message encoding
  - Add UDP streaming with configurable network conditions

## Implementation Details

### Class Structure

All components are defined as C++ interfaces with concrete implementations:

**Core Types** (`src/core/`)

- `events.h`: EventType, OrderSide, OrderType, OrderId, Symbol, Price, Quantity, Timestamp, OrderEvent, TradeEvent, BookUpdateEvent
- `order.h`: Order class with fill tracking

**Event Producer** (`src/producer/event_producer.h`)

- `IEventProducer` interface with `QRSDPEventProducer` implementation
- Seed-based deterministic event generation

**Matching Engine** (`src/matching/`)

- `IOrderBook` interface with `LimitOrderBook` implementation (price-time priority)
- `IMatchingEngine` interface with `PriceTimeMatchingEngine` implementation

**Event Log** (`src/logging/event_log.h`)

- `IEventLog` interface with `DeterministicEventLog` implementation
- Append-only, seed-driven storage

**ITCH Encoder** (`src/encoding/itch_encoder.h`)

- `IITCHEncoder` interface with `ITCHEncoder` implementation
- Binary message encoding/decoding

**UDP Streamer** (`src/streaming/udp_streamer.h`)

- `IUDPStreamer` interface with `UDPStreamer` implementation
- Configurable packet loss and latency simulation

**Exchange Simulator** (`src/exchange_simulator.h`)

- Main orchestrator wiring components together

### Data Flow

```
Event Producer → Matching Engine → Event Log → ITCH Encoder → UDP Streamer
                              ↓
                        (Book Updates)
```

All events flow through the event log as the source of truth, enabling both real-time streaming and offline replay.

### File Organization

```
src/
├── core/           # Core event types and data structures
├── producer/       # Event generation
├── matching/       # Order book and matching engine
├── logging/        # Event logging
├── encoding/       # ITCH encoding
├── streaming/      # UDP streaming
└── exchange_simulator.h  # Main orchestrator
```

## Notes / References

- Issue #4: Architect the overall pipeline
- Event log as source of truth (from project goal)
- QR-SDP v0 mentioned as initial event producer strategy
