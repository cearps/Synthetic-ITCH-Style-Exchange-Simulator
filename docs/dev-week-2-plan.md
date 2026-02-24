# Dev Week 2 Plan — Data Platform, ITCH Streaming + Producer Improvements

**Duration:** 4 days (Monday–Thursday)
**Objective:** Build a Kafka-based event distribution layer with dual-write from the C++ producer, a Parquet analytics landing zone with dbt, an ITCH 5.0 UDP multicast feed served from Kafka, then add Hawkes self-exciting and correlated multi-security intensity models.

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
| 5 Jupyter notebooks (price visualisation, stylised facts, session summary, multi-security, model comparison) | Done |
| Google Test suite (94 tests across 12 files) | Done |
| Docker support (build, test, notebooks) | Done |
| Headless CLI (`qrsdp_cli`), multi-day runner (`qrsdp_run`), log inspector (`qrsdp_log_info`), calibrator (`qrsdp_calibrate`) | Done |

**Key interfaces for this plan:**

- `IEventSink` (`src/io/i_event_sink.h`) — single `append(const EventRecord&)` method; the extension point for Kafka
- `BinaryFileSink` (`src/io/binary_file_sink.h`) — existing file sink; will be composed inside MultiplexSink
- `EventRecord` / `DiskEventRecord` (`src/core/records.h`) — 30-byte in-memory (with flags), 26-byte on-disk packed struct
- `SessionRunner` (`src/producer/session_runner.h`) — orchestrates multi-day/multi-security runs, creates sinks per day
- `qrsdp_run` (`src/run_main.cpp`) — CLI entry point; will gain `--kafka-brokers` flag
- `qrsdp_reader.py` (`notebooks/qrsdp_reader.py`) — Python binary reader with `RECORD_DTYPE`
- Docker Compose (`docker/docker-compose.yml`) — existing services: build, test, simulator, notebooks

---

## Architecture

```
┌─────────────────────────────────────────┐
│         C++ Producer Process            │
│                                         │
│  QrsdpProducer ──▶ MultiplexSink        │
│                      ├──▶ BinaryFileSink│
│                      └──▶ KafkaSink     │
└──────────────┬───────────────┬──────────┘
               │               │
               ▼               ▼
     .qrsdp files        Kafka topic:
     (archival,          exchange.events
      source of truth)        │
                    ┌─────────┼─────────┐
                    │                    │
                    ▼                    ▼
          Parquet Consumer       ITCH Stream Consumer
          (Python)               (C++, Kafka consumer)
                    │                    │
                    ▼                    ▼
          MinIO / S3             ITCH 5.0 + MoldUDP64
          (Parquet files)              │
                    │                  ▼
                    ▼           UDP Multicast (239.1.1.1)
          dbt + DuckDB                 │
                    │                  ▼
                    ▼           qrsdp_listen
          Mart tables           (reference decoder)
          (OHLCV, stats)
```

**Design principle (mirrors real exchange architecture):** The producer writes to a local journal (`.qrsdp`) and a distribution layer (Kafka) simultaneously via MultiplexSink. The `.qrsdp` file and Kafka are independent — if Kafka is unavailable, the file write still succeeds. The ITCH feed handler is a **separate process** consuming from Kafka, decoupled from the producer for fault isolation, independent scaling, and restartability. If the ITCH consumer crashes, the producer keeps running and no data is lost; the consumer restarts from its last Kafka offset.

---

## Day 6 (Monday) — Data Platform: Kafka Integration + Parquet + dbt

**Goal:** Build the full data platform in one day: C++ dual-write (MultiplexSink + KafkaSink), Docker infrastructure (Kafka, MinIO), Python Parquet consumer, dbt analytics layer, and data quality checks.

### C++ Components

1. **Add librdkafka dependency to CMake** (`CMakeLists.txt`)
   - Use `find_package(RdKafka REQUIRED)` — librdkafka ships CMake config files
   - Make Kafka support optional: `option(BUILD_KAFKA_SUPPORT "Enable Kafka sink" OFF)`
   - When enabled, link `RdKafka::rdkafka++` into `simulator_lib`
   - Update `docker/Dockerfile.build` to `apt-get install librdkafka-dev`

2. **Implement `MultiplexSink`** (`src/io/multiplex_sink.h/.cpp`)
   - `MultiplexSink : public IEventSink`
   - Holds a `std::vector<IEventSink*>` (non-owning pointers)
   - `append()` forwards to all sinks; if one throws, log and continue to the rest (best-effort fanout)
   - Also forward `flush()` and `close()` — add these as virtual methods on `IEventSink` with default no-op implementations to avoid breaking existing sinks

3. **Implement `KafkaSink`** (`src/io/kafka_sink.h/.cpp`)
   - `KafkaSink : public IEventSink`
   - Constructor: `KafkaSink(const std::string& brokers, const std::string& topic, const std::string& symbol)`
   - Creates `RdKafka::Producer` with: `bootstrap.servers`, `enable.idempotence=true`, `linger.ms=5`, `compression.type=lz4`
   - `append()`: serialize `DiskEventRecord` (26 bytes, same layout as `.qrsdp`) as message value; use `symbol` as message key for partition affinity
   - Include `ts_ns` as a Kafka message header for consumer-side filtering without deserialization
   - `flush()` / `close()`: flush pending messages, destroy producer
   - Error handling: delivery report callback logs failed sends but does not block the producer

4. **Wire into SessionRunner** (`src/producer/session_runner.cpp`)
   - When `kafka_brokers` is set in `RunConfig`, wrap `BinaryFileSink` + `KafkaSink` in `MultiplexSink`
   - When not set, use `BinaryFileSink` directly (no behaviour change)
   - Add to `RunConfig`: `std::string kafka_brokers` and `std::string kafka_topic`
   - Add CLI flags to `src/run_main.cpp`: `--kafka-brokers <host:port>`, `--kafka-topic <name>` (default: `exchange.events`)

5. **Tests**: `tests/qrsdp/test_multiplex_sink.cpp` — verify fanout to 2+ mock sinks, verify one sink failure doesn't block others

### Infrastructure

6. **Docker Compose** (`docker/docker-compose.yml`)
   - Add Kafka service (KRaft mode, single broker, `confluentinc/cp-kafka:7.6.0` or `bitnami/kafka:3.7`)
   - Add MinIO service (`minio/minio`) for S3-compatible object storage
   - Add `createbuckets` init service to create the `exchange-data` bucket on startup
   - Add `parquet-consumer` service (Python, runs `parquet_consumer.py`)
   - Add `dbt-runner` service (one-shot: `dbt run && dbt test`)
   - Update simulator service to pass `--kafka-brokers kafka:9092`
   - Expose: Kafka `localhost:9092`, MinIO `localhost:9000`

### Python Pipeline

7. **Parquet consumer** (`pipeline/parquet_consumer.py`)
   - `confluent-kafka` Python client, consumer group `parquet-writer`
   - Deserializes 26-byte message values using the same `RECORD_DTYPE` numpy dtype from `notebooks/qrsdp_reader.py`
   - Batches messages (10,000 records or 5 seconds, whichever first)
   - Writes Parquet via `pyarrow` to MinIO: `s3://exchange-data/raw/events/symbol={key}/date={YYYY-MM-DD}/events-{offset}.parquet`
   - Handles graceful shutdown (commit offsets, flush partial batch)

8. **dbt project** (`pipeline/dbt_project/`)
   - DuckDB profile reading Parquet from MinIO via `httpfs` extension
   - **Staging**: `stg_events.sql` — read raw Parquet, cast types, add `event_category`, `is_trade`, `time_seconds`
   - **Mart**: `fct_ohlcv_1min.sql` — 1-minute OHLCV bars per symbol per day via window functions
   - **Mart**: `fct_session_summary.sql` — per-session statistics (event counts by type, open/close, spread stats)
   - **dbt tests**: not-null PKs, accepted event type values (0–5), monotonic `ts_ns`, no duplicate `order_id`

9. **Data quality checks** (dbt tests)
   - Row count per session within expected range
   - Cross-session: close of day N ≈ open of day N+1

10. **Documentation**: `docs/data-platform.md` — architecture diagram, component descriptions, full-stack runbook. Update `docs/event-log-format.md` with Kafka message format.

**End of day:** `docker compose up` runs the full pipeline end-to-end: simulator dual-writes `.qrsdp` + Kafka, Parquet consumer lands files in MinIO, `dbt run` produces OHLCV bars and session summaries queryable in DuckDB.

---

## Day 7 (Tuesday) — ITCH 5.0 Streaming via Kafka Consumer

**Goal:** Build an ITCH encoder, MoldUDP64 framer, and UDP multicast sender, wired as a Kafka consumer that reads events from the `exchange.events` topic and streams them as spec-compliant ITCH 5.0 messages. Also build a reference listener for testing.

**Design rationale:** The ITCH feed handler is a separate process from the producer, consuming from Kafka. This mirrors real exchange architecture where the matching engine and feed handler are decoupled — the feed handler can crash, restart, or be redeployed without affecting the producer. It restarts from its last committed Kafka offset with no data loss.

### Tasks

1. **Define ITCH message structs** (`src/itch/itch_messages.h`)
   - `MessageType` enum and packed big-endian structs for the 5-message subset:

   | Message | Type byte | Size | Purpose |
   |---|---|---|---|
   | System Event | `S` | 12 bytes | Session start/end markers |
   | Stock Directory | `R` | 39 bytes | Symbol metadata (sent once per security at session start) |
   | Add Order | `A` | 36 bytes | New limit order: reference number, side B/S, shares, stock, price |
   | Order Delete | `D` | 19 bytes | Full cancellation: reference number |
   | Order Executed | `E` | 31 bytes | Trade: reference number, shares, match number |

   - All fields big-endian, `#pragma pack(push, 1)`, per NASDAQ ITCH 5.0 specification

2. **Implement `ItchEncoder`** (`src/itch/itch_encoder.h/.cpp`)
   - `ItchEncoder(const std::string& symbol, uint32_t tick_size)`
   - `encode(const EventRecord&) → std::vector<uint8_t>`: translates event type + fields to the appropriate ITCH message
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

   - Maintains monotonic match number counter for executions

3. **Implement MoldUDP64 framing** (`src/itch/moldudp64.h/.cpp`)
   - `MoldUDP64Framer(const std::string& session_id)`: 10-char session ID, 64-bit sequence number
   - `addMessage(const uint8_t* data, uint16_t len)`: accumulate into current packet
   - `flush() → std::vector<uint8_t>`: emit complete MoldUDP64 packet (20-byte header + message blocks)
   - Auto-flush when approaching MTU (~1400 bytes to leave room for IP/UDP headers)

4. **Implement UDP multicast sender** (`src/itch/udp_sender.h/.cpp`)
   - `UdpMulticastSender(const std::string& group, uint16_t port, uint8_t ttl = 1)`
   - POSIX sockets: `socket(AF_INET, SOCK_DGRAM)`, `setsockopt(IP_MULTICAST_TTL)`, `sendto()`
   - `send(const uint8_t* data, size_t len)` — fire-and-forget UDP
   - Cross-platform: POSIX on macOS/Linux, Winsock on Windows

5. **Implement ITCH stream consumer** (`src/itch/itch_stream_consumer.h/.cpp`)
   - C++ Kafka consumer (`RdKafka::KafkaConsumer`) reading from `exchange.events` topic
   - Consumer group: `itch-streamer`
   - On each message: deserialize 26-byte `DiskEventRecord`, encode to ITCH via `ItchEncoder`, frame via `MoldUDP64Framer`, send via `UdpMulticastSender`
   - At session start (first message or session-change detection): emit System Event (`S`) + Stock Directory (`R`) before order messages
   - Configurable: `--multicast-group` (default `239.1.1.1`), `--port` (default `5001`)

6. **ITCH stream binary** (`src/itch_stream_main.cpp` → `qrsdp_itch_stream`)
   - CLI entry point that creates and runs the `ItchStreamConsumer`
   - Flags: `--kafka-brokers`, `--kafka-topic`, `--multicast-group`, `--port`, `--consumer-group`

7. **Reference listener** (`src/listen_main.cpp` → `qrsdp_listen`)
   - Joins multicast group, receives MoldUDP64 packets, decodes ITCH messages
   - Prints human-readable output: `[seq=1] ADD_ORDER ref=42 side=B shares=1 stock=AAPL price=100.0000`
   - Useful for testing and demos

8. **Tests**
   - `tests/qrsdp/test_itch_encoder.cpp`: encode each event type, verify message size + type byte, verify big-endian field ordering, verify match number increments on executions
   - `tests/qrsdp/test_moldudp64.cpp`: header layout, sequence number progression, message count, MTU splitting
   - `tests/qrsdp/test_udp_roundtrip.cpp`: localhost loopback send/receive, decode and verify ITCH messages match originals

**End of day:** Run `docker compose up` to start the full stack, then in a separate terminal `qrsdp_listen --port 5001` shows a live stream of decoded ITCH messages. The ITCH consumer independently processes events from Kafka with no coupling to the producer process.

---

## Day 8 (Wednesday) — Hawkes Self-Exciting Intensity Model

**Goal:** Add a self-exciting (Hawkes) process where each event temporarily boosts future arrival rates, producing realistic volatility clustering — bursts of high activity followed by calm periods.

### Tasks

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
   - Extend `ModelType` enum: `SIMPLE`, `HLR`, `HAWKES`
   - Add `--model hawkes` CLI flag to `qrsdp_run`
   - When `--model hawkes`: wrap `SimpleImbalanceIntensity` in `HawkesIntensity`
   - Add CLI flags: `--hawkes-alpha`, `--hawkes-beta`, `--hawkes-cap` with sensible defaults
   - Update `runSecurityDays()` in `session_runner.cpp` to accept a model selector and instantiate accordingly

4. **Tests** (`tests/qrsdp/test_hawkes_intensity.cpp`)
   - Verify excitation decays: after a burst of events, check that excitation decreases exponentially over time
   - Verify cap: with high alpha, excitation should not exceed `max_excitation`
   - Verify decorator: with `alpha=0`, output should exactly match the base model
   - Deterministic step-by-step: manually compute expected intensities for a known event sequence and compare

5. **Notebook** (`notebooks/06_hawkes_comparison.ipynb`)
   - Generate two datasets: `--model simple` and `--model hawkes --hawkes-alpha 0.5 --hawkes-beta 2.0`
   - Compare autocorrelation of absolute returns (Hawkes should show positive autocorrelation; Simple should not)
   - Overlay inter-arrival time distributions (Hawkes should have a heavier tail of short inter-arrivals)
   - Event rate over time (Hawkes should show visible clustering; Simple should be more uniform)

**End of day:** Running `qrsdp_run --model hawkes --days 5` produces data with visible volatility clustering that is absent in the default model. Events flow through Kafka and appear as ITCH on the multicast feed.

---

## Day 9 (Thursday) — Correlated Securities + HLR Wiring + Polish

**Goal:** Add cross-security correlation, wire the existing HLR curve model into the session runner, and polish documentation.

### Tasks

1. **Correlated multi-security via shared market factor** (`src/model/market_factor.h/.cpp`)
   - `MarketFactor`: samples a common Brownian increment `dW ~ N(0, dt)` shared across all securities
   - Each security has a `factor_loading` parameter (e.g. 0.5) that scales its exposure to the common factor
   - Implementation: the factor adjusts the `imbalance` field in `BookFeatures` before it's passed to the intensity model, creating correlated price movements without coupling order books
   - Add `--factor-loading <f>` CLI flag (default 0.0 = fully independent, as today)
   - In `SessionRunner::run()`, create a single `MarketFactor` instance shared (read-only per time step) across security threads via a synchronisation barrier or pre-sampled factor path

2. **Wire `CurveIntensityModel` into `SessionRunner` fully**
   - Currently `runSecurityDays()` only instantiates `SimpleImbalanceIntensity`
   - When `--model hlr`: instantiate `CurveIntensityModel` with `HLRParams`
   - If no `--hlr-curves` file provided, use built-in defaults from `makeDefaultHLRParams()`
   - Completes the calibration closed loop: simulate → calibrate → feed calibrated curves back

3. **Documentation updates**
   - Update `README.md` — add data platform section, ITCH streaming, new Docker services, model selection (`--model`)
   - Update `docs/build-test-run.md` — CLI usage for all new binaries and flags (`--kafka-brokers`, `--model hawkes`, `qrsdp_itch_stream`, `qrsdp_listen`)
   - Update `docs/README.md` — add links to `data-platform.md` and this plan doc

4. **Notebook** (extend `notebooks/05_model_comparison.ipynb` or new `notebooks/07_three_model_comparison.ipynb`)
   - Side-by-side comparison: Simple vs Hawkes vs HLR
   - Metrics: return autocorrelation, inter-arrival distribution, event rate clustering, price path character

5. **Final checks**
   - Run full test suite (target ~115+ tests)
   - Full Docker Compose stack runs end-to-end (simulator → Kafka → Parquet + ITCH)
   - Clean up any temporary files
   - Tag release: `v0.3.0`

**End of day:** All three intensity models are accessible via CLI, multi-security runs can be correlated, ITCH streaming works end-to-end, and the full data platform runs in Docker.

---

## Out of Scope (Acknowledged — Future Enhancements)

| Item | Description |
|---|---|
| **Hot/local storage lifecycle** | `.qrsdp` files on local disk are the hot tier. In production, a nightly job would re-compress with Zstd, upload to S3 warm storage, with S3 lifecycle rules transitioning to Glacier Deep Archive after 90 days. Document in `data-platform.md`. |
| **Schema Registry + Avro** | Kafka messages use raw 26-byte `DiskEventRecord` binary. Production systems would use Avro with Schema Registry for schema evolution. |
| **ITCH retransmission/recovery** | MoldUDP64 gap detection is noted but the retransmit-request protocol is not implemented. UDP is fire-and-forget. |
| **Airflow/Dagster orchestration** | Pipeline runs via Docker Compose commands. Production orchestration is a future task. |

---

## Technical Decisions

| Decision | Choice | Reasoning |
|---|---|---|
| ITCH consumer architecture | Kafka consumer (separate process) | Mirrors real exchanges: matching engine and feed handler are separate. Fault isolation, independent scaling, restartability (resume from offset). Producer does not know ITCH exists. |
| Kafka client library | librdkafka (C/C++) | De facto standard; used by confluent-kafka-python under the hood. Same library for producer and ITCH consumer. |
| Kafka message format | Raw 26-byte `DiskEventRecord` binary | Avoids libavro/libserdes build complexity. Python consumers already know the dtype. Schema evolution deferred. |
| Kafka mode | KRaft (no ZooKeeper) | Simpler Docker setup, current direction of Kafka. |
| Object storage | MinIO (S3-compatible) | Demonstrates standard data lake backing store. DuckDB reads natively via `httpfs`. |
| Analytics database | dbt-duckdb | Zero infrastructure (embedded), fast on Parquet, no server process. |
| MultiplexSink failure mode | Best-effort fanout | If KafkaSink fails, log and continue writing to BinaryFileSink. File is source of truth; Kafka is best-effort. |
| ITCH version | ITCH 5.0 (NASDAQ) | Current spec, widely documented, 5-message subset covers all event types. |
| Session layer | MoldUDP64 | Standard for ITCH; adds sequence numbers for gap detection. |
| Transport | UDP multicast (239.x.x.x) | Matches real exchange architecture; multiple consumers can join. |
| Hawkes excitation | Multiplicative: `base × (1 + E)` | Preserves relative rate structure of the base model. |
| Hawkes kernel | Exponential decay | Analytically tractable, efficient O(1) update, standard in financial literature. |
| Cross-security correlation | Shared market factor | Cleaner than correlated RNG streams; controllable loading; doesn't modify the RNG layer. |

---

## Dependencies to Add

| Type | Package | How |
|---|---|---|
| **C++** | `librdkafka-dev` | Ubuntu: `apt-get install librdkafka-dev`, macOS: `brew install librdkafka` |
| **C++** | POSIX sockets | System headers, no external dependency |
| **Python** | `confluent-kafka` | `pip install confluent-kafka` |
| **Python** | `pyarrow` | `pip install pyarrow` |
| **Python** | `boto3` | `pip install boto3` |
| **Python** | `dbt-duckdb` | `pip install dbt-duckdb` |
| **Docker** | Kafka | `confluentinc/cp-kafka:7.6.0` |
| **Docker** | MinIO | `minio/minio` |

No new third-party C++ libraries beyond librdkafka. ITCH encoding is hand-rolled (packed structs + endian conversion). UDP uses platform sockets.

---

## New Build Targets

| Target | Day | Description |
|---|---|---|
| `qrsdp_itch_stream` | 7 | Kafka consumer → ITCH 5.0 encoder → MoldUDP64 → UDP multicast |
| `qrsdp_listen` | 7 | Reference ITCH consumer (joins multicast, decodes, prints) |

---

## New File Map

```
src/io/
  multiplex_sink.h / .cpp          Day 6 (fan-out to multiple sinks)
  kafka_sink.h / .cpp               Day 6 (Kafka producer via librdkafka)

src/itch/
  itch_messages.h                   Day 7 (packed ITCH 5.0 message structs)
  itch_encoder.h / .cpp             Day 7 (EventRecord → ITCH binary)
  moldudp64.h / .cpp                Day 7 (MoldUDP64 session-layer framing)
  udp_sender.h / .cpp               Day 7 (UDP multicast sender)
  itch_stream_consumer.h / .cpp     Day 7 (Kafka consumer → ITCH over UDP)

src/model/
  hawkes_intensity.h / .cpp         Day 8 (self-exciting intensity wrapper)
  market_factor.h / .cpp            Day 9 (shared Brownian factor)

src/
  itch_stream_main.cpp              Day 7 (qrsdp_itch_stream entry point)
  listen_main.cpp                   Day 7 (qrsdp_listen entry point)

pipeline/
  parquet_consumer.py               Day 6 (Kafka → Parquet landing)
  requirements.txt                  Day 6 (Python deps for pipeline)
  dbt_project/
    dbt_project.yml                 Day 6
    models/staging/stg_events.sql
    models/marts/fct_ohlcv_1min.sql
    models/marts/fct_session_summary.sql

docker/
  docker-compose.yml                Day 6 (add Kafka, MinIO, consumers)
  Dockerfile.pipeline               Day 6 (Python consumer image)

docs/
  data-platform.md                  Day 6 (architecture + runbook)

tests/qrsdp/
  test_multiplex_sink.cpp           Day 6
  test_itch_encoder.cpp             Day 7
  test_moldudp64.cpp                Day 7
  test_udp_roundtrip.cpp            Day 7
  test_hawkes_intensity.cpp         Day 8

notebooks/
  06_hawkes_comparison.ipynb        Day 8
```

---

## What Does NOT Change

- `.qrsdp` binary file format (stays at v1.0)
- `BinaryFileSink`, `EventLogReader` — no changes
- `QrsdpProducer`, `IProducer` — no changes (Hawkes wraps the model; Kafka wraps the sink; ITCH is a downstream consumer)
- `IIntensityModel` interface — no changes (Hawkes implements it as a decorator)
- Python reader and existing notebooks (01–05) — untouched
- `book_replay.py`, `ohlc.py` — no changes

---

## Success Criteria (End of Week)

- [ ] `qrsdp_run --kafka-brokers localhost:9092 --days 5` dual-writes to `.qrsdp` + Kafka simultaneously
- [ ] MultiplexSink continues writing to file if Kafka is unavailable
- [ ] Parquet files land in MinIO partitioned by symbol/date within seconds of production
- [ ] `dbt run` produces OHLCV bars and session summaries queryable via DuckDB
- [ ] dbt tests pass (no nulls, valid event types, monotonic timestamps)
- [ ] `qrsdp_itch_stream` consumes from Kafka and streams ITCH 5.0 over UDP multicast
- [ ] `qrsdp_listen` decodes and prints all ITCH message types from the live feed
- [ ] ITCH consumer can restart and resume from last Kafka offset without data loss
- [ ] MoldUDP64 packets have correct sequence numbers and message counts
- [ ] `--model hawkes` produces data with measurable volatility clustering (positive autocorrelation of absolute returns)
- [ ] `--model hlr` uses `CurveIntensityModel` and produces events with per-level queue-size-dependent rates
- [ ] Multi-security runs with `--factor-loading 0.5` show visible price correlation between symbols
- [ ] All existing tests still pass; new tests cover MultiplexSink, ITCH encoding, MoldUDP64 framing, Hawkes model, and UDP round-trip
- [ ] `docker compose up` runs the full stack end-to-end

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| librdkafka build complexity on macOS/Linux | Use system package manager; Docker provides a clean build environment. Make Kafka support optional (`BUILD_KAFKA_SUPPORT=OFF` still builds everything else). |
| Cross-platform socket API (POSIX vs Winsock) | Abstract behind a thin `UdpSocket` wrapper; test on macOS/Linux first. Winsock is structurally similar. |
| Multicast not working on all networks | Support `--unicast` fallback flag; localhost testing works regardless. |
| Hawkes model instability (runaway excitation) | Hard cap via `max_excitation`; default parameters chosen conservatively. |
| Kafka not running for tests | MultiplexSink and ITCH encoder tests are pure unit tests (no Kafka needed). Integration tests only run when `docker compose up` is active. |
| Running out of time on Day 9 polish | Days 6–8 are self-contained deliverables; Day 9 features are enhancements, not blockers. |
