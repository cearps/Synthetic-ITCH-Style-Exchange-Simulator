# QRSDP Model: Mathematics & Code Review

**Reviewer:** AI Quant + C++ Reviewer
**Date:** 2026-02-22
**Branch:** `feature/qrsdp-queue-reactive-hlr2014`
**Commit:** `05d8cce` (fix: cancel-triggered shifts, spread-preserving price movement)

---

## 1. Glossary

| Symbol / Term | Definition | Units |
|---|---|---|
| **tick** | Minimum discrete price increment. All prices are integers in "tick" units. | — (dimensionless integer) |
| **spread** | `best_ask − best_bid`. Always ≥ 1 tick when invariants hold. | ticks |
| **mid** | `(best_bid + best_ask) / 2`. Half-integer in ticks. | ticks |
| **best bid / best ask** | Price of the highest-priority bid / lowest-priority ask (level index 0). | ticks |
| **q_bid_best, q_ask_best** | Queue depth (number of shares/units) at the best bid / best ask level. | shares |
| **I** (imbalance) | `(q_bid_best − q_ask_best) / (q_bid_best + q_ask_best + ε)`. Range [−1, +1]. | dimensionless |
| **K** | Number of price levels tracked per side (bid and ask). | — |
| **n** or **q** | Queue depth at a specific level. | shares |
| **λ** | Intensity (rate) of a Poisson event process. | events/second |
| **Λ** | Total intensity: sum of all six event intensities. | events/second |
| **Δt** | Inter-arrival time between consecutive events. Drawn from Exp(Λ). | seconds |
| **shift** | A price change triggered when the best level's depth reaches 0. The depleted side's levels slide and the opposite side's prices move to preserve spread. | — |
| **initial_depth** | Queue depth assigned to newly created levels (e.g., back of book after shift, or at session start). | shares |
| **base_L** | Legacy model: base add (limit order) intensity. | events/second |
| **base_M** | Legacy model: base execute (market order) intensity. | events/second |
| **base_C** | Legacy model: cancel intensity coefficient. | 1/second (multiplied by queue size to get events/sec) |
| **ε_exec** (`epsilon_exec`) | Legacy model: baseline execution rate when imbalance ≈ 0. | dimensionless (multiplied by base_M) |
| **α** (`alpha`) | Level selection parameter for adds: level k chosen with weight ∝ exp(−αk). | dimensionless |
| **θ_reinit** (`theta_reinit`) | Probability of reinitializing the book (Poisson depths) after a shift. 0 = off. | dimensionless probability |
| **reinit_depth_mean** | Mean of the Poisson distribution used when reinitializing depths after a shift. | shares |

---

## 2. Model Summary in Equations

### 2.1 State Space

The Markov state at time t is:

$$
X(t) = \bigl(\{(p^b_k, q^b_k)\}_{k=0}^{K-1},\; \{(p^a_k, q^a_k)\}_{k=0}^{K-1}\bigr)
$$

where $p^b_k$ / $q^b_k$ are the price/depth at bid level k (k=0 is best), and similarly for ask. Prices are strictly ordered: $p^b_0 > p^b_1 > \cdots$ and $p^a_0 < p^a_1 < \cdots$, with $p^b_0 < p^a_0$ always.

### 2.2 Event Processes

Six event types, each with state-dependent intensity:

| Event | Symbol | Effect on book |
|---|---|---|
| ADD_BID | λ^L_bid | `q^b_k += 1` at level k (chosen by sampler) |
| ADD_ASK | λ^L_ask | `q^a_k += 1` at level k |
| CANCEL_BID | λ^C_bid | `q^b_k −= 1` at level k; if k=0 and q → 0 → **shift** |
| CANCEL_ASK | λ^C_ask | `q^a_k −= 1` at level k; if k=0 and q → 0 → **shift** |
| EXECUTE_BUY | λ^M_buy | `q^a_0 −= 1` (always best ask); if q → 0 → **shift** |
| EXECUTE_SELL | λ^M_sell | `q^b_0 −= 1` (always best bid); if q → 0 → **shift** |

All quantities are `qty = 1` (unit volume).

### 2.3 Total Intensity and Next-Event Time

$$
\Lambda(X) = \lambda^L_{\text{bid}} + \lambda^L_{\text{ask}} + \lambda^C_{\text{bid}} + \lambda^C_{\text{ask}} + \lambda^M_{\text{buy}} + \lambda^M_{\text{sell}}
$$

The time to the next event is exponentially distributed:

$$
\Delta t \sim \text{Exp}(\Lambda) \quad\Longrightarrow\quad \Delta t = -\frac{\ln U}{\Lambda}, \quad U \sim \text{Uniform}(0,1)
$$

### 2.4 Event Type Draw

Given the next event occurs, its type is drawn categorically:

$$
P(\text{type} = i) = \frac{\lambda_i(X)}{\Lambda(X)}
$$

Implementation: cumulative sum over the six intensities; first type where `U < cum / total` is selected.

### 2.5 Event Attributes (Price / Level Selection)

| Event type | Price / level rule |
|---|---|
| ADD_BID / ADD_ASK | Level k with weight ∝ exp(−αk) (legacy) or per-level hint (HLR). Price = book price at that level. |
| CANCEL_BID / CANCEL_ASK | Level k with weight ∝ q_k (proportional to depth). Price = book price at that level. |
| EXECUTE_BUY | Always at best ask (level 0). Price = `best_ask_ticks`. |
| EXECUTE_SELL | Always at best bid (level 0). Price = `best_bid_ticks`. |

---

## 3. Code Mapping (Equation → Code)

### 3.1 Main Event Loop

**File:** `src/producer/qrsdp_producer.cpp`
**Method:** `QrsdpProducer::stepOneEvent()`

```cpp
// Step 1: Read state
BookState state;
state.features = book_->features();
// ... fill bid_depths, ask_depths ...

// Step 2: Compute intensities
const Intensities intens = intensityModel_->compute(state);
const double lambda_total = intens.total();

// Step 3: Sample Δt ~ Exp(Λ)
const double dt = eventSampler_->sampleDeltaT(lambda_total);
t_ += dt;

// Step 4: Sample event type
EventType type = eventSampler_->sampleType(intens);

// Step 5: Sample attributes (price, level, qty)
const EventAttrs attrs = attributeSampler_->sample(type, *book_, state.features, level_hint);

// Step 6: Apply to book (may trigger shift)
book_->apply(ev);

// Step 7: Record to sink
sink.append(rec);
```

### 3.2 Exponential Sampling: Δt = −ln(U) / Λ

**File:** `src/sampler/competing_intensity_sampler.cpp`
**Method:** `CompetingIntensitySampler::sampleDeltaT()`

```cpp
double CompetingIntensitySampler::sampleDeltaT(double lambdaTotal) {
    if (lambdaTotal <= 0.0 || !std::isfinite(lambdaTotal)) return kSafeDeltaT;  // 1e9 seconds
    double u = rng_->uniform();
    if (u <= 0.0 || u >= 1.0) u = kMinU;  // 1e-10
    if (u < kMinU) u = kMinU;
    return -std::log(u) / lambdaTotal;
}
```

**Math check:** Correct. If U ~ Uniform(0,1), then −ln(U)/λ ~ Exp(λ). The clamp `u ≥ 1e-10` prevents `−ln(0) = ∞` and caps max Δt at `−ln(1e-10)/Λ ≈ 23/Λ`.

### 3.3 Categorical Event Type Selection

**File:** `src/sampler/competing_intensity_sampler.cpp`
**Method:** `CompetingIntensitySampler::sampleType()`

```cpp
const EventType types[] = {
    EventType::ADD_BID, EventType::ADD_ASK, EventType::CANCEL_BID,
    EventType::CANCEL_ASK, EventType::EXECUTE_BUY, EventType::EXECUTE_SELL
};
for (EventType t : types) {
    cum += intens.at(t);
    if (u < cum / total) return t;
}
return EventType::EXECUTE_SELL;  // fallback for rounding
```

**Math check:** Standard inverse-CDF categorical sampling. Unbiased. The order of iteration doesn't affect correctness.

### 3.4 Feature Extraction: Imbalance

**File:** `src/book/multi_level_book.cpp`
**Method:** `MultiLevelBook::features()`

```cpp
const double sum = static_cast<double>(q_bid) + static_cast<double>(q_ask) + kImbalanceEps;  // 1e-9
const double imbalance = (static_cast<double>(q_bid) - static_cast<double>(q_ask)) / sum;
```

Implements: $I = \frac{q^b - q^a}{q^b + q^a + \varepsilon}$, where ε = 10⁻⁹ prevents division by zero.

### 3.5 RNG: Seeding and Uniformity

**File:** `src/rng/mt19937_rng.cpp`

```cpp
Mt19937Rng::Mt19937Rng(uint64_t seed) : gen_(seed), dist_(0.0, 1.0) {}
double Mt19937Rng::uniform() { return dist_(gen_); }
void Mt19937Rng::seed(uint64_t s) { gen_.seed(s); }
```

Uses `std::mt19937_64` with `std::uniform_real_distribution<double>(0.0, 1.0)`. Deterministic: same seed → same event stream. The distribution produces values in [0, 1); the sampler guards against the boundary values 0 and 1.

---

## 4. Intensity Model Deep Dive

### 4.1 Legacy Model: SimpleImbalanceIntensity

**File:** `src/model/simple_imbalance_intensity.cpp`

Given imbalance I, best-level depths q_b, q_a, and parameters (base_L, base_M, base_C, ε_exec):

$$
\lambda^L_{\text{bid}} = L \cdot (1 - I), \qquad
\lambda^L_{\text{ask}} = L \cdot (1 + I)
$$

$$
\lambda^M_{\text{sell}} = M \cdot (\varepsilon + \max(I, 0)), \qquad
\lambda^M_{\text{buy}} = M \cdot (\varepsilon + \max(-I, 0))
$$

$$
\lambda^C_{\text{bid}} = C \cdot q_b, \qquad
\lambda^C_{\text{ask}} = C \cdot q_a
$$

All clamped to ≥ 10⁻⁹ via `clampNonNegative()`.

**Interpretation:**
- **Adds** increase on the side with less depth (imbalance drives replenishment).
- **Executions** increase against the side with more depth (positive I → more exec_sell draining bids).
- **Cancels** are proportional to queue size (each order independently cancellable).

**Stability analysis:**

The net drain at the best bid level is:

$$
\text{drain}^b = \lambda^M_{\text{sell}} + \frac{\lambda^C_{\text{bid}} \cdot q^b_0}{\sum_k q^b_k} - \lambda^L_{\text{bid}} \cdot P(\text{level } 0)
$$

where P(level 0) ≈ 1/Σ exp(−αk). For depth to deplete, drain must exceed add rate at best.

The cancel intensity uses `q_bid_best` as the scalar, but the cancel attribute sampler distributes cancels across ALL levels proportional to depth. So the effective cancel rate at best is:

$$
\lambda^C_{\text{bid, eff at best}} = C \cdot q^b_0 \cdot \frac{q^b_0}{\sum_k q^b_k}
$$

This is a modeling inconsistency: the intensity formula implies all cancels drain the best level, but the sampler spreads them. The total cancel rate across all levels is correct, but the per-level allocation differs from what the intensity formula suggests.

**Required condition for shifts (at I ≈ 0):**

$$
M \cdot \varepsilon + C \cdot \frac{(q^b_0)^2}{\sum_k q^b_k} > L \cdot P(\text{level } 0)
$$

With Debug Preset (M=30, ε=0.2, L=5, C=0.1, depth=2, K=5, α=0.5): 6.0 + 0.04 > 2.15 ✓

**Depth explosion condition:** Depth grows without bound when add rate exceeds drain rate at best. This occurs when `L · P(level 0) > M · ε + C · q / K` for all q. With flat add rate and linear cancel, there's always a finite equilibrium q* = (L · P(0) − M · ε) / (C / K), provided M · ε < L · P(0). Above q*, drain dominates.

### 4.2 HLR2014 Model: CurveIntensityModel

**File:** `src/model/curve_intensity_model.cpp`

Each event type at each level has a **queue-size-dependent** intensity curve:

$$
\lambda^L_{\text{bid}, i}(n) = \text{curve}^L_{b,i}(q^b_i), \qquad
\lambda^C_{\text{bid}, i}(n) = \text{curve}^C_{b,i}(q^b_i)
$$

Market orders only at best:

$$
\lambda^M_{\text{buy}}(n) = \text{curve}^M_{\text{buy}}(q^a_0), \qquad
\lambda^M_{\text{sell}}(n) = \text{curve}^M_{\text{sell}}(q^b_0)
$$

Aggregated:

$$
\lambda^L_{\text{bid}} = \sum_{i=0}^{K-1} \lambda^L_{\text{bid}, i}, \quad\text{etc.}
$$

The per-level intensities are stored in a flat vector of size 4K+2, and when present, the producer samples directly from this vector (not from the 6-way aggregate). This gives **joint (type, level) selection in one draw**.

**Default curves** (file: `src/model/hlr_params.cpp`):

| Curve | Formula | Rationale |
|---|---|---|
| addBest(n) | 1.0 if n=0, else 2.5 | Flat: limit order arrival doesn't depend much on queue size |
| addDeeper(n) | 2.0 / (1 + 0.15n) | Deeper levels attract fewer adds when queue is long |
| cancelCurve(n) | 0 if n=0, else 0.15n | Linear: each order independently cancellable |
| marketCurve(n) | 0.5 if n=0, else 1.0 / (1 + 0.005n) | Near-constant: external market order flow |

**Equilibrium at best:** add(n) = cancel(n) + market(n)
→ 2.5 = 0.15n + 1.0/(1 + 0.005n)
→ n ≈ 10. Queue fluctuates around depth 10.

**Tail rule:** All curves use `FLAT` — for n > n_max, the last table value is used. This means at very large depths, add rate = 2.5 and cancel rate = 0.15 · n_max. Since cancel grows linearly and is evaluated at actual n (not clamped to n_max), depth cannot truly explode — but the table lookup IS clamped:

```cpp
// intensity_curve.cpp
double IntensityCurve::value(size_t n) const {
    if (n <= n_max_) return std::max(table_[n], 0.0);
    switch (tail_) {
        case TailRule::FLAT: return std::max(table_.back(), 0.0);  // ← clamped!
        ...
    }
}
```

**Issue (important):** For n > n_max (default 100), the cancel curve returns `cancelCurve(100) = 15.0` regardless of actual n. This means at depth 500, cancel intensity is still 15.0 instead of the "correct" 75.0. The cancel curve saturates, which can cause depth accumulation beyond n_max in long sessions. The add curve also saturates at 2.5, so the equilibrium at n=100 is: 2.5 vs 15.0 + ~1.0 = 16.0 (strongly draining). This means the FLAT tail is actually fine for these particular curves — depth cannot grow past ~17 because cancel(100)=15 >> add=2.5. But this is fragile and depends on the specific curve shapes.

### 4.3 Clamp / Guard Logic Summary

| Location | Guard | Purpose |
|---|---|---|
| `clampNonNegative()` in legacy model | NaN, inf, negative → 10⁻⁹ | Prevent negative/invalid intensities |
| `IntensityCurve::setTable()` | negative/NaN → 0, tiny positive → 10⁻¹² | Sanitize curve tables |
| `IntensityCurve::value()` | `std::max(v, 0.0)` | Nonnegativity at lookup |
| `CurveIntensityModel::compute()` | `std::max(x, 10⁻¹²)` | Prevent zero total intensity |
| `sampleDeltaT()` | u clamped to [10⁻¹⁰, 1), Λ ≤ 0 → 10⁹ seconds | Prevent log(0), handle degenerate Λ |
| `sampleType()` | total ≤ 0 → return ADD_BID | Fallback for degenerate intensity |

---

## 5. Price Move / Shift Mechanics

### 5.1 Trigger Condition

A shift occurs when **any** event reduces the best level's depth to zero:

- EXECUTE_BUY depletes best ask → `shiftAskBook()`
- EXECUTE_SELL depletes best bid → `shiftBidBook()`
- CANCEL_BID depletes best bid (level index 0) → `shiftBidBook()`
- CANCEL_ASK depletes best ask (level index 0) → `shiftAskBook()`

**File:** `src/book/multi_level_book.cpp`, lines 80–127.

### 5.2 What Happens During a Shift

Taking `shiftAskBook()` as example (bid shift is symmetric):

**Step 1 — Slide the depleted side:**
```cpp
for (size_t i = 0; i + 1 < num_levels_; ++i)
    ask_levels_[i] = ask_levels_[i + 1];          // level 1 → 0, 2 → 1, ...
ask_levels_[num_levels_ - 1].price_ticks = ask_levels_[num_levels_ - 2].price_ticks + 1;
ask_levels_[num_levels_ - 1].depth = initial_depth_;  // fresh back level
```

**Step 2 — Move opposite side's prices to preserve spread:**
```cpp
for (size_t k = 0; k < num_levels_; ++k)
    bid_levels_[k].price_ticks += 1;   // all bids shift UP by 1 tick
```

Net effect: mid-price moves up by 1 tick. Spread unchanged. The new best ask is what was level 1 (with its accumulated depth). A fresh level is appended at the back with `initial_depth`.

### 5.3 Optional Reinitialize After Shift

**File:** `src/producer/qrsdp_producer.cpp`, lines 81–86.

```cpp
if (shift_occurred) {
    ++shift_count_;
    if (theta_reinit_ > 0.0 && rng_->uniform() < theta_reinit_) {
        book_->reinitialize(*rng_, reinit_mean_);
    }
}
```

With probability θ_reinit, ALL level depths (both sides) are redrawn from Poisson(reinit_depth_mean). Prices are NOT changed. This prevents long-term depth accumulation at inner levels.

`reinitialize()` in `multi_level_book.cpp`:
```cpp
void MultiLevelBook::reinitialize(IRng& rng, double depth_mean) {
    for (size_t k = 0; k < num_levels_; ++k) {
        bid_levels_[k].depth = poissonSample(rng, mu);
        ask_levels_[k].depth = poissonSample(rng, mu);
    }
}
```

### 5.4 Comparison with HLR2014 Paper

In Huang, Lehalle & Rosenbaum (2014), the model tracks queues at the best bid and ask only (K=1 effective). When either queue reaches zero, the reference (mid) price shifts by δ, and both queues are reinitialized from a stationary distribution. Key differences:

| Aspect | HLR2014 paper | This implementation |
|---|---|---|
| Levels tracked | 1 per side (best only) | K per side (multi-level) |
| Shift trigger | Best queue → 0 | Best queue → 0 (same) |
| After shift | Both queues reinitialized | Depleted side slides; opposite side prices shift; reinit optional (θ_reinit) |
| Price move | Mid shifts by 1 tick | Both sides move by 1 tick (equivalent mid shift) |
| Inner levels | Not modeled | Carry accumulated depth across shifts |

The multi-level extension is a generalization. The paper's model is recovered by setting K=1 and θ_reinit=1.0.

---

## 6. Pipeline Sanity Checks ("Nonsense Audit")

### 6.1 Units

| Parameter | Claimed unit | Verified? |
|---|---|---|
| base_L | events/sec | ✓ Directly used as intensity in Intensities struct |
| base_M | events/sec | ✓ Multiplied by dimensionless ε_exec and imbalance |
| base_C | 1/sec | ✓ Multiplied by q (shares) → events/sec |
| Δt | seconds | ✓ `−ln(U)/Λ` where Λ is events/sec → seconds |
| t_ | seconds | ✓ Accumulated Δt, compared to `session_seconds_` |
| ts_ns | nanoseconds | ✓ `t_ * 1e9` |

**Verdict:** Units are consistent.

### 6.2 Signs

- `add_bid = L · (1 − I)`: When I > 0 (bid depth > ask depth), add_bid decreases. ✓ Correct — less incentive to add to the already-deeper side.
- `exec_sell = M · (ε + max(I, 0))`: When I > 0 (bid heavy), exec_sell increases. ✓ Correct — market sells are attracted to deep bids.
- `exec_buy = M · (ε + max(−I, 0))`: When I < 0 (ask heavy), exec_buy increases. ✓ Correct.

**Verdict:** Signs are correct throughout.

### 6.3 Symmetry

The legacy model is symmetric under bid↔ask exchange (swap I → −I). The HLR model uses identical curves for bid and ask sides. Shift functions are symmetric (`shiftBidBook` mirrors `shiftAskBook`).

**Verdict:** Symmetry preserved.

### 6.4 Boundary Conditions

**q = 0 for cancels:**
```cpp
if (d >= e.qty) d -= e.qty;
else d = 0;
```
Cannot go negative. If already 0, subtracting 1 → clamped to 0. If at best level, triggers shift. ✓

**q = 0 for executes:**
```cpp
if (ask_levels_[0].depth > 0) {
    --ask_levels_[0].depth;
    if (ask_levels_[0].depth == 0) shiftAskBook();
}
```
If depth is already 0 (zombie from rapid events), execute is silently skipped. This can only happen if a cancel-triggered shift produced a new best level with depth 0 (i.e., the old level 1 was zeroed by a previous cancel). This is a rare edge case — see Finding #1.

**Spread constraints:**
After every shift, spread = old spread (preserved by design). Initial spread set by `BookSeed`. The UI checks `bid < ask` as an invariant. Spread can never go to 0 because shifts move both sides together and levels are ordered by construction.

### 6.5 RNG Correctness

**Exponential sampling:** `−ln(U) / Λ` with U ~ Uniform(0,1). ✓ Standard inverse-CDF method.

**Categorical sampling:** Cumulative-sum scan with `u < cum / total`. ✓ Unbiased. Final fallback returns last type (handles floating-point rounding).

**Seeding:** `rng.seed(session.seed)` called once at session start. Same seed → identical event stream. ✓ Deterministic. Verified by test `QrsdpProducer.DeterminismSameSeed`.

**Concern:** The RNG is shared between `eventSampler`, `attributeSampler`, and `producer` (for θ_reinit coin flip). Call order matters for determinism. The current order (sampleDeltaT → sampleType → sample attrs → apply → maybe reinit) is fixed and deterministic. ✓

### 6.6 Invariants

| Invariant | Enforced? | Where |
|---|---|---|
| No negative depths | ✓ | Cancel clamps to 0; execute checks `depth > 0` |
| best_bid < best_ask | ✓ | Shifts preserve spread; initial seed enforces; UI checks |
| spread ≥ 1 | ✓ | Initial spread ≥ 1; shifts preserve it |
| Executes target best (k=0) | ✓ | Attribute sampler always uses `f.best_ask_ticks` / `f.best_bid_ticks` |
| order_id sequential | ✓ | `order_id_++` in producer |
| ts_ns monotonic | ✓ | `t_` only increases (Δt > 0 always) |

### 6.7 Ergodicity / Stability

**Legacy model:** At large depths, cancel rate ~ C · q grows linearly while add rate ~ L · P(0) is constant. Drain eventually dominates. With cancel-triggered shifts, depth cannot grow unboundedly. The system has a stochastic equilibrium around q* ≈ (L · P(0) − M · ε) · K / C (for the legacy model with effective cancel distribution).

**HLR model:** Cancel curve is linear (0.15n), add curve is flat (2.5), market curve is ~constant (1.0). For n > n_max (100), cancel saturates at 15.0 due to FLAT tail rule. Since 15.0 >> 2.5, depth is still strongly drained above n_max. System is ergodic.

**Verdict:** No permanently depth-accumulating regimes with current parameters. But see Finding #2 regarding the tail rule.

### 6.8 Logging Consistency

- `rec.price_ticks = attrs.price_ticks`: the price BEFORE the shift (at the time the event was sampled). ✓ Correct: the cancel/execute happened at that price.
- `rec.qty = attrs.qty = 1`: always unit volume. ✓
- `rec.flags = 0`: shifts are not flagged. See Finding #3.
- `rec.ts_ns = t_ * 1e9`: nanosecond timestamp from floating-point seconds. Precision: at t=3600, `1e9 * 3600 = 3.6e12`, well within uint64_t range. Double has ~15 digits of precision, so nanosecond precision is maintained up to ~10⁷ seconds. ✓ For session lengths ≤ 3600s.

---

## 7. Minimal Worked Example

**Setup:** Legacy model, 1 level per side (K=1), initial_depth=2, spread=2.

Parameters: base_L=5, base_M=30, base_C=0.1, ε_exec=0.2, α=0.5.

**Initial state:**
```
Bid: [9999:2]    Ask: [10001:2]
mid = 10000, spread = 2, I = 0.0
```

**Step 1: Compute intensities (I = 0)**

| λ | Formula | Value |
|---|---|---|
| add_bid | 5·(1−0) | 5.0 |
| add_ask | 5·(1+0) | 5.0 |
| cancel_bid | 0.1·2 | 0.2 |
| cancel_ask | 0.1·2 | 0.2 |
| exec_buy | 30·(0.2+0) | 6.0 |
| exec_sell | 30·(0.2+0) | 6.0 |

Λ = 22.4

**Step 2: Sample Δt.** Suppose U₁ = 0.3.
Δt = −ln(0.3)/22.4 = 1.204/22.4 = 0.054 seconds. t = 0.054.

**Step 3: Sample event type.** Cumulative probabilities:

| Type | λ/Λ | Cumulative |
|---|---|---|
| ADD_BID | 0.223 | 0.223 |
| ADD_ASK | 0.223 | 0.446 |
| CANCEL_BID | 0.009 | 0.455 |
| CANCEL_ASK | 0.009 | 0.464 |
| EXEC_BUY | 0.268 | 0.732 |
| EXEC_SELL | 0.268 | 1.000 |

Suppose U₂ = 0.70 → falls in EXEC_BUY range (0.464 < 0.70 < 0.732).

**Step 4: Apply EXEC_BUY at best ask (10001).**
```
ask_levels_[0].depth: 2 → 1. Not zero, no shift.
```

**State after event 1:**
```
Bid: [9999:2]    Ask: [10001:1]
I = (2−1)/(2+1) = 0.333
```

---

**Event 2:** Recompute intensities with I = 0.333, q_b=2, q_a=1.

| λ | Formula | Value |
|---|---|---|
| add_bid | 5·(1−0.333) | 3.33 |
| add_ask | 5·(1+0.333) | 6.67 |
| cancel_bid | 0.1·2 | 0.2 |
| cancel_ask | 0.1·1 | 0.1 |
| exec_buy | 30·(0.2+0.333) | 16.0 |
| exec_sell | 30·(0.2+0) | 6.0 |

Λ = 32.3. Note: exec_buy surged to 16.0 because imbalance favors buying into the thin ask.

Suppose U₃ = 0.5 → Δt = −ln(0.5)/32.3 = 0.021s. t = 0.075.
Suppose U₄ = 0.55 → cumulative: ADD_BID 0.103, ADD_ASK 0.310, CANCEL_BID 0.316, CANCEL_ASK 0.319, EXEC_BUY 0.814. 0.55 < 0.814 → **EXEC_BUY**.

**Apply EXEC_BUY at 10001:**
```
ask_levels_[0].depth: 1 → 0 → shiftAskBook()!
```

**Shift:** ask slides (only 1 level, so just ask price += 1, depth = initial_depth). Bid prices += 1.

```
Before shift: Bid [9999:2], Ask [10001:0]
After shift:  Bid [10000:2], Ask [10002:2]
mid = 10001, spread = 2
```

Mid-price moved from 10000 → 10001. ✓ Price rose because buyers (exec_buy) depleted the ask.

---

**Event 3:** New state: q_b=2, q_a=2, I=0. Back to the original intensity regime but at a higher price.

Suppose U₅ = 0.9 → Δt = −ln(0.9)/22.4 = 0.105/22.4 = 0.0047s. t = 0.080.
Suppose U₆ = 0.95 → EXEC_SELL (cum 0.732..1.000). 0.95 > 0.732 → EXEC_SELL.

**Apply EXEC_SELL at 10000 (best bid):**
```
bid_levels_[0].depth: 2 → 1. Not zero, no shift.
```

**State:**
```
Bid: [10000:1]    Ask: [10002:2]
I = (1−2)/(1+2) = −0.333. Now ask-heavy; exec_sell will drop, exec_buy will surge again.
```

This walkthrough confirms the code produces the expected dynamics: imbalance-driven intensity → frequent executions on the thin side → depletion → shift → price moves.

---

## 8. Findings & Recommended Fixes

### Finding #1: Post-Shift Zombie Level (no cascade)

**Severity:** Nice-to-have
**Symptom:** If a cancel previously zeroed a non-best level (e.g., level 1 had depth 0), and then the best level is depleted triggering a shift, the new best level (old level 1) has depth 0. No further shift cascades. Executes are silently dropped until an add replenishes it.
**Root cause:** `shiftBidBook()` / `shiftAskBook()` don't check if the newly-revealed best level is also empty.
**Impact:** Rare with current parameters (cancel sampler weights by depth, so empty levels get zero weight). Would require two cancels zeroing adjacent levels in quick succession.
**Proposed fix:** After the shift, check `if (bid_levels_[0].depth == 0) shiftBidBook();` (recursive cascade). Add a max-cascade guard to prevent infinite recursion if all levels are zero.

### Finding #2: IntensityCurve FLAT Tail May Mask Bugs

**Severity:** Nice-to-have
**Symptom:** For n > n_max (default 100), the intensity curve returns the value at n_max regardless of actual depth. With the current cancel curve (0.15 · n), cancel(100) = 15.0 is used for all n ≥ 100.
**Root cause:** `IntensityCurve::value()` applies tail rule instead of evaluating the formula.
**Impact:** With current curves, 15.0 >> 2.5, so depth is still drained above n=100. But if someone changes the curves so that cancel(n_max) ≈ add(n_max), depths above n_max could grow without bound.
**Proposed fix:** Consider adding an `EXTRAPOLATE` tail rule that evaluates the curve formula beyond n_max, or document that n_max must be chosen large enough that tail behavior is safe.

### Finding #3: Shifts Not Flagged in Event Log

**Severity:** Important
**Symptom:** Downstream consumers replaying the event log cannot detect shifts without rebuilding the full book. The `flags` field is always 0.
**Root cause:** `rec.flags = 0;` in producer, never set to indicate shift.
**Proposed fix:** Define flag bits (e.g., `kFlagShiftUp = 0x1, kFlagShiftDown = 0x2, kFlagReinit = 0x4`) and set in producer when shift_occurred is true. Would also capture whether a reinit happened.

### Finding #4: Legacy Cancel Intensity vs. Cancel Distribution Mismatch

**Severity:** Nice-to-have (modeling)
**Symptom:** The legacy intensity formula uses `cancel_bid = base_C · q_bid_best`, implying all cancels drain the best level. But the attribute sampler distributes cancels across all K levels proportional to depth. The effective cancel rate at best is `base_C · q_best² / Σq`, not `base_C · q_best`.
**Root cause:** The intensity model computes a scalar cancel rate from best-level depth; the sampler spreads it across levels.
**Impact:** The HLR model (which has per-level curves) doesn't have this issue. For the legacy model, the effective drain at best is lower than the intensity formula suggests, requiring more aggressive parameters.
**Proposed fix:** Either (a) change the legacy formula to use total depth: `cancel_bid = base_C · Σq_bid`, or (b) document that the legacy model's cancel distribution is approximate and the HLR model should be preferred for accurate per-level dynamics.

### Finding #5: Poisson Sampler for Reinitialize Uses Naive Algorithm

**Severity:** Nice-to-have (performance)
**Symptom:** The `poissonSample()` function uses the inverse-CDF method (sequential multiplication), which is O(mean) per sample. For `reinit_depth_mean = 10`, this is fine. For large means (>50), it becomes slow and numerically unstable (underflow in `exp(-mean)`).
**Root cause:** Knuth's algorithm without the normal approximation for large means.
**Impact:** Only affects `reinitialize()` which is called rarely (θ_reinit probability per shift). Not a practical issue at current parameters.
**Proposed fix:** None needed unless reinit_depth_mean is expected to be large (>100). If so, switch to the rejection method or normal approximation for large means.

---

**End of review. Which finding would you like to address first?**
