# Dev Week 2 Plan — ITCH Streaming and Model Improvements

**Duration:** 4 days (Monday–Thursday)
**Objective:** Encode simulated events into NASDAQ ITCH 5.0 binary messages streamed over UDP multicast, build a historical replay server, and add self-exciting (Hawkes) intensity dynamics and correlated multi-security support.

---

## What Already Exists

The following was delivered in Week 1 and is complete, tested, and committed:

| Component | Status |
|---|---|
| QRSDP event producer (continuous-time competing-risk loop) | Done |
| Two intensity models (SimpleImbalance, HLR2014 CurveIntensity) | Done |
| Multi-level order book with shift mechanics and spread preservation | Done |
| Deterministic RNG (mt19937_64), seeded sessions | Done |
| `IEventSink` interface with `InMemorySink` and `BinaryFileSink` implementations | Done |
| Chunked LZ4-compressed binary event logs (`.qrsdp` format) | Done |
| `EventLogReader` with sequential and random-access reads | Done |
| Multi-day session runner with continuous price chaining | Done |
| Multi-security parallel execution (one thread per symbol, 2D seed derivation) | Done |
| Manifest v1.0 (single-security) and v1.1 (multi-security) formats | Done |
| Python binary log reader with lazy chunk iteration and manifest support | Done |
| 4 Jupyter notebooks (price visualisation, stylised facts, session summary, multi-security comparison) | Done |
| Google Test suite (91 tests across 12 files) | Done |
| Docker support (build, test, notebooks) | Done |
| Headless CLI (`qrsdp_cli`), multi-day runner (`qrsdp_run`), log inspector (`qrsdp_log_info`) | Done |

**Key architectural fact:** the producer writes to `IEventSink::append(const EventRecord&)`. A new `ItchFeedSink` implementing this interface is all that's needed to stream events over the network — the producer code does not need to change.

---

## What Needs Building

1. **ITCH message encoder** — translate `EventRecord` to NASDAQ ITCH 5.0 binary messages
2. **MoldUDP64 session layer** — frame ITCH messages into sequenced UDP packets
3. **UDP multicast transport** — send packets to a multicast group for downstream consumers
4. **Live feed binary** — `qrsdp_feed`: run the simulator and stream ITCH over UDP in real time
5. **Replay server** — `qrsdp_replay`: replay historical `.qrsdp` files as ITCH over UDP at configurable speed
6. **Reference consumer** — `qrsdp_listen`: receive, decode, and print ITCH messages (for testing)
7. **Hawkes self-exciting model** — volatility clustering via exponential-decay self-excitation
8. **Correlated multi-security** — shared market factor driving correlated drift across securities
9. **HLR model wiring** — plug the existing `CurveIntensityModel` into `SessionRunner`

**Out of scope this week:** matching engine, external trading agents, ITCH retransmission/recovery protocol, GUI updates.

---

## Daily Plan

### Day 6 (Monday) — ITCH Message Encoder + MoldUDP64 Framing

**Goal:** Build the encoding layer that translates internal `EventRecord` structs into NASDAQ ITCH 5.0 binary messages, wrapped in MoldUDP64 session-layer packets.

**Tasks:**

1. **Define ITCH message structs** (`src/itch/itch_messages.h`)
   - `MessageType` enum and packed big-endian structs for the 5-message subset:

   | Message | Type byte | Size | Purpose |
   |---|---|---|---|
   | System Event | `S` | 12 bytes | Session start/end markers |
   | Stock Directory | `R` | 39 bytes | Symbol, tick size, price decimals (sent once per security at session start) |
   | Add Order | `A` | 36 bytes | New order: reference number, side (B/S), shares, stock, price |
   | Order Delete | `D` | 19 bytes | Full cancellation: reference number |
   | Order Executed | `E` | 31 bytes | Trade: reference number, shares, match number |

   - All fields big-endian, `#pragma pack(push, 1)`, matching the public NASDAQ ITCH 5.0 specification

2. **Implement `ItchEncoder`** (`src/itch/itch_encoder.h/.cpp`)
   - `ItchEncoder(const std::string& symbol, uint32_t tick_size)`
   - `encode(const EventRecord&) → std::vector<uint8_t>`: translates event type + fields to the appropriate ITCH message
   - Maintains state: monotonic match number counter for executions, symbol string for stock field
   - Field mapping:

   | EventRecord field | ITCH field |
   |---|---|
   | `ts_ns` | Timestamp (nanoseconds from midnight) |
   | `type` (ADD_BID/ADD_ASK) | Add Order, buy/sell indicator `B`/`S` |
   | `type` (CANCEL_BID/CANCEL_ASK) | Order Delete |
   | `type` (EXECUTE_BUY/EXECUTE_SELL) | Order Executed |
   | `order_id` | Order Reference Number (64-bit) |
   | `price_ticks * tick_size` | Price (4-decimal fixed point, e.g. 10000 = $1.0000) |
   | `qty` | Shares |
   | symbol (from constructor) | Stock (8-char right-padded with spaces) |

3. **Implement MoldUDP64 framing** (`src/itch/moldudp64.h/.cpp`)
   - `MoldUDP64Framer(const std::string& session_id)`: 10-char session ID, 64-bit sequence number
   - `addMessage(const uint8_t* data, uint16_t len)`: accumulate messages into current packet
   - `flush() → std::vector<uint8_t>`: emit a complete MoldUDP64 packet (20-byte header + message blocks)
   - Auto-flush when accumulated size approaches MTU (~1400 bytes to leave room for IP/UDP headers)

4. **Tests** (`tests/qrsdp/test_itch_encoder.cpp`)
   - Encode each event type, verify message size and type byte
   - Verify big-endian field ordering (compare first few bytes against hand-calculated values)
   - Verify match number increments on executions
   - MoldUDP64: verify header layout, sequence number progression, message count, MTU splitting

**End of day:** Can encode any `EventRecord` into a spec-compliant ITCH message and frame it in a MoldUDP64 packet.

---

### Day 7 (Tuesday) — UDP Multicast Transport + Replay Server

**Goal:** Send ITCH-encoded events over the network and build both a live feed and a historical replay server.

**Tasks:**

1. **UDP multicast sender** (`src/itch/udp_sender.h/.cpp`)
   - `UdpMulticastSender(const std::string& group, uint16_t port, uint8_t ttl = 1)`
   - POSIX sockets: `socket(AF_INET, SOCK_DGRAM)`, `setsockopt(IP_MULTICAST_TTL)`, `sendto()`
   - `send(const uint8_t* data, size_t len)` — fire-and-forget UDP
   - Cross-platform: POSIX on macOS/Linux, Winsock on Windows

2. **ITCH feed sink** (`src/itch/itch_feed_sink.h/.cpp`)
   - `ItchFeedSink : public IEventSink`
   - Composes `ItchEncoder` + `MoldUDP64Framer` + `UdpMulticastSender`
   - On `append(const EventRecord&)`: encode → frame → send when packet is full or on explicit flush
   - Emits System Event (S) and Stock Directory (R) at session start before any order messages

3. **Live feed binary** (`src/feed_main.cpp` → `qrsdp_feed`)
   - Runs the QRSDP producer and streams ITCH over UDP multicast
   - CLI flags:
     ```
     --multicast-group <ip>   Multicast group address (default: 239.1.1.1)
     --port <n>               UDP port (default: 5001)
     --seed <n>               Base seed (default: 42)
     --seconds <n>            Session length (default: 23400)
     --securities <spec>      Comma-separated symbol:p0 pairs
     ```
   - For multi-security: each security gets its own `ItchFeedSink` on a separate port (base port + index)

4. **Replay server** (`src/replay_main.cpp` → `qrsdp_replay`)
   - Reads `.qrsdp` files from a run directory (via manifest) and replays as ITCH over UDP
   - CLI flags:
     ```
     --input <dir>            Run directory with manifest.json
     --speed <factor>         Playback speed: 1.0 = real-time, 10.0 = 10x, 0 = max throughput
     --multicast-group <ip>   Multicast group (default: 239.1.1.1)
     --port <n>               UDP port (default: 5001)
     ```
   - Multi-security: interleaves events from all symbols in timestamp order using a priority queue
   - Pacing: `std::this_thread::sleep_for()` based on inter-event time gaps scaled by `--speed`

5. **Reference consumer** (`src/listen_main.cpp` → `qrsdp_listen`)
   - Joins multicast group, receives MoldUDP64 packets, decodes ITCH messages
   - Prints human-readable output: `[seq=1] ADD_ORDER ref=42 side=B shares=1 stock=AAPL price=100.0000`
   - Useful for testing and demos

6. **Tests**
   - `tests/qrsdp/test_moldudp64.cpp` — framing correctness, sequence numbers, MTU splitting
   - `tests/qrsdp/test_udp_roundtrip.cpp` — localhost loopback: send a MoldUDP64 packet to `127.0.0.1`, receive it, decode and verify ITCH messages match originals

**End of day:** Can run `qrsdp_feed --securities AAPL:10000` in one terminal and `qrsdp_listen` in another, seeing a stream of decoded ITCH messages. Can also replay historical runs with `qrsdp_replay`.

---

### Day 8 (Wednesday) — Hawkes Self-Exciting Intensity Model

**Goal:** Add a self-exciting (Hawkes) process where each event temporarily boosts future arrival rates, producing realistic volatility clustering — bursts of high activity followed by calm periods.

**Tasks:**

1. **Implement `HawkesIntensity`** (`src/model/hawkes_intensity.h/.cpp`)
   - `HawkesIntensity : public IIntensityModel`
   - Decorator pattern: wraps an inner `IIntensityModel` (e.g. `SimpleImbalanceIntensity`) and adds excitation
   - Kernel: `α · exp(−β · (t − tᵢ))` for each past event `tᵢ`
   - Efficient implementation: maintains a running excitation sum `E(t)`. On each event at time `t`:
     ```
     E(t) = E(t_prev) · exp(−β · (t − t_prev)) + α
     ```
     This avoids re-summing the full event history (O(1) per event instead of O(N))
   - Total intensity = base model intensity × (1 + min(E(t), max_excitation))
   - The cap `max_excitation` prevents intensity explosion during burst periods

2. **Define `HawkesParams`** (in `src/core/records.h`)
   ```cpp
   struct HawkesParams {
       double alpha = 0.0;           // excitation magnitude per event
       double beta = 1.0;            // exponential decay rate (1/s)
       double max_excitation = 5.0;  // cap on multiplicative excitation
   };
   ```

3. **Wire into `SessionRunner`**
   - Add `--model` CLI flag to `qrsdp_run`: `simple` (default, current behaviour), `hawkes`, `hlr`
   - When `--model hawkes`: wrap `SimpleImbalanceIntensity` in `HawkesIntensity`
   - When `--model hlr`: use `CurveIntensityModel` (see Day 9)
   - Add `--hawkes-alpha`, `--hawkes-beta`, `--hawkes-cap` CLI flags with sensible defaults
   - Update `runSecurityDays()` in `session_runner.cpp` to accept a model selector and instantiate accordingly

4. **Tests** (`tests/qrsdp/test_hawkes_intensity.cpp`)
   - Verify excitation decays: after a burst of events, check that excitation decreases exponentially over time
   - Verify cap: with high alpha, excitation should not exceed `max_excitation`
   - Verify decorator: with `alpha=0`, output should exactly match the base model
   - Deterministic step-by-step: manually compute expected intensities for a known event sequence and compare

5. **Notebook** (`notebooks/05_hawkes_comparison.ipynb`)
   - Generate two datasets: `--model simple` and `--model hawkes --hawkes-alpha 0.5 --hawkes-beta 2.0`
   - Compare autocorrelation of absolute returns (Hawkes should show positive autocorrelation; Simple should not)
   - Overlay inter-arrival time distributions (Hawkes should have a heavier tail of short inter-arrivals)
   - Event rate over time (Hawkes should show visible clustering; Simple should be more uniform)

**End of day:** Running `qrsdp_run --model hawkes --days 5` produces data with visible volatility clustering that is absent in the default model.

---

### Day 9 (Thursday) — Correlated Securities + HLR Wiring + Polish

**Goal:** Add cross-security correlation, wire the existing HLR curve model into the session runner, and polish documentation.

**Tasks:**

1. **Correlated multi-security via shared market factor** (`src/model/market_factor.h/.cpp`)
   - `MarketFactor`: samples a common Brownian increment `dW ~ N(0, dt)` shared across all securities
   - Each security has a `factor_loading` parameter (e.g. 0.5) that scales its exposure to the common factor
   - Implementation: the factor adjusts the `imbalance` field in `BookFeatures` before it's passed to the intensity model, creating correlated price movements without coupling order books
   - Add `--factor-loading <f>` CLI flag (default 0.0 = fully independent, as today)
   - In `SessionRunner::run()`, create a single `MarketFactor` instance shared (read-only per time step) across security threads via a synchronisation barrier or pre-sampled factor path

2. **Wire `CurveIntensityModel` into `SessionRunner`**
   - Currently `runSecurityDays()` only instantiates `SimpleImbalanceIntensity`
   - When `--model hlr`: instantiate `CurveIntensityModel` with `HLRParams`
   - Add `--hlr-curves <dir>` flag: load per-level intensity curves from JSON files (produced by `IntensityCurveIO`)
   - If no curves directory is provided, use built-in default curves (flat rates matching the simple model's aggregate rates)
   - This completes the calibration pipeline: estimate curves from data → save as JSON → load and simulate

3. **Documentation updates**
   - Update `README.md` — add ITCH streaming section, new binaries (`qrsdp_feed`, `qrsdp_replay`, `qrsdp_listen`), model selection (`--model`)
   - Update `docs/build-test-run.md` — CLI usage for all new binaries, model flags, replay examples
   - Update `docs/README.md` — add links to this plan doc

4. **Notebook for model comparison** (`notebooks/05_hawkes_comparison.ipynb` or extend existing)
   - Side-by-side comparison: Simple vs Hawkes vs HLR
   - Metrics: return autocorrelation, inter-arrival distribution, event rate clustering, price path character

5. **Final checks**
   - Run full test suite (should be ~100+ tests)
   - Clean up any temporary files
   - Tag release: `v0.4.0`

**End of day:** All three intensity models are accessible via CLI, multi-security runs can be correlated, and ITCH streaming works end-to-end.

---

## Technical Decisions

| Decision | Options | Recommendation |
|---|---|---|
| ITCH version | ITCH 5.0 (NASDAQ) vs ITCH 4.1 (legacy) | 5.0 — current spec, widely documented |
| Transport | Raw UDP vs MoldUDP64 framing | MoldUDP64 — standard session layer for ITCH, adds sequence numbers for gap detection |
| Multicast vs unicast | `239.x.x.x` multicast vs point-to-point | Multicast — matches real exchange architecture, multiple consumers can join |
| Endianness | Network byte order (big-endian) | Required by ITCH spec; use `htonl`/`htons` or manual byte packing |
| Hawkes kernel | Exponential vs power-law decay | Exponential — analytically tractable, efficient O(1) update, standard in financial literature |
| Hawkes composition | Additive vs multiplicative excitation | Multiplicative (`base × (1 + E)`) — preserves relative rate structure of the base model |
| Cross-security correlation | Shared factor vs correlated RNG streams | Shared factor — cleaner, controllable loading, doesn't require modifying the RNG layer |
| HLR curve source | Built-in defaults vs require JSON files | Built-in defaults with optional JSON override — works out of the box |

---

## Dependencies to Add

| Library | Purpose | Integration |
|---|---|---|
| POSIX sockets / Winsock | UDP multicast send/receive | System headers, no external dependency |

No new third-party libraries are required. ITCH encoding is hand-rolled (packed structs + endian conversion). UDP uses platform sockets.

---

## Success Criteria (End of Week)

- [ ] `qrsdp_feed --securities AAPL:10000` streams ITCH 5.0 messages over UDP multicast
- [ ] `qrsdp_listen` successfully decodes and prints all message types from a live feed
- [ ] `qrsdp_replay --input output/run_42 --speed 10` replays historical data at 10x speed
- [ ] MoldUDP64 packets have correct sequence numbers and message counts
- [ ] `--model hawkes` produces data with measurable volatility clustering (positive autocorrelation of absolute returns)
- [ ] `--model hlr` uses `CurveIntensityModel` and produces events with per-level queue-size-dependent rates
- [ ] Multi-security runs with `--factor-loading 0.5` show visible price correlation between symbols
- [ ] All existing tests still pass; new tests cover ITCH encoding, MoldUDP64 framing, Hawkes model, and UDP round-trip
- [ ] Documentation reflects all new capabilities

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| Cross-platform socket API differences (POSIX vs Winsock) | Abstract behind a thin `UdpSocket` wrapper; test on macOS/Linux first, Winsock is structurally similar |
| Multicast not working on all networks (corporate firewalls, Docker) | Support `--unicast` fallback flag; localhost testing works regardless |
| Hawkes model instability (runaway excitation) | Hard cap via `max_excitation`; default parameters chosen conservatively |
| MoldUDP64 packet loss (UDP is unreliable) | Out of scope for Week 2; note in docs that retransmission (MoldUDP64 retransmit request protocol) is a future enhancement |
| HLR curves not calibrated to realistic data | Provide built-in defaults that produce reasonable behaviour; calibration from real data is a future task |
| Running out of time on Day 9 polish | Days 6–8 are self-contained deliverables; Day 9 features are enhancements, not blockers |

---

## New Build Targets

| Target | Day | Description |
|---|---|---|
| `qrsdp_feed` | 7 | Live simulator → ITCH 5.0 over UDP multicast |
| `qrsdp_replay` | 7 | Replay `.qrsdp` files as ITCH over UDP at configurable speed |
| `qrsdp_listen` | 7 | Reference ITCH consumer (joins multicast, decodes, prints) |

---

## File Map (Expected New Files)

```
src/itch/
  itch_messages.h              Day 6 (packed ITCH message structs)
  itch_encoder.h / .cpp        Day 6 (EventRecord → ITCH binary)
  moldudp64.h / .cpp           Day 6 (MoldUDP64 session-layer framing)
  udp_sender.h / .cpp          Day 7 (UDP multicast sender)
  itch_feed_sink.h / .cpp      Day 7 (IEventSink → ITCH over UDP)

src/model/
  hawkes_intensity.h / .cpp    Day 8 (self-exciting intensity wrapper)
  market_factor.h / .cpp       Day 9 (shared Brownian factor for correlation)

src/
  feed_main.cpp                Day 7 (qrsdp_feed entry point)
  replay_main.cpp              Day 7 (qrsdp_replay entry point)
  listen_main.cpp              Day 7 (qrsdp_listen entry point)

tests/qrsdp/
  test_itch_encoder.cpp        Day 6
  test_moldudp64.cpp           Day 7
  test_udp_roundtrip.cpp       Day 7
  test_hawkes_intensity.cpp    Day 8

notebooks/
  05_hawkes_comparison.ipynb   Day 8
```

## What Does NOT Change

- `.qrsdp` binary file format (stays at v1.0)
- `BinaryFileSink`, `EventLogReader` — no changes
- `QrsdpProducer`, `IProducer`, `IEventSink` interface — no changes (Hawkes wraps the model, not the producer; ITCH wraps the sink)
- Python reader and existing notebooks (01–04) — untouched
- `book_replay.py`, `ohlc.py` — no changes
