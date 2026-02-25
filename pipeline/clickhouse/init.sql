-- ClickHouse schema for the exchange event streaming pipeline.
-- Kafka broker and topic are substituted by init.sh before execution.

CREATE TABLE IF NOT EXISTS exchange_events_kafka (
    ts_ns      UInt64,
    type       UInt8,
    side       UInt8,
    price_ticks Int32,
    qty        UInt32,
    order_id   UInt64
) ENGINE = Kafka
SETTINGS
    kafka_broker_list = '${KAFKA_BROKERS}',
    kafka_topic_list = '${KAFKA_TOPIC}',
    kafka_group_name = 'clickhouse-consumer',
    kafka_format = 'RowBinary',
    kafka_num_consumers = 1;

CREATE TABLE IF NOT EXISTS exchange_events (
    ts_ns       UInt64,
    type        UInt8,
    type_name   LowCardinality(String),
    side        UInt8,
    price_ticks Int32,
    qty         UInt32,
    order_id    UInt64,
    symbol      LowCardinality(String),
    date        Date
) ENGINE = MergeTree()
PARTITION BY (symbol, date)
ORDER BY (symbol, date, ts_ns);

CREATE MATERIALIZED VIEW IF NOT EXISTS exchange_events_mv
TO exchange_events AS
SELECT
    ts_ns,
    type,
    multiIf(
        type = 0, 'ADD_BID',
        type = 1, 'ADD_ASK',
        type = 2, 'CANCEL_BID',
        type = 3, 'CANCEL_ASK',
        type = 4, 'EXECUTE_BUY',
        type = 5, 'EXECUTE_SELL',
        'UNKNOWN'
    ) AS type_name,
    side,
    price_ticks,
    qty,
    order_id,
    _key AS symbol,
    toDate(_timestamp) AS date
FROM exchange_events_kafka;

-- ── Best-bid/offer tracking (trade-implied) ────────────────────────
-- Execution events reveal the BBO at the moment of the trade:
--   EXECUTE_BUY  (4) fills at the best ask
--   EXECUTE_SELL (5) fills at the best bid
-- An AggregatingMergeTree keeps the latest value per symbol with
-- near-zero query cost (just reads the pre-aggregated state).

CREATE TABLE IF NOT EXISTS current_bbo (
    symbol   LowCardinality(String),
    last_bid AggregateFunction(argMax, Int32, UInt64),
    last_ask AggregateFunction(argMax, Int32, UInt64)
) ENGINE = AggregatingMergeTree()
ORDER BY symbol;

CREATE MATERIALIZED VIEW IF NOT EXISTS current_bbo_mv TO current_bbo AS
SELECT
    _key AS symbol,
    argMaxState(
        if(type = 5, price_ticks, toInt32(-2147483648)),
        if(type = 5, ts_ns, toUInt64(0))
    ) AS last_bid,
    argMaxState(
        if(type = 4, price_ticks, toInt32(2147483647)),
        if(type = 4, ts_ns, toUInt64(0))
    ) AS last_ask
FROM exchange_events_kafka
WHERE type IN (4, 5)
GROUP BY _key;

CREATE VIEW IF NOT EXISTS v_current_midprice AS
SELECT
    symbol,
    argMaxMerge(last_bid) AS best_bid_ticks,
    argMaxMerge(last_ask) AS best_ask_ticks,
    (argMaxMerge(last_bid) + argMaxMerge(last_ask)) / 2.0 AS midprice_ticks
FROM current_bbo
GROUP BY symbol
HAVING best_bid_ticks > -2147483648 AND best_ask_ticks < 2147483647;
