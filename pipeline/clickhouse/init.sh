#!/bin/bash
set -e

KAFKA_BROKERS="${KAFKA_BROKERS:-kafka:9092}"
KAFKA_TOPIC="${KAFKA_TOPIC:-exchange.events}"

echo "Initialising ClickHouse schema (kafka_broker_list=${KAFKA_BROKERS}, topic=${KAFKA_TOPIC})..."

sed -e "s|\${KAFKA_BROKERS}|${KAFKA_BROKERS}|g" \
    -e "s|\${KAFKA_TOPIC}|${KAFKA_TOPIC}|g" \
    /docker-entrypoint-initdb.d/init.sql.template \
    | clickhouse-client --host localhost --password "${CLICKHOUSE_PASSWORD:-}" --multiquery

echo "ClickHouse schema ready."
