# QRSDP Mechanics: How It Works and Where

This document is a **method-level breakdown** of the QRSDP producer: what runs, in what order, and which functions do what.

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

**File:** `src/qrsdp/qrsdp_producer.cpp`  
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
| 1 | Book | **`book_->features()`** | → `BookFeatures f` (best_bid_ticks, best_ask_ticks, q_bid_best, q_ask_best, spread_ticks, imbalance) |
| 2 | Intensity model | **`intensityModel_->compute(f)`** | → `Intensities` (add_bid, add_ask, cancel_bid, cancel_ask, exec_buy, exec_sell) |
| 3 | Event sampler | **`eventSampler_->sampleDeltaT(intens.total())`** | → `dt` (seconds to next event) |
| 4 | Producer | `t_ += dt`; if `t_ >= session_seconds_` return false | — |
| 5 | Event sampler | **`eventSampler_->sampleType(intens)`** | → `EventType` (one of ADD_BID, ADD_ASK, CANCEL_BID, CANCEL_ASK, EXECUTE_BUY, EXECUTE_SELL) |
| 6 | Attribute sampler | **`attributeSampler_->sample(type, *book_, f)`** | → `EventAttrs` (side, price_ticks, qty=1, order_id=0) |
| 7 | Producer | Build `SimEvent` from type + attrs + `order_id_++` | — |
| 8 | Book | **`book_->apply(ev)`** | Updates depths (and may call shift if best level goes to 0) |
| 9 | Producer | Build `EventRecord`, **`sink.append(rec)`**, `++events_written_` | — |

So the **actual QRSDP logic** is: **features → compute → sampleDeltaT → sampleType → sample (attrs) → apply → append**.

---

## 5. Components and their methods

### 5.1 Order book — `IOrderBook` / `MultiLevelBook`

**Files:** `src/qrsdp/i_order_book.h`, `src/qrsdp/multi_level_book.cpp` (and `.h`)

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

### 5.2 Intensity model — `IIntensityModel` / `SimpleImbalanceIntensity`

**Files:** `src/qrsdp/i_intensity_model.h`, `src/qrsdp/simple_imbalance_intensity.cpp` (and `.h`)

| Method | Where | What it does |
|--------|--------|---------------|
| **`compute(BookFeatures f)`** | `SimpleImbalanceIntensity::compute` | Computes six rates from `f` and `params_`: **add** = base_L×(1∓I), **execute** = base_M×(epsilon_exec + max(±I,0)), **cancel** = base_C×q_bid/q_ask; each is passed through **`clampNonNegative`** (no NaN/neg, minimum ε) and returned as **`Intensities`**. |

So **all intensity formulas** live in this one method; the producer only calls **`compute(f)`** and **`intens.total()`** (and the sampler uses **`intens.at(type)`**).

---

### 5.3 Event sampler — `IEventSampler` / `CompetingIntensitySampler`

**Files:** `src/qrsdp/i_event_sampler.h`, `src/qrsdp/competing_intensity_sampler.cpp` (and `.h`)

| Method | Where | What it does |
|--------|--------|---------------|
| **`sampleDeltaT(lambdaTotal)`** | `CompetingIntensitySampler::sampleDeltaT` | Draws one uniform U, clamps to [kMinU, 1), returns **Δt = −ln(U) / lambdaTotal** (exponential inter-arrival time). |
| **`sampleType(Intensities intens)`** | `CompetingIntensitySampler::sampleType` | Draws one uniform U; cumulative sum over the six types in order (ADD_BID, ADD_ASK, CANCEL_BID, CANCEL_ASK, EXECUTE_BUY, EXECUTE_SELL); returns the first type for which U < cum/total. So type i is chosen with probability λ_i / λ_total. |

So **when** the next event occurs and **which type** are both determined here; the producer only calls these two methods.

---

### 5.4 Attribute sampler — `IAttributeSampler` / `UnitSizeAttributeSampler`

**Files:** `src/qrsdp/i_attribute_sampler.h`, `src/qrsdp/unit_size_attribute_sampler.cpp` (and `.h`)

| Method | Where | What it does |
|--------|--------|---------------|
| **`sample(EventType type, IOrderBook& book, BookFeatures f)`** | `UnitSizeAttributeSampler::sample` | Returns **`EventAttrs`** (side, price_ticks, qty=1, order_id=0). **ADD_BID/ADD_ASK:** level k from **`sampleLevelIndex(book.numLevels())`** (weight ∝ exp(−α·k)), then price = **book.bidPriceAtLevel(k)** or ask. **CANCEL_BID/CANCEL_ASK:** level from **`sampleCancelLevelIndex(is_bid, book)`** (weight = depth at level), then price at that level. **EXECUTE_BUY:** side=ASK, price=**f.best_ask_ticks**. **EXECUTE_SELL:** side=BID, price=**f.best_bid_ticks**. |
| **`sampleLevelIndex(num_levels)`** | private | Weights exp(−α·k), categorical draw → level index for adds. |
| **`sampleCancelLevelIndex(is_bid, book)`** | private | Weights = depth at each level (from **book.bidDepthAtLevel(k)** / **askDepthAtLevel(k)**), categorical draw → level index for cancels (only non-empty levels get weight). |

So **where** (which price/level) and **qty=1** are determined here; the producer only calls **`sample(type, book, f)`**.

---

### 5.5 RNG — `IRng` / `Mt19937Rng`

**Files:** `src/qrsdp/irng.h`, `src/qrsdp/mt19937_rng.cpp` (and `.h`)

| Method | Purpose |
|--------|---------|
| **`seed(uint64_t)`** | Reseed the generator (called once at startSession). |
| **`uniform()`** | Return next uniform in [0, 1) (used by event sampler and attribute sampler). |

Determinism: same session seed → same sequence of events.

---

### 5.6 Sink — `IEventSink` / `InMemorySink`

**Files:** `src/qrsdp/i_event_sink.h`, `src/qrsdp/in_memory_sink.cpp` (and `.h`)

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
| Session setup, event loop, append to sink | **QrsdpProducer::startSession**, **stepOneEvent**, **runSession** — `qrsdp_producer.cpp` |
| Book state, spread, imbalance, apply, shift | **MultiLevelBook::seed**, **features**, **apply**, **shiftBidBook** / **shiftAskBook** — `multi_level_book.cpp` |
| All six λ formulas, clamp | **SimpleImbalanceIntensity::compute** (and **clampNonNegative**) — `simple_imbalance_intensity.cpp` |
| Time to next event (exponential) | **CompetingIntensitySampler::sampleDeltaT** — `competing_intensity_sampler.cpp` |
| Which event type (categorical) | **CompetingIntensitySampler::sampleType** — `competing_intensity_sampler.cpp` |
| Price/level for add and cancel, best for execute | **UnitSizeAttributeSampler::sample** (and **sampleLevelIndex**, **sampleCancelLevelIndex**) — `unit_size_attribute_sampler.cpp` |
| Deterministic randomness | **Mt19937Rng::seed**, **uniform** — `mt19937_rng.cpp` |

That’s the full QRSDP flow and the methods that implement it.
