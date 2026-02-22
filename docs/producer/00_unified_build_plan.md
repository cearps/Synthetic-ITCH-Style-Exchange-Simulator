# 00_unified_build_plan.md
## Unified Build Plan — QRSDP Single-Asset LOB Event Producer (v1)

This single document replaces and consolidates the earlier design docs into one **implementation-oriented** plan.  
It is intended to be the **only file** a Cursor/LLM agent needs to read to implement v1.

**v1 Scope:** build the **intraday producer** that generates **one session** of events into an **in-memory sink**, with unit tests.  
**Out of scope (v1):** daily loop, file sinks, ITCH encoding, regimes/Hawkes, networking, Redis, multithreading.

---

# 1) What we are building

A **single-security**, **event-driven** simulator of a limit order book (LOB). The simulator produces a stream of events with timestamps, similar in spirit to exchange feeds. The output will eventually be convertible to ITCH, but v1 only needs a clean, deterministic event log in memory.

### Core loop (continuous time)

At any time `t`:
1. Compute state features from the current book.
2. Compute intensities (rates) for each event type: `λ_i(state)`.
3. Let `λ_total = Σ λ_i`.
4. Sample inter-arrival time: `Δt ~ Exp(λ_total)`, then `t ← t + Δt`.
5. Sample event type: `P(type=i) = λ_i / λ_total`.
6. Sample event attributes (price level, side, qty).
7. Apply event to book.
8. Emit `EventRecord` to sink.
9. Repeat until `t >= session_seconds`.

### Unit size (v1)

- All events have `qty = 1` (unit liquidity quanta).
- Quantity remains part of the schema so other producers can be swapped later.

---

# 2) Strict v1 requirements

## Must-haves
- Deterministic: same seed ⇒ identical event stream.
- Continuous-time: exponential waiting times.
- Competing intensity categorical selection for event type.
- Book invariants always hold:
  - non-negative depth
  - bid < ask
  - spread ≥ 1 tick
- No per-event heap allocations in the hot loop.
- Modular design via interfaces (swap-ability later).

## Must-NOTs
- No daily loop / SimulationRunner.
- No file I/O / BinaryFileSink (use in-memory stub only).
- No ITCH encoding.
- No regimes, overnight gap logic, or Hawkes.
- No concurrency.

---

# 3) Event types (minimal)

Use these in v1:

- `ADD_LIMIT` (adds 1 unit to a chosen level/side)
- `CANCEL` (removes 1 unit from a chosen level/side; clamp at 0)
- `EXECUTE` (market order that consumes 1 unit from the opposite best)

Optional later (not v1): `TRADE` (can be derived from EXECUTE), snapshots, halts.

---

# 4) Data model (packed records)

Represent price in integer ticks.

## 4.1 EventRecord (fixed width)

Recommended fields (tweak as needed, but keep fixed-width):

- `uint64_t ts_ns`  — nanoseconds since session start
- `uint8_t  type`   — enum EventType
- `uint8_t  side`   — enum Side (BID/ASK/NA)
- `int32_t  price_ticks`
- `uint32_t qty`    — 1 in v1
- `uint64_t order_id` — monotonically increasing
- `uint32_t flags`  — reserved (e.g., seed order)

Constraints:
- trivially copyable
- no pointers
- stable layout (static_assert size)

## 4.2 TradingSession input

- `uint64_t seed`
- `int32_t  p0_ticks` (reference mid)
- `uint32_t session_seconds` (tests use 5–10 sec; prod can be 6.5h)
- `uint32_t levels_per_side` (e.g., 5 or 10)
- `uint32_t tick_size` (optional metadata; core sim uses ticks)
- intensity params (struct)

## 4.3 SessionResult output

- `int32_t close_ticks`
- `uint64_t events_written`

---

# 5) Interfaces (keep them small)

These are the abstraction boundaries that allow swapping components later.

## 5.1 IOrderBook

Responsibilities:
- store L levels per side
- seed initial depth around `p0_ticks`
- compute features used by intensity model
- apply simulated events

Methods (minimal):
- `void seed(const BookSeed&)`
- `BookFeatures features() const`
- `void apply(const SimEvent&)`
- `Level bestBid() const`
- `Level bestAsk() const`

v1 can model **counts only** (no FIFO per order). FIFO can be added later if needed for queue position strategies.

## 5.2 IIntensityModel

`Intensities compute(const BookFeatures&) const`

No RNG here. Deterministic given features.

## 5.3 IEventSampler

- `double sampleDeltaT(double lambdaTotal)`
- `EventType sampleType(const Intensities&)`

Owns or is injected with RNG.

## 5.4 IAttributeSampler

Given chosen type + current state, selects:
- side
- price_ticks (level)
- qty (1)

`EventAttrs sample(EventType, const IOrderBook&, const BookFeatures&)`

## 5.5 IEventSink

v1 sink is in-memory, but keep an interface:

- `void append(const EventRecord&)`

---

# 6) v1 Implementations (simple + stable)

## 6.1 MultiLevelBook (counts only)

- Maintain `levels_per_side` levels each for bid/ask.
- Each level has:
  - `price_ticks`
  - `depth_units` (uint32)
- Ensure:
  - bid prices strictly increasing toward best bid
  - ask prices strictly increasing away from best ask
  - bestBid < bestAsk

### Seeding rule (at t=0)
Given `p0_ticks`:
- choose spread = 1 tick (or 2 ticks configurable)
- best bid = p0_ticks - 1
- best ask = p0_ticks + 1
- for k=0..L-1:
  - bid level price = best_bid - k
  - ask level price = best_ask + k
  - depth_units sampled from a simple distribution (e.g., constant 50, or Exp/Geom)
Keep it simple: start with constant depth.

### Apply rules (unit size)
- ADD_LIMIT: depth_units++ at target level
- CANCEL: depth_units-- if >0
- EXECUTE:
  - if side == BUY, consume 1 from best ask
  - if side == SELL, consume 1 from best bid
  - if best level hits 0: shift the book:
    - when best bid depleted: best bid price moves down 1 tick; shift levels; create new far bid level with seeded depth
    - when best ask depleted: best ask price moves up 1 tick; shift levels; create new far ask level
This creates a price process from queue depletion.

## 6.2 Features (BookFeatures)

Compute at least:
- `best_bid_ticks`, `best_ask_ticks`
- `q_bid_best`, `q_ask_best`
- `spread_ticks`
- `imbalance`:
  `I = (q_bid_best - q_ask_best) / (q_bid_best + q_ask_best + eps)`

Optionally later:
- multi-level imbalance
- microprice

## 6.3 Intensity model (SimpleImbalanceIntensity)

Return intensities for:
- `λ_add_bid`, `λ_add_ask`
- `λ_cancel_bid`, `λ_cancel_ask`
- `λ_exec_buy`, `λ_exec_sell`  (exec_buy = market buy consuming ask; exec_sell = market sell consuming bid)

A stable starting point:

- Add mean-reverts imbalance:
  - `λ_add_bid = base_L * (1 - I)`
  - `λ_add_ask = base_L * (1 + I)`
- Executes follow imbalance direction (pressure):
  - `λ_exec_sell = base_M * max(I, 0)`
  - `λ_exec_buy  = base_M * max(-I, 0)`
- Cancels proportional to queue size:
  - `λ_cancel_bid = base_C * q_bid_best`
  - `λ_cancel_ask = base_C * q_ask_best`

Clamp all intensities to `>= epsilon` (e.g., 1e-9) to avoid zero-total edge cases.  
Also guard against NaNs.

## 6.4 Sampler (CompetingIntensitySampler)

- `Δt = -log(U) / λ_total`
- Choose type by cumulative sum over λ_i / λ_total.

Use `std::mt19937_64` or PCG with explicit seeding.

## 6.5 Attribute sampler (UnitSizeAttributeSampler)

For v1:
- qty = 1 always

Price level sampling for ADD/CANCEL:
- pick side based on event type (bid/ask)
- pick level index `k` with probability proportional to `exp(-alpha * k)` (alpha configurable)
- map to `price_ticks` at that level

For EXECUTE:
- side indicates aggressor (BUY consumes ask; SELL consumes bid)
- price is best opposite price (optional; may log price as best level price)

## 6.6 Producer (QrsdpProducer)

Dependencies injected:
- `IOrderBook`
- `IIntensityModel`
- `IEventSampler`
- `IAttributeSampler`
- `IEventSink`

Implements `runSession(session, sink)`:
- seed book
- `t = 0`, `order_id = 1`
- loop until `t >= session_seconds`
- emit records via sink
- compute close = mid(best bid/ask) at end

---

# 7) Tests (required)

Use any test framework already in repo; otherwise use doctest or Catch2 (header-only). Keep tests fast.

## 7.1 Records layout
- `static_assert(trivially_copyable)`
- `static_assert(sizeof(EventRecord) == expected)`
- field sanity

## 7.2 Sampler sanity
- Exp mean:
  - sample N=200k with λ=50; mean ≈ 1/50 within tolerance (e.g., 2–5%)
- Categorical ratios:
  - intensities (10,20,30); sample N=200k; empirical proportions ≈ (1/6, 2/6, 3/6)

## 7.3 Book invariants
- Seed book; apply random adds/cancels/executes for N steps; assert:
  - bid < ask
  - spread >= 1 tick
  - no negative depth

## 7.4 Producer determinism
- Run producer twice with same seed for 10 seconds; compare first 10k records equal.
- Run with different seed; streams differ.

## 7.5 Integration test
- session_seconds = 5–10
- assert events_written > 0
- assert close is between best bid/ask mid-ish (or simply bid<ask)

---

# 8) Recommended implementation order (keep compiling)

1. Create `Records.hpp` + `EventTypes.hpp`
2. Add minimal test framework + records layout test
3. Implement interfaces headers
4. Implement MultiLevelBook + invariants test
5. Implement intensity model + non-negative test
6. Implement sampler + distribution tests
7. Implement attribute sampler + basic test
8. Implement producer + determinism + integration tests

---

# 9) Repository layout suggestion (minimal)

Adapt to your repo, but keep separation:

```
/producer
  /include
    Records.hpp
    EventTypes.hpp
    IOrderBook.hpp
    IIntensityModel.hpp
    IEventSampler.hpp
    IAttributeSampler.hpp
    IEventSink.hpp
    IProducer.hpp
  /src
    MultiLevelBook.cpp
    SimpleImbalanceIntensity.cpp
    CompetingIntensitySampler.cpp
    UnitSizeAttributeSampler.cpp
    QrsdpProducer.cpp
    InMemorySink.cpp
  /tests
    test_records.cpp
    test_sampler.cpp
    test_book.cpp
    test_intensity.cpp
    test_producer.cpp
```

---

# 10) Cursor/LLM Agent Master Prompt (use this)

Paste into Cursor Agent:

> Read `docs/00_unified_build_plan.md`. Implement v1 exactly as specified. Do not implement out-of-scope items. Work in the recommended order and ensure all tests pass after each step. Summarize changes and how to run tests.

---

# End

This document defines a complete, simplified v1 implementation plan for a deterministic single-session QRSDP-style producer with tests, designed to be extended later (daily loop, file sink, regimes, Hawkes, ITCH).
