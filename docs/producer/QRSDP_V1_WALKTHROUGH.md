# QRSDP v1 Implementation Walkthrough

This document is a guided tour of what was implemented from `docs/producer/new documents/00_unified_build_plan.md`, written for someone with basic–intermediate C++ knowledge.

---

## 1) Plan vs implementation: deviations

| Plan | Implementation | Why |
|------|----------------|-----|
| **Event types:** Plan §3 says “ADD_LIMIT, CANCEL, EXECUTE” (3 kinds). | We use **6** enum values: `ADD_BID`, `ADD_ASK`, `CANCEL_BID`, `CANCEL_ASK`, `EXECUTE_BUY`, `EXECUTE_SELL`. | The plan’s intensity model (§6.3) has six rates (λ_add_bid, λ_add_ask, …). The categorical sampler must pick one of six outcomes; so we have six event types for the competing-risk loop. The three “kinds” are still there (add/cancel/execute), split by side/aggressor. |
| **IOrderBook:** Plan lists only `seed`, `features`, `apply`, `bestBid`, `bestAsk`. | We added `numLevels()`, `bidPriceAtLevel(k)`, `askPriceAtLevel(k)`. | The attribute sampler must choose a **level index** k and then get the **price** at that level. The plan says “map to price_ticks at that level”; that requires the book to expose “price at level k”. So we extended the interface; `MultiLevelBook` implements these. |
| **IProducer dependencies:** Plan says producer gets Book, Model, Sampler, AttributeSampler, Sink. | We also inject **IRng** and the producer calls `rng.seed(session.seed)` at the start of `runSession`. | Determinism requires the RNG to be reseeded per session. The producer is the only place that sees `TradingSession.seed` and runs the session, so it seeds the RNG there. The plan says “RNG must be injected and seed-controlled”; having the producer seed it keeps the same API (one `runSession` call) and guarantees same seed ⇒ same stream. |
| **EventRecord size:** Plan says “stable layout (static_assert size)” but does not give a number. | We use **30 bytes** and `static_assert(sizeof(EventRecord) == 30)`. | With `#pragma pack(push, 1)`: 8+1+1+4+4+8+4 = 30. We fixed the size so the layout is portable and testable. |
| **Repository layout:** Plan suggests `/producer/include` and `/producer/src`. | We put everything under **`src/qrsdp/`** and **`tests/qrsdp/`**. | The repo already had `src/core`, `src/producer`, etc., and a single `include_directories(src)`. We kept that convention and put all QRSDP headers and sources under `src/qrsdp/` so no CMake or include-path changes were needed beyond adding files. |
| **BookSeed / initial_depth:** Plan does not put `initial_depth` in `TradingSession`. | `TradingSession` has **`initial_depth`** (0 = use producer default 50) and **`initial_spread_ticks`** (0 = default 2). | Session-level control for tests and configs; producer passes them into `BookSeed` when seeding the book. |
| **IEventSink:** Plan says v1 has only `append(EventRecord)`. | We implemented exactly that. No `openDay`/`flush`/`close` in the interface. | Plan §5.5 says v1 sink is in-memory with just `append`; we did not add file-style lifecycle. |

Everything else (continuous-time loop, exponential + categorical sampling, intensity formulas, apply rules, shift-when-depleted, unit size, no file I/O in producer, tests) matches the plan.

---

## 2) High-level architecture: files and components

All QRSDP code lives under **`src/qrsdp/`** and **`tests/qrsdp/`**. The main executable (`itch_simulator`) does **not** use the QRSDP producer yet; it still uses the older exchange simulator. The new code is used only by the test binary.

**Interfaces (headers only; no .cpp):**

| File | Interface | Purpose |
|------|-----------|--------|
| `irng.h` | `IRng` | Deterministic RNG: `uniform()`, `seed(uint64_t)`. |
| `i_order_book.h` | `IOrderBook` | Book state: `seed`, `features`, `apply`, `bestBid`, `bestAsk`, `numLevels`, `bidPriceAtLevel`, `askPriceAtLevel`. |
| `i_intensity_model.h` | `IIntensityModel` | `Intensities compute(BookFeatures)`. |
| `i_event_sampler.h` | `IEventSampler` | `sampleDeltaT(λ_total)`, `sampleType(Intensities)`. |
| `i_attribute_sampler.h` | `IAttributeSampler` | `EventAttrs sample(EventType, IOrderBook, BookFeatures)`. |
| `i_event_sink.h` | `IEventSink` | `append(EventRecord)`. |
| `i_producer.h` | `IProducer` | `SessionResult runSession(TradingSession, IEventSink)`. |

**Data and types (no .cpp):**

| File | Contents |
|------|----------|
| `event_types.h` | Enums `EventType` (6 values), `Side` (BID/ASK/NA). |
| `records.h` | `EventRecord`, `TradingSession`, `SessionResult`, `BookSeed`, `BookFeatures`, `Intensities`, `Level`, `SimEvent`, `EventAttrs`, `IntensityParams`. |

**Implementations (.h + .cpp):**

| File | Implements | Depends on |
|------|------------|------------|
| `multi_level_book.*` | `IOrderBook` | records, event_types |
| `simple_imbalance_intensity.*` | `IIntensityModel` | records |
| `mt19937_rng.*` | `IRng` | — |
| `competing_intensity_sampler.*` | `IEventSampler` | records, irng |
| `unit_size_attribute_sampler.*` | `IAttributeSampler` | records, i_order_book, irng |
| `in_memory_sink.*` | `IEventSink` | records |
| `qrsdp_producer.*` | `IProducer` | all of the above |

**Data flow (conceptual):**  
`QrsdpProducer::runSession` uses: **IRng** (seeded once), **IOrderBook** (seed + apply), **IIntensityModel** (compute λ from features), **IEventSampler** (Δt and event type), **IAttributeSampler** (side, price, qty), **IEventSink** (append records). The producer never touches files or concrete types beyond the interfaces.

---

## 3) Data model: EventRecord, TradingSession, SessionResult

### EventRecord (persisted event)

```cpp
#pragma pack(push, 1)
struct EventRecord {
    uint64_t ts_ns;       // nanoseconds since session start
    uint8_t  type;        // EventType (0..5)
    uint8_t  side;        // Side (BID/ASK/NA)
    int32_t  price_ticks;
    uint32_t qty;         // always 1 in v1
    uint64_t order_id;    // monotonic
    uint32_t flags;       // reserved
};
#pragma pack(pop)
```

- **Packed:** `#pragma pack(push, 1)` removes padding so the struct is 30 bytes and layout is stable across compilers. That matters if you ever write these to a binary file or send them on a wire.
- **ts_ns:** Lets you order events in time. The producer uses `t` in seconds and sets `ts_ns = (uint64_t)(t * 1e9)`.
- **type / side:** `type` is the 6-way event (ADD_BID, ADD_ASK, …); `side` is redundant for some (e.g. ADD_BID implies BID) but stored for consistency and future decoupling.
- **order_id:** Assigned by the producer (monotonic counter) so each event has a unique id; useful for replay and debugging.
- **flags:** Reserved (e.g. “seed order” or future use); v1 always 0.

### TradingSession (input to runSession)

- **seed:** Used to seed the RNG at the start of the session so runs are reproducible.
- **p0_ticks:** Reference mid at session start; the book is seeded using `initial_spread_ticks` (see §4).
- **session_seconds:** Simulation runs while `t < session_seconds`.
- **levels_per_side:** Number of price levels on each side of the book (e.g. 5 or 10).
- **tick_size:** Metadata (e.g. 100 = 1 cent); the sim uses integer ticks only.
- **initial_spread_ticks:** Spread at t=0. Default 2 ⇒ best_bid = p0−1, best_ask = p0+1. If 0, the producer uses 2. Configurable so tests and sessions can use other spreads.
- **initial_depth:** Depth per level at seed. If 0, the producer uses its default (50).
- **intensity_params:** `base_L`, `base_C`, `base_M`, etc., passed into the intensity model when you construct it; the producer does not read these (the model is preconfigured).

### SessionResult (output)

- **close_ticks:** Mid at session end, `(best_bid + best_ask) / 2`.
- **events_written:** Number of `EventRecord`s appended to the sink (one per loop iteration).

---

## 4) Order book: seeding, storage, apply, and price movement

### Seeding

`MultiLevelBook::seed(BookSeed s)`:

1. Sets `num_levels_ = min(s.levels_per_side, kMaxLevels)` (capped at 64).
2. Sets `initial_depth_` from `s.initial_depth` (default 50 if 0).
3. Uses **`s.initial_spread_ticks`** (if 0, treated as 2). Defines:
   - `half = spread / 2`, then `best_bid = p0_ticks - half`, `best_ask = p0_ticks + (spread - half)`.
   So spread at t=0 equals `initial_spread_ticks` (e.g. 2 ⇒ best_bid = p0−1, best_ask = p0+1).
4. For `k = 0 .. num_levels_-1`:
   - Bid level k: price = `best_bid - k`, depth = `initial_depth_`.
   - Ask level k: price = `best_ask + k`, depth = `initial_depth_`.

So level 0 is always the best (tightest) level on each side. Initial spread is configurable via `BookSeed::initial_spread_ticks` (and `TradingSession::initial_spread_ticks` when the producer builds the seed).

### How levels are stored

- Two fixed-size arrays: `bid_levels_[0..kMaxLevels-1]`, `ask_levels_[0..kMaxLevels-1]`, each slot has `price_ticks` and `depth`.
- Only the first `num_levels_` entries are valid. Index 0 = best bid / best ask.
- No heap allocation in the hot path; everything is in `std::array` (and a small weight buffer in the attribute sampler).

### apply(SimEvent)

- **ADD_BID / ADD_ASK:** Find the level index for `e.price_ticks` (e.g. for bid: `idx = best_bid - price_ticks`). If `idx` is in range, add `e.qty` to that level’s depth.
- **CANCEL_BID / CANCEL_ASK:** Same index lookup; subtract `e.qty` from that level’s depth, clamped at 0 (no negative depth).
- **EXECUTE_BUY:** Subtract 1 from **best ask** (index 0). If that depth becomes 0, call `shiftAskBook()`.
- **EXECUTE_SELL:** Subtract 1 from **best bid** (index 0). If that depth becomes 0, call `shiftBidBook()`.

If the event’s price is outside the current book (e.g. add at a price not in the window), the index is out of range and we do nothing; the attribute sampler is designed to only pick valid levels.

### What happens when the top level is depleted (shift)

When best bid depth goes to 0 (after a cancel or execute):

1. **shiftBidBook()** copies levels “up”: `bid_levels_[i] = bid_levels_[i+1]` for `i = 0 .. num_levels_-2`.
2. The **new** last level is set to: price = (old level at `num_levels_-2`) − 1, depth = `initial_depth_`.

So the best bid **moves down one tick** and a new far level appears. Same idea for ask: **shiftAskBook()** moves the best ask **up** one tick and refills the far level. That’s how price moves in the simulation: queue depletion at the best level moves the mid.

**Gotcha:** After a shift, the “best” is still at index 0; we don’t swap indices, we shift the whole array and refill the last slot.

---

## 5) Features: spread and imbalance

`MultiLevelBook::features()` returns a `BookFeatures`:

- **best_bid_ticks, best_ask_ticks:** `bid_levels_[0].price_ticks`, `ask_levels_[0].price_ticks`.
- **q_bid_best, q_ask_best:** Depths at those levels.
- **spread_ticks:** `best_ask_ticks - best_bid_ticks` (≥ 1 by construction).
- **imbalance:**  
  `I = (q_bid_best - q_ask_best) / (q_bid_best + q_ask_best + eps)`  
  with `eps = 1e-9` so we never divide by zero. I is in [-1, 1]: positive when the bid side has more size at the best.

The intensity model uses I to push add/cancel/execute rates; the eps is the only safeguard so the denominator is always positive.

---

## 6) Intensity model: formulas and clamping

`SimpleImbalanceIntensity::compute(BookFeatures f)` returns six rates:

- **Add (mean-revert imbalance):**
  - `λ_add_bid = base_L * (1 - I)`
  - `λ_add_ask = base_L * (1 + I)`  
  So when the book is bid-heavy (I > 0), we add more on the ask and less on the bid.
- **Execute (pressure in direction of imbalance, plus baseline when I ≈ 0):**
  - `λ_exec_sell = base_M * (epsilon_exec + max(I, 0))`  (sell when bid-heavy)
  - `λ_exec_buy  = base_M * (epsilon_exec + max(-I, 0))` (buy when ask-heavy)
  - `epsilon_exec` (default 0.05) keeps execution intensity nonzero when imbalance ≈ 0 so best-level depletion and shifts can occur.
- **Cancel (proportional to queue size):**
  - `λ_cancel_bid = base_C * q_bid_best`
  - `λ_cancel_ask = base_C * q_ask_best`

Each raw value is passed through `clampNonNegative(x)`: if `x` is NaN, inf, or negative, we return `1e-9`; otherwise we return `max(x, 1e-9)`. So every intensity is ≥ ε and λ_total is never 0, which avoids division-by-zero and degenerate sampling. The plan’s “guard against NaNs” is done here and in the imbalance denominator (eps).

**Parameters (IntensityParams):**  
`base_L` scales add rates; `base_C` scales cancel rates; `base_M` scales execute rates; `epsilon_exec` (default 0.05) is the baseline added to execute terms so λ_exec_* are nonzero when I ≈ 0. `imbalance_sensitivity` and `cancel_sensitivity` are in the struct but not used in this simple model (they’re there for future or alternate models).

---

## 7) Sampler: exponential Δt and categorical type

### Exponential inter-arrival time

The next event is assumed to occur after a random time Δt with **exponential distribution** with rate λ_total. So the mean time to the next event is 1/λ_total.

**Math:** If U is uniform in (0, 1], then `Δt = -ln(U) / λ_total` is exponential with rate λ_total. We use U from the RNG; to avoid U=0 (which would give ln(0) = -∞), we clamp U to at least `1e-10`.

**Code (conceptually):**

```cpp
double u = rng_->uniform();
if (u <= 0.0 || u >= 1.0) u = kMinU;  // 1e-10
if (u < kMinU) u = kMinU;
return -std::log(u) / lambdaTotal;
```

**Example:** λ_total = 50, U = 0.5 → Δt = -ln(0.5)/50 ≈ 0.0139 seconds. So we advance time by about 14 ms.

### Categorical event type

Given the six intensities λ_i and λ_total, we want to pick event type i with probability λ_i / λ_total. We draw one uniform U and do a cumulative sum:

1. Compute `total = λ_add_bid + λ_add_ask + … + λ_exec_sell`.
2. Draw U in [0, 1).
3. For each type in order (ADD_BID, ADD_ASK, …), add its λ to a running sum `cum`. When `U < cum / total`, return that type. If we finish the loop, return the last type (EXECUTE_SELL).

**Example:** Intensities (1, 2, 3, 4, 5, 6), total = 21. U = 0.3 → cum/total: 1/21≈0.048, 3/21≈0.143, 6/21≈0.286, 10/21≈0.476, … So we return the first type where 0.3 < cum/21, i.e. CANCEL_BID (cum=10).

---

## 8) Attribute sampler: level choice and side/price/qty per type

**Unit size:** Every event gets `qty = 1`. `order_id` is left 0 in the sampler; the producer assigns the real order_id.

**ADD_BID, ADD_ASK:**  
We sample a **level index** k with probability proportional to `exp(-alpha * k)` (so level 0 is most likely). Then `price_ticks = book.bidPriceAtLevel(k)` or `askPriceAtLevel(k)`. Side is fixed by type.

**CANCEL_BID, CANCEL_ASK:**  
We sample a **level index** k with probability **proportional to depth** at that level: only levels with depth &gt; 0 get weight; weight at k is `book.bidDepthAtLevel(k)` or `askDepthAtLevel(k)`. So cancel targets are never empty levels (except when all levels have depth 0, we fall back to level 0 and apply() keeps depth at 0). Implemented in `sampleCancelLevelIndex(is_bid, book)`; ADD still uses `sampleLevelIndex(num_levels)`.

**EXECUTE_BUY:** Consumes the ask side. We set `side = ASK`, `price_ticks = f.best_ask_ticks` (the level we hit).

**EXECUTE_SELL:** Consumes the bid side. We set `side = BID`, `price_ticks = f.best_bid_ticks`.

Level index is sampled with a **fixed-size buffer** (no vector per call): weights for k = 0..n-1 are written into a member `std::array<double, kAttrSamplerMaxLevels>`, then we do the same cumulative-sum trick with one U. So there’s no per-event heap allocation. The book interface exposes `bidDepthAtLevel(k)` and `askDepthAtLevel(k)` for cancel-weight computation.

---

## 9) Producer loop: runSession() step-by-step

**Pseudocode** (then mapped to real code):

```
1. rng.seed(session.seed)
2. book.seed(BookSeed from session: p0, levels_per_side, initial_depth=50))
3. t = 0, order_id = 1, events_written = 0
4. while t < session_seconds:
5.     f = book.features()
6.     intens = intensityModel.compute(f)
7.     λ_total = intens.total()
8.     Δt = eventSampler.sampleDeltaT(λ_total)
9.     t += Δt
10.    if t >= session_seconds: break
11.    type = eventSampler.sampleType(intens)
12.    attrs = attributeSampler.sample(type, book, f)
13.    ev = SimEvent(type, attrs.side, attrs.price_ticks, attrs.qty, order_id++)
14.    book.apply(ev)
15.    rec = EventRecord(ts_ns=t*1e9, type, side, price_ticks, qty, order_id, flags=0)
16.    sink.append(rec)
17.    events_written++
18. close_ticks = (book.bestBid().price_ticks + book.bestAsk().price_ticks) / 2
19. return SessionResult(close_ticks, events_written)
```

**Mapping to actual code:**

- 1–2: `qrsdp_producer.cpp` lines 19–26 (seed RNG, build BookSeed, call `book_->seed(seed)`).
- 3: lines 28–30 (`t`, `order_id`, `events_written`).
- 4: line 32 (`while (t < session_seconds)`).
- 5–7: lines 33–35 (`features()`, `compute(f)`, `total()`).
- 8–10: lines 37–39 (`sampleDeltaT`, `t += dt`, `break` if over).
- 11–12: lines 41–42 (`sampleType`, `sample(type, book, f)`).
- 13–14: lines 44–50 (build `SimEvent`, `book_->apply(ev)`).
- 15–17: lines 52–60 (build `EventRecord`, `sink.append(rec)`, `++events_written`).
- 18–19: lines 63–66 (close from best bid/ask mid, return `SessionResult`).

So one iteration = one event: we advance time, pick type and attributes, apply to the book, and append one record. No file I/O inside the producer; the sink is an abstraction (v1 = InMemorySink).

---

## 10) Sink: InMemorySink and no file I/O

**InMemorySink** implements `IEventSink` with a single method:

```cpp
void append(const EventRecord& rec) override {
    events_.push_back(rec);
}
```

It holds a `std::vector<EventRecord>`. The producer only calls `sink.append(rec)`; it never opens files, writes to disk, or flushes. So in v1 there is **no file I/O** in the producer or in the sink. If you later add a `BinaryFileSink`, it would implement the same `append()` and the producer code would not change.

---

## 11) Tests: what each file checks

| File | What it checks |
|------|----------------|
| **test_records.cpp** | `EventRecord` is trivially copyable; `sizeof(EventRecord) == 30`; `Intensities::total()` and `at(EventType)`; basic construction of TradingSession, SessionResult, BookFeatures, Level, SimEvent, EventAttrs. |
| **test_interfaces.cpp** | All interface headers compile and a minimal sanity check (e.g. EventRecord size). |
| **test_book.cpp** | After seed, features match expected (best bid/ask, spread, depths). Add then cancel restores depth. Execute buy/sell reduces ask/bid depth. After many mixed events, invariants (bid&lt;ask, spread≥1, no negative depth) hold. When best level is depleted, shift: best bid/ask moves by one tick and far level is refilled. |
| **test_intensity.cpp** | All six intensities ≥ ε; balanced book gives equal add_bid/add_ask; positive imbalance increases add_ask and decreases add_bid; cancel rates scale with queue size; extreme imbalance does not produce NaN. |
| **test_sampler.cpp** | **Exponential mean:** 200k samples with λ=50; sample mean within 5% of 1/50. **Categorical ratios:** Intensities (1,2,3,4,5,6); 200k samples; each type’s proportion within 5% of λ_i/21. **Determinism:** Same seed ⇒ same Δt and same type sequence for 100 draws. **Different seed:** Different seeds ⇒ at least one difference in 50 draws. **Δt positive and finite:** 1000 samples with λ=100; each Δt &gt; 0 and finite. |
| **test_attribute_sampler.cpp** | qty always 1; ADD_BID returns BID and a price in the bid range; EXECUTE_BUY returns ASK and best_ask_ticks; EXECUTE_SELL returns BID and best_bid_ticks; same seed ⇒ same sequence of attributes for ADD_BID. |
| **test_producer.cpp** | **Determinism:** Two runs with same session (same seed, 10 s); first N records (N = min(10k, size1, size2)) compared field-by-field; close_ticks and events_written equal. **Different seed:** Two runs with different seeds; at least one record differs. **Integration:** 5 s session; events_written &gt; 0; at end bid &lt; ask, spread ≥ 1; close in [best_bid, best_ask]; sink.size() == events_written. **InMemorySink:** append increases size and stored events match. **TraceShiftAndPrintFirst20:** Runs a short session (2 s, initial_depth=1), replays all events on a fresh book, prints the first 20 events and best bid/ask after each; asserts events &gt; 0 and after each event bid &lt; ask and spread ≥ 1. Shift-on-depletion is unit-tested in QrsdpBook.ShiftWhenBestDepleted. |

### Sampler tests: tolerance and flakiness

- **Tolerance:** We check that the **sample mean** of 200k exponential samples is within **5%** of the theoretical mean 1/λ. So for λ=50 we expect mean ≈ 0.02; we allow 0.019–0.021. Similarly for categorical: we expect proportion for type i to be λ_i/total; we allow up to 5% relative error (e.g. for 1/21 ≈ 0.0476 we accept roughly 0.045–0.050).
- **Why Monte Carlo tests can be flaky:** With a finite sample (200k), the observed mean and proportions are random. Very rarely they can fall outside the 5% band just by chance. Making the band wider (e.g. 10%) would reduce flakiness but weaken the test.
- **Reproducibility:** We use a **fixed seed** for the RNG in each test (e.g. 12345 for ExponentialMean, 67890 for CategoricalRatios). So every run of the test uses the same sequence of U’s and produces the same sample mean and counts. The test is therefore deterministic; “random” only in the sense that we’re testing a random-number-driven component.

### Determinism test: what is compared

We do **not** compare hashes or a single checksum. We compare **the first N event records field-by-field**, where:

- N = min(sink1.size(), sink2.size(), 10000).

So we compare at most the first 10,000 records. For each index i in [0, N), we check:

`ts_ns`, `type`, `side`, `price_ticks`, `qty`, `order_id`, `flags` are equal.

We also assert that N &gt; 0 (we got at least one event) and that `close_ticks` and `events_written` are equal for the two runs. So we’re checking **exact byte-level agreement** of the first N records and the two result structs, not a summary statistic.

### Trace test: TraceShiftAndPrintFirst20

- **Purpose:** Run a short session with small depth (initial_depth=1), replay events on a fresh book, and print the first 20 events plus best bid/ask after each for debugging. Ensures the event stream keeps the book valid (bid &lt; ask, spread ≥ 1) after every event.
- **How to run:**  
  `docker-compose -f docker/docker-compose.yml run --rm test /app/build/tests --gtest_filter="*TraceShift*" --gtest_color=yes`  
  (Build the test image first with `docker-compose -f docker/docker-compose.yml build test`.)
- **Output:** For each of the first 20 events: one line with ts_ns, type, side, price, qty, order_id; then one line with best_bid (depth) and best_ask (depth). Finally a summary line with events_written, close_ticks, shift_count. Shift-on-depletion is covered by QrsdpBook.ShiftWhenBestDepleted.

---

## 12) Verification: code anchors and invariants

### Code anchors (where each claim is implemented)

- **Seeding spread:** `multi_level_book.cpp` — `seed()` uses `initial_spread_ticks` and sets best bid/ask symmetrically:
  ```cpp
  const uint32_t spread = s.initial_spread_ticks > 0 ? s.initial_spread_ticks : 2u;
  const int half = static_cast<int>(spread / 2);
  const int32_t best_bid = s.p0_ticks - half;
  const int32_t best_ask = s.p0_ticks + static_cast<int>(spread) - half;
  ```
- **Shift when best depth reaches 0:** `multi_level_book.cpp` — in `apply()`, EXECUTE and CANCEL paths decrement depth; when best level hits 0 we call the shift:
  ```cpp
  case EventType::EXECUTE_BUY: {
      if (num_levels_ > 0 && ask_levels_[0].depth > 0) {
          --ask_levels_[0].depth;
          if (ask_levels_[0].depth == 0) shiftAskBook();
      }
      break;
  }
  ```
  Same pattern for EXECUTE_SELL and shiftBidBook(); CANCEL clamps depth to 0 but does not call shift (only execute at index 0 can deplete best).
- **Shift implementation (copy levels, refill far):** `multi_level_book.cpp`:
  ```cpp
  void MultiLevelBook::shiftBidBook() {
      for (size_t i = 0; i + 1 < num_levels_; ++i) {
          bid_levels_[i] = bid_levels_[i + 1];
      }
      bid_levels_[num_levels_ - 1].price_ticks = bid_levels_[num_levels_ - 2].price_ticks - 1;
      bid_levels_[num_levels_ - 1].depth = initial_depth_;
  }
  ```
- **Intensity formulas:** `simple_imbalance_intensity.cpp` — add, execute (with epsilon_exec), cancel from features and params:
  ```cpp
  const double add_bid = params_.base_L * (1.0 - I);
  const double add_ask = params_.base_L * (1.0 + I);
  const double eps_exec = (params_.epsilon_exec > 0.0) ? params_.epsilon_exec : 0.05;
  const double exec_sell = params_.base_M * (eps_exec + std::max(I, 0.0));
  const double exec_buy = params_.base_M * (eps_exec + std::max(-I, 0.0));
  const double cancel_bid = params_.base_C * q_bid;
  const double cancel_ask = params_.base_C * q_ask;
  ```
- **Clamp (no negative/NaN):** `simple_imbalance_intensity.cpp` — every intensity is clamped before output:
  ```cpp
  double clampNonNegative(double x) {
      if (std::isnan(x) || std::isinf(x) || x < 0.0) return kEpsilon;
      return std::max(x, kEpsilon);
  }
  // ... out.add_bid = clampNonNegative(add_bid); etc.
  ```
- **Exponential Δt and categorical type:** `competing_intensity_sampler.cpp`:
  ```cpp
  double u = rng_->uniform();
  if (u <= 0.0 || u >= 1.0) u = kMinU;
  if (u < kMinU) u = kMinU;
  return -std::log(u) / lambdaTotal;
  // ...
  for (EventType t : types) {
      cum += intens.at(t);
      if (u < cum / total) return t;
  }
  return EventType::EXECUTE_SELL;
  ```
- **Attribute sampling (add level, cancel by depth, execute at best):** `unit_size_attribute_sampler.cpp` — ADD uses `sampleLevelIndex(book.numLevels())` with exp(-alpha*k) weights; CANCEL uses `sampleCancelLevelIndex(is_bid, book)` with depth weights; EXECUTE uses `f.best_ask_ticks` / `f.best_bid_ticks`:
  ```cpp
  case EventType::CANCEL_BID:
      out.price_ticks = book.bidPriceAtLevel(sampleCancelLevelIndex(true, book));
      break;
  case EventType::EXECUTE_BUY:
      out.side = Side::ASK;
      out.price_ticks = f.best_ask_ticks;
      break;
  ```

### Invariants enforced in code

| Invariant | Where enforced |
|-----------|----------------|
| **bid &lt; ask** | Seeding sets best_bid = p0−half, best_ask = p0+(spread−half), so spread ≥ 1. apply() only adds/cancels at valid levels and execute only decrements depth; shift preserves ordering (new best bid/ask stay either side of mid). No code path sets bid ≥ ask. |
| **spread ≥ 1 tick** | Seed uses `initial_spread_ticks` ≥ 1 (default 2). Shift moves bid down one tick and ask up one tick; refill levels are beyond best, so spread never shrinks below 1. |
| **No negative depth** | `multi_level_book.cpp` CANCEL: `if (d >= e.qty) d -= e.qty; else d = 0`. EXECUTE only decrements when `depth > 0`; after decrement we call shift when depth==0. Add only increases depth. |
| **EXECUTE always consumes opposite best** | Attribute sampler sets EXECUTE_BUY price = `f.best_ask_ticks`, EXECUTE_SELL = `f.best_bid_ticks`. apply() uses index 0 (best) only for execute: `ask_levels_[0].depth--` / `bid_levels_[0].depth--`. |
| **Shift when best depth reaches 0** | In apply(), immediately after `--ask_levels_[0].depth` we check `if (ask_levels_[0].depth == 0) shiftAskBook();`; same for bid and shiftBidBook(). So whenever best level goes to 0 (from that execute), shift runs. (Cancel can zero a level but only at the level indexed by price; if that level is not index 0, no shift. When cancel zeros level 0, we do not currently call shift—see note below.) |

**Note:** If a CANCEL targets level 0 and reduces its depth to 0, the current code does **not** call shift (only the EXECUTE path does). So “shift occurs whenever best depth reaches 0” is fully true for **execute** at best; for **cancel** at best we clamp depth to 0 but do not shift. If desired, shift-on-cancel-at-best can be added by checking after the cancel clamp whether level 0 depth is 0 and calling shift.

---

## 13) How to run

### Build and test (Docker, recommended)

From the repo root:

```bash
# Build the test image (includes CMake + build + Google Test)
docker-compose -f docker/docker-compose.yml build test

# Run all tests
docker-compose -f docker/docker-compose.yml run --rm test
```

To run only QRSDP tests:

```bash
docker-compose -f docker/docker-compose.yml run --rm test /app/build/tests --gtest_filter="*Qrsdp*" --gtest_color=yes
```

To run only the **trace test** (2 s session, initial_depth=1, first 20 events and best bid/ask printed):

```bash
docker-compose -f docker/docker-compose.yml run --rm test /app/build/tests --gtest_filter="*TraceShift*" --gtest_color=yes
```

### Run a short simulation and print events

The **TraceShiftAndPrintFirst20** test (see §11) runs a 2 s session with initial_depth=1, replays events, and prints the first 20 events plus best bid/ask after each. Use the filter above. There is no dedicated main in `itch_simulator` for QRSDP; that executable is the old exchange simulator. For a custom run you could add a small test or a standalone `qrsdp_run` target that wires the same components and prints events.

---

## 13) Mental model summary

**Plain English:** The producer runs a continuous-time simulation of a single security’s limit order book for a fixed number of seconds. At each step it reads the current book state (spread, imbalance, queue sizes), computes six “intensities” (rates for add-bid, add-ask, cancel-bid, cancel-ask, market-buy, market-sell). It then samples how long until the next event (exponential with total rate) and which of the six events it is (categorical with probabilities proportional to the intensities). It samples where that event happens (which price level, or best for executes), applies the event to the book (add/cancel/execute, unit size), and appends one EventRecord to the sink. Time advances until we exceed the session length; then we return the mid price at the end and how many events we wrote. The RNG is seeded at the start so the same session config gives the same stream of events.

**Annotated pseudocode (event loop):**

```text
 1  seed RNG with session.seed
 2  seed book with p0, levels, initial depth
 3  t = 0, order_id = 1
 4  while t < session_seconds:
 5      features = book.features()                    // spread, imbalance, best sizes
 6      intensities = model.compute(features)          // six λ_i
 7      λ_total = sum(intensities)
 8      Δt = -ln(U)/λ_total; t += Δt                  // exponential wait
 9      if t >= session_seconds: break
10      type = categorical draw over λ_i/λ_total       // which of 6 events
11      attrs = attributeSampler.sample(type, book, f) // side, price, qty=1
12      book.apply(SimEvent(type, attrs...))          // update book
13      sink.append(EventRecord(ts_ns, type, ...))    // persist
14  return (mid(best_bid, best_ask), events_written)
```

That’s the full v1 producer loop in one place.
