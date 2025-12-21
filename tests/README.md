# Tests

This directory contains unit tests for the Synthetic ITCH Exchange Simulator.

## Structure

Tests mirror the source code structure:

```
tests/
├── core/           # Tests for core data types
├── producer/       # Tests for event producer
├── matching/       # Tests for matching engine and order book
├── logging/        # Tests for event log
├── encoding/       # Tests for ITCH encoder
└── streaming/      # Tests for UDP streamer
```

## Test Requirements

- All tests must be deterministic (no time-dependent or random behavior without seeds)
- Each test should be independent (no shared state)
- Tests should follow TDD principles
- Use descriptive test names

## Running Tests

See `docs/build-test-run.md` for instructions on running tests.

