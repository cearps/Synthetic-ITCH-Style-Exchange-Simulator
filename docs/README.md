# Documentation

This folder contains project documentation for both humans and AI coding agents.

## Project

- [Project goal](project-goal.md) — motivation, design goals, and intended use cases
- [Build, test, and run](build-test-run.md) — how to build all targets and run each CLI tool
- [Event log format](event-log-format.md) — binary `.qrsdp` file format specification
- [Chunk size tuning](chunk-size-tuning.md) — compression vs granularity tradeoff analysis
- [Performance results](performance-results.md) — throughput benchmarks and compression metrics

## ITCH Streaming

- [ITCH streaming architecture](itch/ITCH_STREAMING.md) — ITCH 5.0 over UDP (unicast/multicast), Docker setup, CLI reference
- [Data platform](data-platform.md) — Kafka dual-write, ClickHouse Kafka engine, Docker runbook

## QRSDP Producer

- [QRSDP Mechanics](producer/QRSDP_MECHANICS.md) — method-level breakdown of the producer
- [QRSDP v1 Walkthrough](producer/QRSDP_V1_WALKTHROUGH.md) — implementation walkthrough
- [Calibration](producer/QRSDP_CALIBRATION.md) — intensity estimation and curve I/O

## Python Analysis

- [Python notebooks](build-test-run.md#python-notebooks) — setup, data generation, and interactive notebooks for price visualisation, stylised facts, and session summaries

## Reviews and Audits

- [Model Math & Code Review](06_model_math_and_code_review.md) — equation-to-code mapping, sanity checks, findings
- [SimpleImbalance Nonsense Audit](07_simple_imbalance_nonsense_audit.md) — deep audit of the legacy intensity model

## Historical Plans

These documents capture the original planning and are preserved for context. They are no longer actively maintained.

- [Dev week 1 plan](dev-week-plan.md) — 5-day plan for event log persistence, replay, and Jupyter visualisation
- [Dev week 2 plan](dev-week-2-plan.md) — data platform, ITCH streaming, and producer improvements
- [Unified build plan](producer/00_unified_build_plan.md) — original v1 implementation plan

## AI Agent Guidance

- [General rules](ai/general-rules.md)

## Templates

- [Component README template](templates/component-README.template.md)
