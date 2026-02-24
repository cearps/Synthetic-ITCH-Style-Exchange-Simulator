# QRSDP Mechanics: How It Works and Where

This document is a **method-level breakdown** of the QRSDP producer: what runs, in what order, and which functions do what. The theoretical foundation is the queue-reactive model of Huang, Lehalle & Rosenbaum (2015) [[1]](#references); for context on how this approach relates to the broader LOB simulation literature, see the survey by Jain et al. (2024) [[2]](#references).

---

## 1. Overview in one sentence

The producer runs a **continuous-time competing-risk loop**: at each step it reads the current order book state, computes six **intensities** (rates for add-bid, add-ask, cancel-bid, cancel-ask, execute-buy, execute-sell), samples the **time to next event** (exponential) and **which event type** (categorical), samples **attributes** (price level or best, qty=1), **applies** the event to the book, and **appends** an `EventRecord` to the sink; when the best level is depleted, the book **shifts** (best price moves). All of this is driven by a single **event loop** inside the producer.

---

## 2. High-level data flow

```
TradingSession (config) ──► Producer.startSession() or runSession()
                                    │
                                    ▼
                    ┌───────────────────────────────────┐
                    │  RNG.seed(session.seed)           │
                    │  Book.seed(BookSeed from session)  │
                    │  t=0, order_id=1                   │
                    └───────────────────────────────────┘
                                    │
          ┌─────────────────────────┴─────────────────────────┐
          │  EVENT LOOP (stepOneEvent or runSession’s while)    │
          │                                                     │
          │  1. Book.features()     → BookFeatures f            │
          │  2. Intensity.compute(f)→ Intensities (6 λ’s)      │
          │  3. Sampler.sampleDeltaT(λ_total) → dt              │
          │  4. t += dt; if t ≥ session_seconds → stop          │
          │  5. Sampler.sampleType(intens)    → EventType       │
          │  6. AttrSampler.sample(type, book, f) → EventAttrs  │
          │  7. Build SimEvent; Book.apply(ev)                 │
          │  8. Sink.append(EventRecord)                       │
          └─────────────────────────────────────────────────────┘
                                    │
                                    ▼
                    SessionResult{ close_ticks, events_written }
```

---

## 3. Entry points: producer

**File:** `src/producer/qrsdp_producer.cpp`  
**Class:** `QrsdpProducer` (implements `IProducer`)

| Method | Purpose |
|--------|--------|
| **`runSession(session, sink)`** | Full run: calls `startSession(session)`, then `while (stepOneEvent(sink)) {}`, then returns `SessionResult{ close_ticks, events_written }`. |
| **`startSession(session)`** | One-time setup: seeds RNG from `session.seed`, builds `BookSeed` from session (p0, levels, initial_depth, initial_spread_ticks), calls `book_->seed(seed)`, sets `t_ = 0`, `order_id_ = 1`, `events_written_ = 0`, `session_seconds_`. |
| **`stepOneEvent(sink)`** | One iteration of the event loop (see §5). Returns `true` if an event was produced, `false` if past session end. |
| **`currentTime()`** | Returns current simulation time `t_` (seconds). |
| **`eventsWrittenThisSession()`** | Returns `events_written_`. |

**Constructor:** Takes references to `IRng`, `IOrderBook`, `IIntensityModel`, `IEventSampler`, `IAttributeSampler`; stores pointers. No logic, just wiring.

---

## 4. One-event step (method chain)

What happens inside **one** call to `stepOneEvent(sink)`:

| Step | Component | Method | In / out |
|------|-----------|--------|----------|
| 1 | Book | **`book_->features()`** + depth queries | → `BookState` (features + per-level depths) |
| 2 | Intensity model | **`intensityModel_->compute(state)`** | → `Intensities` (add_bid, add_ask, cancel_bid, cancel_ask, exec_buy, exec_sell) |
| 3 | Event sampler | **`eventSampler_->sampleDeltaT(intens.total())`** | → `dt` (seconds to next event) |
| 4 | Producer | `t_ += dt`; if `t_ >= session_seconds_` return false | — |
| 5a | Per-level (HLR) | **`eventSampler_->sampleIndexFromWeights(per_level)`** | → index decoded to `(EventType, level_hint)` via `decodePerLevelIndex` |
| 5b | Aggregate (Simple) | **`eventSampler_->sampleType(intens)`** | → `EventType` (categorical over 6 types) |
| 6 | Attribute sampler | **`attributeSampler_->sample(type, *book_, f, level_hint)`** | → `EventAttrs` (side, price_ticks, qty=1) |
| 7 | Producer | Build `SimEvent` from type + attrs + `order_id_++` | — |
| 8 | Book | **`book_->apply(ev)`** | Updates depths (and may shift if best level goes to 0) |
| 8a | Producer | If shift occurred and `theta_reinit > 0`: probabilistic book **`reinitialize()`** | — |
| 9 | Producer | Build `EventRecord`, **`sink.append(rec)`**, `++events_written_` | — |

Step 5 differs by model: the HLR model provides per-level weights (4K+2 entries for K levels) that jointly determine the event type *and* target level, while SimpleImbalance samples the type from 6 aggregate rates and lets the attribute sampler choose the level independently.

---

## 5. Components and their methods

### 5.1 Order book — `IOrderBook` / `MultiLevelBook`

**Files:** `src/book/i_order_book.h`, `src/book/multi_level_book.cpp` (and `.h`)

| Method                                          | Where                      | What it does                                                                                                                                                                                                                                                                                 |
| ----------------------------------------------- | -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **`seed(BookSeed s)`**                          | `MultiLevelBook::seed`     | Sets `num_levels_`, `initial_depth_`, and fills bid/ask level arrays: best_bid = p0 − half_spread, best_ask = p0 + (spread − half); each level k has price and depth = initial_depth_.                                                                                                       |
| **`features()`**                                | `MultiLevelBook::features` | Returns `BookFeatures`: best_bid_ticks, best_ask_ticks, q_bid_best, q_ask_best, spread_ticks, and **imbalance** = (q_bid − q_ask) / (q_bid + q_ask + ε).                                                                                                                                     |
| **`apply(SimEvent e)`**                         | `MultiLevelBook::apply`    | Dispatches on `e.type`: ADD_BID/ADD_ASK add qty to the level matching `e.price_ticks`; CANCEL_* subtract (clamped to 0); EXECUTE_BUY decrements ask_levels_[0], EXECUTE_SELL decrements bid_levels_[0]; if that level’s depth becomes 0, calls **`shiftAskBook()`** or **`shiftBidBook()`**. |
| **`bestBid()` / `bestAsk()`**                   | `MultiLevelBook`           | Return `Level{ bid_levels_[0].price_ticks, depth }` (and same for ask).                                                                                                                                                                                                                      |
| **`numLevels()`**                               | `MultiLevelBook`           | Returns `num_levels_`.                                                                                                                                                                                                                                                                       |
| **`bidPriceAtLevel(k)` / `askPriceAtLevel(k)`** | `MultiLevelBook`           | Return price at level index k (for attribute sampler).                                                                                                                                                                                                                                       |
| **`bidDepthAtLevel(k)` / `askDepthAtLevel(k)`** | `MultiLevelBook`           | Return depth at level k (used for cancel level sampling).                                                                                                                                                                                                                                    |
| **`shiftBidBook()` / `shiftAskBook()`**         | `MultiLevelBook` (private) | Shift levels: copy level i+1 → i, then set last level to new price (one tick worse) and depth = initial_depth_. So best moves by one tick when best level is depleted.                                                                                                                       |

**Helper (private):** `bidIndexForPrice(price)` / `askIndexForPrice(price)` — map price to level index for apply().

---

### 5.2 Intensity models — `IIntensityModel`

**Files:** `src/model/i_intensity_model.h`, `src/model/simple_imbalance_intensity.*`, `src/model/curve_intensity_model.*`

Both models implement `compute(BookState)` → `Intensities` (six rates). The producer only calls `compute()` and `intens.total()`.

#### SimpleImbalanceIntensity (legacy)

| Method | What it does |
|--------|---------------|
| **`compute(state)`** | Six rates from imbalance I and total depths: **add** = base_L×(1∓sI×I)×spread_mult, **execute** = base_M×(ε + max(±sI×I, 0))×spread_mult, **cancel** = base_C×sC×total_depth. Spread-dependent multipliers: add boosted, exec dampened when spread > 2. |

#### CurveIntensityModel (HLR2014)

| Method | What it does |
|--------|---------------|
| **`compute(state)`** | Per-level intensity from lookup tables: **add** λ^L_i(n), **cancel** λ^C_i(n) for each level i, **market** λ^M(n) at best only. Applies spread-dependent and imbalance-driven multipliers. Returns aggregate sums as `Intensities`, and stores per-level weights for joint type+level sampling. |
| **`getPerLevelIntensities(weights_out)`** | Fills a 4K+2 vector of per-(type, level) intensities from the last `compute()`. The event sampler draws from these weights to jointly select the event type and target level. |
| **`decodePerLevelIndex(idx, K, type, level)`** | Static: maps a flat index [0..4K+1] back to `(EventType, level)`. |

The HLR model's per-level sampling ensures that the level targeted by an add or cancel is chosen proportionally to the intensity at that level, rather than independently by the attribute sampler.

---

### 5.3 Event sampler — `IEventSampler` / `CompetingIntensitySampler`

**Files:** `src/sampler/i_event_sampler.h`, `src/sampler/competing_intensity_sampler.cpp` (and `.h`)

| Method | Where | What it does |
|--------|--------|---------------|
| **`sampleDeltaT(lambdaTotal)`** | `CompetingIntensitySampler::sampleDeltaT` | Draws one uniform U, clamps to [kMinU, 1), returns **Δt = −ln(U) / lambdaTotal** (exponential inter-arrival time). |
| **`sampleType(Intensities intens)`** | `CompetingIntensitySampler::sampleType` | Draws one uniform U; cumulative sum over the six types in order (ADD_BID, ADD_ASK, CANCEL_BID, CANCEL_ASK, EXECUTE_BUY, EXECUTE_SELL); returns the first type for which U < cum/total. So type i is chosen with probability λ_i / λ_total. Used by SimpleImbalance. |
| **`sampleIndexFromWeights(weights)`** | `CompetingIntensitySampler::sampleIndexFromWeights` | Categorical draw from an arbitrary weight vector (used by HLR for joint type+level selection). |

For SimpleImbalance the producer calls `sampleType`; for HLR it calls `sampleIndexFromWeights` on the per-level weights, then decodes the index to `(EventType, level_hint)`.

---

### 5.4 Attribute sampler — `IAttributeSampler` / `UnitSizeAttributeSampler`

**Files:** `src/sampler/i_attribute_sampler.h`, `src/sampler/unit_size_attribute_sampler.cpp` (and `.h`)

| Method | Where | What it does |
|--------|--------|---------------|
| **`sample(type, book, f, level_hint)`** | `UnitSizeAttributeSampler::sample` | Returns **`EventAttrs`** (side, price_ticks, qty=1). **ADD_BID/ADD_ASK:** first checks for spread improvement (if spread > 1 and `spread_improve_coeff > 0`, may place the add inside the spread with probability min(1, (spread−1)×coeff)); otherwise uses `level_hint` if provided by HLR, or samples level k ∝ exp(−α·k). **CANCEL_BID/CANCEL_ASK:** uses `level_hint` or samples by depth weight. **EXECUTE_BUY/SELL:** always at best price. |
| **`sampleLevelIndex(num_levels)`** | private | Weights exp(−α·k), categorical draw → level index for adds. |
| **`sampleCancelLevelIndex(is_bid, book)`** | private | Weights = depth at each level, categorical draw → level index for cancels. |

When the HLR model provides a `level_hint`, the sampler uses it directly for level selection (but spread improvement can still override it for adds). When `level_hint == kLevelHintNone` (SimpleImbalance), the sampler chooses the level independently.

---

### 5.5 RNG — `IRng` / `Mt19937Rng`

**Files:** `src/rng/irng.h`, `src/rng/mt19937_rng.cpp` (and `.h`)

| Method | Purpose |
|--------|---------|
| **`seed(uint64_t)`** | Reseed the generator (called once at startSession). |
| **`uniform()`** | Return next uniform in [0, 1) (used by event sampler and attribute sampler). |

Determinism: same session seed → same sequence of events.

---

### 5.6 Sink — `IEventSink` / `InMemorySink`

**Files:** `src/io/i_event_sink.h`, `src/io/in_memory_sink.cpp` (and `.h`)

| Method | Purpose |
|--------|---------|
| **`append(EventRecord rec)`** | Append one record (producer calls this after each event). |

In-memory impl stores records in a `std::vector<EventRecord>`.

---

## 6. Key data types (records.h)

| Type | Role |
|------|------|
| **`TradingSession`** | Input: seed, p0_ticks, session_seconds, levels_per_side, tick_size, initial_spread_ticks, initial_depth, **intensity_params** (base_L, base_C, base_M, epsilon_exec, …). |
| **`BookSeed`** | Input to **Book.seed**: p0_ticks, levels_per_side, initial_depth, initial_spread_ticks. |
| **`BookFeatures`** | Output of **Book.features**: best_bid_ticks, best_ask_ticks, q_bid_best, q_ask_best, spread_ticks, imbalance. |
| **`Intensities`** | Output of **Intensity.compute**: add_bid, add_ask, cancel_bid, cancel_ask, exec_buy, exec_sell; **total()**, **at(EventType)**. |
| **`SimEvent`** | Internal event: type, side, price_ticks, qty, order_id — input to **Book.apply**. |
| **`EventAttrs`** | Output of **AttributeSampler.sample**: side, price_ticks, qty, order_id (producer overwrites order_id). |
| **`EventRecord`** | Output to sink: ts_ns, type, side, price_ticks, qty, order_id, flags. |
| **`SessionResult`** | Output of **runSession**: close_ticks, events_written. |

---

## 7. Summary: “where does X happen?”

| What | Where (method / file) |
|------|------------------------|
| Session setup, event loop, sink | **QrsdpProducer** — `src/producer/qrsdp_producer.cpp` |
| Book state, apply, shift | **MultiLevelBook** — `src/book/multi_level_book.cpp` |
| SimpleImbalance intensity formulas | **SimpleImbalanceIntensity::compute** — `src/model/simple_imbalance_intensity.cpp` |
| HLR curve-based intensities + per-level weights | **CurveIntensityModel::compute** — `src/model/curve_intensity_model.cpp` |
| HLR default curves, JSON I/O | **makeDefaultHLRParams**, **save/loadHLRParamsToJson** — `src/model/hlr_params.cpp` |
| Time to next event (exponential) | **CompetingIntensitySampler::sampleDeltaT** — `src/sampler/competing_intensity_sampler.cpp` |
| Event type selection (aggregate or per-level) | **sampleType** / **sampleIndexFromWeights** — `src/sampler/competing_intensity_sampler.cpp` |
| Price/level, spread improvement | **UnitSizeAttributeSampler::sample** — `src/sampler/unit_size_attribute_sampler.cpp` |
| Deterministic randomness | **Mt19937Rng::seed**, **uniform** — `src/rng/mt19937_rng.cpp` |
| Calibration from event logs | **IntensityEstimator** — `src/calibration/intensity_estimator.cpp` |

That's the full QRSDP flow and the methods that implement it.

---

## References

1. W. Huang, C.-A. Lehalle, and M. Rosenbaum, "Simulating and Analyzing Order Book Data: The Queue-Reactive Model," *Journal of the American Statistical Association*, vol. 110, no. 509, pp. 107–122, 2015. [arXiv:1312.0563](https://arxiv.org/abs/1312.0563)

2. K. Jain, N. Firoozye, J. Kochems, and P. Treleaven, "Limit Order Book Simulations: A Review," *arXiv preprint*, 2024. [arXiv:2402.17359](https://arxiv.org/abs/2402.17359)
