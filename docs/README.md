# Documentation

This folder contains project documentation for both humans and AI coding agents.

## Project

- [Project goal](project-goal.md)
- [Build, test, and run](build-test-run.md) — how to build all targets and run each CLI tool
- [Event log format](event-log-format.md) — binary `.qrsdp` file format specification
- [Chunk size tuning](chunk-size-tuning.md) — compression vs granularity tradeoff analysis
- [Dev week 1 plan](dev-week-plan.md) — 5-day plan for event log persistence, replay, and Jupyter visualisation
- [Dev week 2 plan](dev-week-2-plan.md) — data platform (Kafka, Parquet, dbt), ITCH streaming, and producer improvements

## Python Analysis

- [Python notebooks](build-test-run.md#python-notebooks) — setup, data generation, and interactive notebooks for price visualisation, stylised facts, and session summaries

## QRSDP Producer

- [QRSDP Mechanics](producer/QRSDP_MECHANICS.md) — method-level breakdown of the producer
- [QRSDP v1 Walkthrough](producer/QRSDP_V1_WALKTHROUGH.md) — implementation walkthrough
- [Calibration](producer/QRSDP_CALIBRATION.md) — intensity estimation and curve I/O
- [Unified Build Plan](producer/00_unified_build_plan.md) — v1 implementation plan

## Reviews & Audits

- [Model Math & Code Review](06_model_math_and_code_review.md) — equation-to-code mapping, sanity checks, findings
- [SimpleImbalance Nonsense Audit](07_simple_imbalance_nonsense_audit.md) — deep audit of the legacy intensity model

## AI Agent Guidance

- [General rules](ai/general-rules.md)
