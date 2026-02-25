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
