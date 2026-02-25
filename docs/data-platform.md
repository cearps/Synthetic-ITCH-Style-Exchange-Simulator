# Data Platform Architecture

## Overview

The exchange simulator data platform extends the core QRSDP producer with a
streaming event backbone (Kafka) and a columnar OLAP store (ClickHouse). The
architecture follows a dual-write
pattern: every event is simultaneously written to the local `.qrsdp` binary log
(for low-latency replay) and to Kafka (for real-time distribution).

ClickHouse consumes events directly from Kafka via its native **Kafka engine**
-- no custom consumer code required. A materialized view transforms each raw
26-byte binary record into a typed, enriched row in a MergeTree table that is
immediately queryable.

In **streaming mode** (`--realtime`), the producer emits events paced to
simulated inter-arrival times rather than running as fast as possible. A
`--speed` multiplier controls how fast simulated time passes relative to wall
clock time (e.g. `--speed 100` compresses a 6.5-hour session into ~4 minutes).
Combined with `--days 0` (continuous mode), the producer runs indefinitely,
generating day after day until stopped with SIGTERM.

```
┌─────────────┐
│  QRSDP      │
│  Producer    │
│  (C++)      │
└──────┬──────┘
       │ MultiplexSink
       ├──────────────────┐
       ▼                  ▼
┌──────────────┐   ┌──────────────┐
│ BinaryFile   │   │  KafkaSink   │
│ Sink (.qrsdp)│   │  (librdkafka)│
└──────────────┘   └──────┬───────┘
                          │
                   ┌──────▼───────┐
                   │    Kafka     │
                   │  (KRaft)    │
                   └──────┬───────┘
                          │
                   ┌──────▼───────┐
                   │  ClickHouse  │
                   │ Kafka Engine │
                   │      ↓       │
                   │ Materialized │
                   │    View      │
                   │      ↓       │
                   │  MergeTree   │
                   └──────┬───────┘
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
        ┌──────────┐ ┌──────────┐ ┌──────────┐
        │ Notebook │ │ HTTP API │ │  Future   │
        │  query   │ │  query   │ │ Consumer │
        └──────────┘ └──────────┘ └──────────┘
```

## Components

### C++ Producer (MultiplexSink)

- **MultiplexSink** (`src/io/multiplex_sink.h`): Fan-out wrapper implementing
  `IEventSink`. Forwards every `append()` call to N downstream sinks.
  Best-effort: if one sink throws, remaining sinks still receive the event.

- **KafkaSink** (`src/io/kafka_sink.{h,cpp}`): Publishes each event as a
  26-byte `DiskEventRecord` to a Kafka topic. Uses the security symbol as
  the message key for partition affinity. Compiled only when
  `BUILD_KAFKA_SUPPORT=ON`.

- **BinaryFileSink**: Existing `.qrsdp` log writer (chunked LZ4 compression).

- **Real-time pacing**: When `--realtime` is set, the producer sleeps between
  events so that wall-clock time matches simulated time (scaled by `--speed`).
  Without `--realtime`, the producer runs at full speed for batch workloads.

- **Continuous mode**: When `--days 0`, the day loop runs indefinitely. Each
  day's closing price chains into the next day's open. The producer handles
  SIGTERM gracefully, completing the current event before shutting down.

### Kafka (KRaft mode)

Single-node KRaft broker running in Docker. No ZooKeeper dependency.
Topic `exchange.events` with 3 partitions (one per symbol for ordered
per-security streams).

### ClickHouse

ClickHouse replaces the previous MinIO + Python Parquet consumer pipeline.
Three database objects handle the entire ingestion:

1. **`exchange_events_kafka`** (Kafka engine table): reads 26-byte RowBinary
   records from the `exchange.events` Kafka topic. The binary layout
   (`ts_ns u64, type u8, side u8, price_ticks i32, qty u32, order_id u64`)
   maps directly to ClickHouse's `RowBinary` format.

2. **`exchange_events`** (MergeTree table): the queryable storage, partitioned
   by `(symbol, date)` and ordered by `(symbol, date, ts_ns)`. Includes
   derived columns `type_name` (human-readable) and `symbol` (from Kafka key).

3. **`exchange_events_mv`** (Materialized View): transforms each Kafka record
   on arrival — maps type codes to names via `multiIf`, extracts `symbol`
   from the Kafka message key (`_key`), and `date` from the broker timestamp.

4. **`current_bbo`** (AggregatingMergeTree): maintains the latest trade-implied
   best bid and ask per symbol. Updated incrementally by `current_bbo_mv`
   which processes only execution events (type 4 = EXECUTE_BUY → best ask,
   type 5 = EXECUTE_SELL → best bid). Uses `argMaxState` so the query cost
   is O(1) regardless of total event count.

5. **`v_current_midprice`** (View): convenience layer over `current_bbo` that
   merges the aggregate state and returns `best_bid_ticks`, `best_ask_ticks`,
   and `midprice_ticks` per symbol. Query with `SELECT * FROM v_current_midprice`.

The init SQL is in `pipeline/clickhouse/init.sql`, templated by `init.sh`
which substitutes `${KAFKA_BROKERS}` and `${KAFKA_TOPIC}` at startup.

## Running the Streaming Platform

The Docker Compose stack uses **profiles** to separate always-on platform
services from on-demand jobs.

### Start the platform

```bash
docker compose -f docker/docker-compose.yml --profile platform up -d --build
```

This launches:
- **kafka** — KRaft broker
- **clickhouse** — OLAP store with Kafka engine consuming events automatically
- **kafka-producer** — runs with `--realtime --speed 100 --days 0`
  (continuous streaming, ~4 min per simulated trading day)

Events flow from the producer through Kafka into ClickHouse with zero lag.
Data is queryable the moment it arrives.

### Query anytime

```bash
# Query ClickHouse directly from local Python / notebooks:
# import clickhouse_connect
# client = clickhouse_connect.get_client(host='localhost', port=8123)
# client.query_df("SELECT * FROM exchange_events LIMIT 10")

# Current midprice per symbol (near-instant, reads pre-aggregated state):
# client.query_df("SELECT * FROM v_current_midprice")

# Or use the ClickHouse HTTP API:
curl 'http://localhost:8123/?query=SELECT+count()+FROM+exchange_events'
curl 'http://localhost:8123/?query=SELECT+*+FROM+v_current_midprice'
```

### Stop the platform

```bash
docker compose -f docker/docker-compose.yml --profile platform down
```

ClickHouse data persists in the `clickhouse-data` Docker volume across
restarts. Add `-v` to wipe everything clean.

### Batch mode (legacy)

For one-shot batch runs without real-time pacing, override the producer command:

```bash
docker compose -f docker/docker-compose.yml run --rm kafka-producer \
    --seed 42 --days 5 --output /app/output/batch_run \
    --kafka-brokers kafka:9092 --securities AAPL:10000,MSFT:15000,GOOG:12000
```

## Switching to Managed Services

All external service connections are controlled via YAML anchors at the top of
`docker/docker-compose.yml`. To point at managed Kafka and/or ClickHouse,
update the anchor values (or use a `.env` file override):

### Managed Kafka

```yaml
x-kafka-env: &kafka-env
  KAFKA_BROKERS: your-managed-cluster:9092
  KAFKA_TOPIC: exchange.events
  KAFKA_SECURITY_PROTOCOL: SASL_SSL
  KAFKA_SASL_MECHANISM: SCRAM-SHA-256
  KAFKA_SASL_USERNAME: your-username
  KAFKA_SASL_PASSWORD: your-password
```

Then remove the local `kafka` service from the compose file (or simply don't
start it). The C++ producer, ClickHouse Kafka engine, and any other consumers
all read from these same env vars.

## Configuration

### CLI Flags (C++ Producer)

| Flag | Default | Description |
|:-----|:--------|:------------|
| `--kafka-brokers` | (empty) | Kafka bootstrap servers; empty = file-only |
| `--kafka-topic` | `exchange.events` | Kafka topic name |
| `--realtime` | off | Pace events to simulated inter-arrival times |
| `--speed` | `100.0` | Speed multiplier (100 = 6.5h session in ~4 min) |
| `--days` | `5` | Trading days to generate; 0 = run indefinitely |

### Environment Variables

| Variable | Default | Used By |
|:---------|:--------|:--------|
| `KAFKA_BROKERS` | `kafka:9092` | Producer, ClickHouse init |
| `KAFKA_TOPIC` | `exchange.events` | Producer, ClickHouse init |
| `CLICKHOUSE_HOST` | `clickhouse` | Notebooks |
| `CLICKHOUSE_PORT` | `8123` | Notebooks |
| `CLICKHOUSE_USER` | `default` | Notebooks |
| `CLICKHOUSE_PASSWORD` | (empty) | Notebooks |
| `CLICKHOUSE_DB` | `default` | Notebooks |

### CMake Options

| Option | Default | Description |
|:-------|:--------|:------------|
| `BUILD_KAFKA_SUPPORT` | `OFF` | Enable KafkaSink (requires librdkafka) |

## Local Development

The C++ codebase builds and tests fine without Kafka (`BUILD_KAFKA_SUPPORT=OFF`).
All Kafka-dependent code is behind `#ifdef QRSDP_KAFKA_ENABLED`. The full
Kafka stack only runs inside Docker where `librdkafka-dev` is installed via
apt-get.

To test real-time pacing locally (without Kafka):

```bash
./build/qrsdp_run --realtime --speed 1000 --days 1 --seconds 100
```
