# SimpleImbalance Model — Mathematical & Implementation Nonsense Audit

**Auditor:** AI Quant + C++ Auditor
**Date:** 2026-02-22
**Branch:** `feature/qrsdp-queue-reactive-hlr2014`
**Scope:** `simple_imbalance_intensity.cpp`, `competing_intensity_sampler.cpp`, `unit_size_attribute_sampler.cpp`, `multi_level_book.cpp`, `qrsdp_producer.cpp`

---

## Section 1 — The Actual Implemented Model

### 1.1 Intensity Formulas (as coded)

From `src/model/simple_imbalance_intensity.cpp`:

```cpp
const double I = std::isnan(f.imbalance) ? 0.0 : f.imbalance;

double total_bid_depth = 0.0;
double total_ask_depth = 0.0;
for (auto d : state.bid_depths) total_bid_depth += static_cast<double>(d);
for (auto d : state.ask_depths) total_ask_depth += static_cast<double>(d);
if (total_bid_depth == 0.0) total_bid_depth = static_cast<double>(f.q_bid_best);
if (total_ask_depth == 0.0) total_ask_depth = static_cast<double>(f.q_ask_best);

const double add_bid    = params_.base_L * (1.0 - I);
const double add_ask    = params_.base_L * (1.0 + I);
const double eps_exec   = (params_.epsilon_exec > 0.0) ? params_.epsilon_exec : 0.05;
const double exec_sell  = params_.base_M * (eps_exec + std::max(I, 0.0));
const double exec_buy   = params_.base_M * (eps_exec + std::max(-I, 0.0));
const double cancel_bid = params_.base_C * total_bid_depth;
const double cancel_ask = params_.base_C * total_ask_depth;
```

In mathematical notation:

$$\lambda^L_\text{bid} = L \cdot (1 - I)$$
$$\lambda^L_\text{ask} = L \cdot (1 + I)$$
$$\lambda^M_\text{sell} = M \cdot (\varepsilon + \max(I, 0))$$
$$\lambda^M_\text{buy} = M \cdot (\varepsilon + \max(-I, 0))$$
$$\lambda^C_\text{bid} = C \cdot Q^b_\text{total}$$
$$\lambda^C_\text{ask} = C \cdot Q^a_\text{total}$$

where $Q^b_\text{total} = \sum_{k=0}^{K-1} q^b_k$.

All six values are then clamped via `clampNonNegative()`:

```cpp
double clampNonNegative(double x) {
    if (std::isnan(x) || std::isinf(x) || x < 0.0) return kEpsilon;  // 1e-9
    return std::max(x, kEpsilon);
}
```

**Every intensity has a floor of $10^{-9}$ events/second.** Zero is never returned.

### 1.2 Definitions

| Symbol | Definition | Source |
|---|---|---|
| $I$ | $(q^b_0 - q^a_0) / (q^b_0 + q^a_0 + 10^{-9})$ | `multi_level_book.cpp:59-60` |
| $q^b_0$ (`q_bid_best`) | Depth at bid level 0 (best bid) | `multi_level_book.cpp:56` |
| $q^a_0$ (`q_ask_best`) | Depth at ask level 0 (best ask) | `multi_level_book.cpp:57` |
| $Q^b_\text{total}$ | Sum of all bid level depths | Computed in `compute()` from `state.bid_depths` |
| $L$ (`base_L`) | Base add (limit order) intensity | events/second |
| $M$ (`base_M`) | Base execute (market order) intensity | events/second |
| $C$ (`base_C`) | Cancel intensity coefficient | 1/second (multiplied by depth → events/sec) |
| $\varepsilon$ (`epsilon_exec`) | Baseline execution rate factor when $I \approx 0$ | dimensionless |

**Are these per-second?** Yes. They feed directly into $\Lambda$ (sum of six rates, units of events/second), from which $\Delta t \sim \text{Exp}(\Lambda)$ is sampled in seconds.

### 1.3 Dead Fields

`IntensityParams` declares two fields that are **never read** by any intensity model:

```cpp
struct IntensityParams {
    double base_L;
    double base_C;
    double base_M;
    double imbalance_sensitivity;  // NEVER READ
    double cancel_sensitivity;     // NEVER READ
    double epsilon_exec;
};
```

They are hardcoded to `1.0` in the UI at construction:

```cpp
IntensityParams p{ui_base_L, ui_base_C, ui_base_M, 1.0, 1.0, ui_epsilon_exec};
```

These appear to be vestigial from an earlier design where imbalance and cancel scaling were tunable multipliers. They currently do nothing. See **Finding #1**.

---

## Section 2 — Event Sampling Audit

### 2.1 Total Intensity

From `qrsdp_producer.cpp:52-53`:

```cpp
const Intensities intens = intensityModel_->compute(state);
const double lambda_total = intens.total();
```

Where `total()` in `records.h:98-100`:

```cpp
double total() const {
    return add_bid + add_ask + cancel_bid + cancel_ask + exec_buy + exec_sell;
}
```

$$\Lambda = \lambda^L_\text{bid} + \lambda^L_\text{ask} + \lambda^C_\text{bid} + \lambda^C_\text{ask} + \lambda^M_\text{buy} + \lambda^M_\text{sell}$$

**Correct.** Simple sum of six nonneg rates.

### 2.2 Inter-arrival Time Sampling

From `competing_intensity_sampler.cpp:17-23`:

```cpp
double CompetingIntensitySampler::sampleDeltaT(double lambdaTotal) {
    if (lambdaTotal <= 0.0 || !std::isfinite(lambdaTotal)) return kSafeDeltaT;  // 1e9
    double u = rng_->uniform();
    if (u <= 0.0 || u >= 1.0) u = kMinU;  // 1e-10
    if (u < kMinU) u = kMinU;
    return -std::log(u) / lambdaTotal;
}
```

**Verification:** If $U \sim \text{Uniform}(0,1)$, then $-\ln(U)/\Lambda \sim \text{Exp}(\Lambda)$. Standard inverse-CDF method. **Correct.**

Guards:
- $U$ clamped to $[10^{-10}, 1)$. Max $\Delta t = -\ln(10^{-10})/\Lambda = 23.03/\Lambda$.
- $\Lambda \leq 0$ or non-finite → return $10^9$ seconds (effectively ends session).

### 2.3 Event Type Selection

From `competing_intensity_sampler.cpp:25-39`:

```cpp
EventType CompetingIntensitySampler::sampleType(const Intensities& intens) {
    const double total = intens.total();
    if (total <= 0.0 || !std::isfinite(total)) return EventType::ADD_BID;
    const double u = rng_->uniform();
    double cum = 0.0;
    const EventType types[] = {
        EventType::ADD_BID, EventType::ADD_ASK, EventType::CANCEL_BID,
        EventType::CANCEL_ASK, EventType::EXECUTE_BUY, EventType::EXECUTE_SELL
    };
    for (EventType t : types) {
        cum += intens.at(t);
        if (u < cum / total) return t;
    }
    return EventType::EXECUTE_SELL;
}
```

$P(\text{type} = i) = \lambda_i / \Lambda$. Standard inverse-CDF categorical draw. **Correct.**

Iteration order doesn't matter for correctness. Final fallback handles $U$ rounding to 1.0.

### 2.4 ADD Level Selection

From `unit_size_attribute_sampler.cpp:10-27`:

```cpp
size_t UnitSizeAttributeSampler::sampleLevelIndex(size_t num_levels) {
    // ...
    for (size_t k = 0; k < n; ++k) {
        weight_buf_[k] = std::exp(-alpha_ * static_cast<double>(k));
        total += weight_buf_[k];
    }
    // ... categorical draw from weights ...
}
```

Level $k$ is chosen with weight $w_k = e^{-\alpha k}$.

$$P(\text{level}=k \mid \text{ADD}) = \frac{e^{-\alpha k}}{\sum_{j=0}^{K-1} e^{-\alpha j}}$$

$$P(\text{level}=0 \mid \text{ADD}) = \frac{1}{\sum_{j=0}^{K-1} e^{-\alpha j}} = \frac{1 - e^{-\alpha}}{1 - e^{-\alpha K}}$$

**Concrete values** (default $\alpha=0.5$, $K=5$):

| k | $e^{-0.5k}$ | Normalized |
|---|---|---|
| 0 | 1.0000 | 0.4286 |
| 1 | 0.6065 | 0.2600 |
| 2 | 0.3679 | 0.1577 |
| 3 | 0.2231 | 0.0957 |
| 4 | 0.1353 | 0.0580 |
| **Σ** | **2.3329** | **1.0000** |

$$P_0 = P(\text{level}=0 \mid \text{ADD}) = 0.4286$$

**So 43% of adds land at best.** This is the effective replenishment rate at the best level:

$$\lambda^L_{\text{bid, eff at best}} = L \cdot (1 - I) \cdot P_0$$

### 2.5 CANCEL Level Selection — Consistency Audit

**Intensity formula:** $\lambda^C_\text{bid} = C \cdot Q^b_\text{total}$

**Level selection** from `unit_size_attribute_sampler.cpp:29-46`:

```cpp
size_t UnitSizeAttributeSampler::sampleCancelLevelIndex(bool is_bid, const IOrderBook& book) {
    // ...
    for (size_t k = 0; k < n; ++k) {
        const uint32_t d = is_bid ? book.bidDepthAtLevel(k) : book.askDepthAtLevel(k);
        weight_buf_[k] = static_cast<double>(d);
        total += weight_buf_[k];
    }
    // ... categorical draw from weights ...
}
```

$$P(\text{level}=k \mid \text{CANCEL\_BID}) = \frac{q^b_k}{\sum_j q^b_j} = \frac{q^b_k}{Q^b_\text{total}}$$

**Effective cancel rate at level $k$:**

$$\lambda^C_{\text{bid, eff at level } k} = \lambda^C_\text{bid} \cdot \frac{q^b_k}{Q^b_\text{total}} = C \cdot Q^b_\text{total} \cdot \frac{q^b_k}{Q^b_\text{total}} = C \cdot q^b_k$$

**This IS self-consistent.** The effective cancel rate at any level $k$ is exactly $C \cdot q^b_k$, which matches the classical "each order independently cancellable at rate $C$" interpretation.

---

## Section 3 — Net Drift at Best Level

### 3.1 Deriving E[dq₀ᵇ / dt] From the Implemented Code

Three processes affect $q^b_0$:

| Process | Rate at best bid | Sign |
|---|---|---|
| ADD_BID lands at level 0 | $L \cdot (1-I) \cdot P_0$ | $+1$ |
| CANCEL_BID targets level 0 | $C \cdot q^b_0$ | $-1$ |
| EXECUTE_SELL | $M \cdot (\varepsilon + \max(I,0))$ | $-1$ |

$$\boxed{E\!\left[\frac{dq^b_0}{dt}\right] = L(1-I) \cdot P_0 - C \cdot q^b_0 - M(\varepsilon + \max(I,0))}$$

### 3.2 Equilibrium Depth

Setting drift to zero and solving for $q^b_0$ at $I=0$:

$$q^{b*}_0 = \frac{L \cdot P_0 - M \varepsilon}{C}$$

If $L \cdot P_0 > M \varepsilon$, the equilibrium depth is **positive** — the queue stabilizes at a finite depth. Shifts require stochastic fluctuations to push below zero.

If $L \cdot P_0 < M \varepsilon$, the equilibrium is "negative" — the queue is always being drained, and shifts happen rapidly.

### 3.3 Concrete Numbers

**Default Parameters** ($L=20, M=5, C=0.1, \varepsilon=0.2, K=5, \alpha=0.5$):

$$q^{b*}_0 = \frac{20 \times 0.4286 - 5 \times 0.2}{0.1} = \frac{8.571 - 1.0}{0.1} = 75.7$$

With `initial_depth=50`, depth will **grow** toward 75.7. Combined with the initial depth of 50 at all 5 levels (total bid = 250), the system starts in an accumulating regime. Price shifts require $q^b_0$ to hit 0 from ~76 through stochastic fluctuation alone — a near-impossible first-passage event.

**Conclusion: defaults produce a system where shifts are astronomically rare.**

**Debug Preset** ($L=5, M=30, C=0.1, \varepsilon=0.2, K=5, \alpha=0.5$):

$$q^{b*}_0 = \frac{5 \times 0.4286 - 30 \times 0.2}{0.1} = \frac{2.143 - 6.0}{0.1} = -38.6$$

Negative equilibrium — the queue is always draining. Depletion happens rapidly. **Shifts are frequent.** This is why the Debug Preset works.

### 3.4 Answers

**1. Is drift always positive?**

No. It depends on parameters. At the default parameters, drift is positive (accumulating) when $q^b_0 < 75.7$. The queue grows toward equilibrium, not depletes. Drift turns negative only when $q^b_0 > 75.7$, which keeps the queue near 76 — far from 0.

**2. Is depletion even possible under defaults?**

Theoretically yes (any random walk can reach 0), but the mean first-passage time from 76 to 0 is exponentially large. In a 30-second session with $\Lambda \approx 30$, only ~900 events occur, nowhere near enough for the random walk to traverse 76 units.

**3. Under what imbalance does drift turn negative?**

Setting $E[dq^b_0/dt] < 0$:

$$L(1-I) \cdot P_0 - C \cdot q^b_0 - M(\varepsilon + \max(I,0)) < 0$$

At $q^b_0 = 0$ (the depletion threshold) with defaults:

For $I \geq 0$: $20(1-I)(0.4286) < 5(0.2 + I)$
→ $8.571 - 8.571I < 1.0 + 5I$
→ $7.571 < 13.571I$
→ $I > 0.558$

So the bid queue can only drain when $I > 0.56$, meaning bid depth is more than ~3.5x the ask depth. This is a very lopsided state that rarely occurs spontaneously.

---

## Section 4 — Unit Consistency Audit

### 4.1 Interpretation of L, M, C

| Parameter | Dimension | Verification |
|---|---|---|
| $L$ | events/second | Directly used as $\lambda$; confirmed by $\Delta t$ units |
| $M$ | events/second | $M \cdot (\varepsilon + \ldots)$ where $\varepsilon$ is dimensionless → events/sec |
| $C$ | 1/(second·share) | $C \cdot q$ [shares] → events/sec |

All consistent. Intensities are in events/second.

### 4.2 Does $\Delta t \sim \text{Exp}(\Lambda)$ assume $\Lambda$ in 1/seconds?

Yes. $\Delta t = -\ln(U)/\Lambda$ produces seconds when $\Lambda$ is in events/second. Confirmed by `t_ += dt` where `t_` is compared against `session_seconds_` (a double in seconds).

### 4.3 Is the time axis meaningful?

Yes. `session_seconds` from the UI is real simulated clock time. At default parameters with $\Lambda \approx 70$:

- Mean inter-arrival: $1/\Lambda \approx 14$ ms
- In a 30-second session: ~2100 events

Timestamps are recorded as `ts_ns = t_ * 1e9` (nanoseconds). At $t = 30$, $\text{ts\_ns} = 3 \times 10^{10}$, well within `uint64_t` range.

**Caveat:** `double` has ~15.7 significant digits. At $t=30$, the fractional resolution is $30 / 10^{15.7} \approx 6 \times 10^{-15}$ seconds = 6 femtoseconds. After multiplying by $10^9$, nanosecond precision is maintained. No issue for sessions up to ~$10^7$ seconds.

### 4.4 Implicit time rescaling?

None. The model operates in wall-clock seconds. No normalization or scaling is applied between the intensity model and the event loop.

---

## Section 5 — Hidden Bias / Asymmetry Audit

### 5.1 Symmetry at I=0

At $I=0$:
- $\lambda^L_\text{bid} = L = \lambda^L_\text{ask}$ ✓
- $\lambda^M_\text{sell} = M\varepsilon = \lambda^M_\text{buy}$ ✓
- $\lambda^C_\text{bid} = C \cdot Q^b_\text{total}$, $\lambda^C_\text{ask} = C \cdot Q^a_\text{total}$. Equal when total depths are equal. ✓

Under bid↔ask swap ($I \to -I$), the formulas transform correctly:
- $L(1-I) \leftrightarrow L(1+I)$ ✓
- $M(\varepsilon + \max(I,0)) \leftrightarrow M(\varepsilon + \max(-I,0))$ ✓

**The model is symmetric.** No hidden bias.

### 5.2 Does clampNonNegative Introduce Bias?

`clampNonNegative` returns $10^{-9}$ instead of 0. This means:

- At $I = +1$ (extreme): $\lambda^L_\text{bid} = L(1-1) = 0 \to 10^{-9}$. Negligible phantom add rate.
- At $q = 0$ for all levels: $\lambda^C_\text{bid} = C \cdot 0 = 0 \to 10^{-9}$. Negligible phantom cancel rate.

**In practice, $10^{-9}$ events/sec is irrelevant.** With $\Lambda \approx 30$, the probability of sampling a clamped event is $10^{-9}/30 \approx 3 \times 10^{-11}$. No measurable bias.

However: if a cancel event is sampled on an **empty** book (all depths = 0), the cancel level sampler falls through to level 0 (because `total <= 0.0` returns 0), the book apply clamps to `d = 0`, and no harm occurs. **Safe but theoretically unclean.** See **Finding #2**.

### 5.3 Does $\varepsilon_\text{exec}$ Guarantee a Permanent Minimum Market Rate?

Yes. Even at $I=0$:

$$\lambda^M_\text{sell} = M \cdot \varepsilon > 0$$

With defaults: $5 \times 0.2 = 1.0$ events/sec. With Debug: $30 \times 0.2 = 6.0$ events/sec.

**This is by design.** Without $\varepsilon$, at $I=0$ both market rates would be zero and the only drain mechanism would be cancels. The $\varepsilon$ parameter ensures executions always contribute to drain.

### 5.4 Could $\varepsilon_\text{exec}$ Prevent Queues From Growing Large?

Only if $M\varepsilon > L \cdot P_0$. Otherwise, adds at best outpace the permanent execution floor and the queue grows until cancel drag catches up.

With defaults: $M\varepsilon = 1.0$ vs $L \cdot P_0 = 8.57$. Adds dominate. $\varepsilon$ alone does NOT prevent large queues.

With Debug: $M\varepsilon = 6.0$ vs $L \cdot P_0 = 2.14$. Executions dominate. $\varepsilon$ alone prevents accumulation.

**The $\varepsilon$ parameter is the primary lever controlling whether the system is in an accumulating or draining regime.** The ratio $M\varepsilon / (L \cdot P_0)$ is the key dimensionless number. See **Finding #3**.

---

## Section 6 — Stability / Explosion Audit

### 6.1 Does Cancel Scale Linearly With Depth?

**At a single level:** Yes. Effective cancel at level $k$ = $C \cdot q^b_k$. Linear in $q$.

**Total cancel intensity:** $\lambda^C_\text{bid} = C \cdot Q^b_\text{total} = C \cdot \sum_k q^b_k$. Linear in total depth.

### 6.2 Does Add Scale With Depth?

**No.** $\lambda^L_\text{bid} = L \cdot (1-I)$. Depends on imbalance only, not on depth.

The imbalance $I$ depends on best-level depths, introducing an indirect coupling. But for fixed $I$, adds are constant regardless of queue size.

### 6.3 Is There Negative Drift at Large $q$?

Yes. At best level, the drift is:

$$\frac{dq^b_0}{dt} = L(1-I) P_0 - C q^b_0 - M(\varepsilon + \max(I,0))$$

As $q^b_0 \to \infty$, the $-C q^b_0$ term dominates. Drift becomes strongly negative. **No finite-time explosion is possible at a single level.**

### 6.4 Can Depth Explode to Infinity?

**At best level (k=0):** No. Linear cancel drag ensures a finite attractor at $q^{b*}_0 = (LP_0 - M\varepsilon)/C$ (when this is positive).

**At deeper levels (k > 0):** These levels receive adds at rate $L(1-I) \cdot P_k$ and cancels at rate $C \cdot q^b_k$. The equilibrium at level $k$ is:

$$q^{b*}_k = \frac{L(1-I) \cdot P_k}{C}$$

With defaults: $q^{b*}_1 = 20 \times 0.260 / 0.1 = 52.0$, $q^{b*}_2 = 20 \times 0.158 / 0.1 = 31.6$, etc.

Deeper levels don't receive executions (only best level does), so their equilibrium is purely add vs. cancel. All finite. **No explosion possible at any level.**

### 6.5 Can Depth Become Permanently Stuck at High Levels?

Not permanently, but **practically** yes. With default parameters, the best level equilibrium is ~76. The time for a random walk with small negative drift (at equilibrium) to first reach 0 is exponentially long relative to session duration.

With 30-second sessions and ~2100 events, the variance in $q^b_0$ is roughly:

$$\text{Var}(q) \approx (\text{rate of unit changes}) \times \Delta t_\text{session} \approx (LP_0 + Cq^* + M\varepsilon) \times 30 \approx (8.6 + 7.6 + 1.0) \times 30 \approx 516$$

$$\sigma \approx \sqrt{516} \approx 23$$

Starting at $q=76$, reaching 0 requires a $-3.3\sigma$ fluctuation — probability $\approx 0.05\%$. **Price shifts are possible but extremely rare under defaults.**

---

## Section 7 — Critical Findings

### Finding #1 — Dead Fields in IntensityParams

**Severity:** Minor
**Description:** `imbalance_sensitivity` and `cancel_sensitivity` are declared in `IntensityParams` but never read by `SimpleImbalanceIntensity::compute()` or any other code.
**Location:** `src/core/records.h:33-34`
**Why it matters:** Dead fields create confusion about what the model actually does. A reader might expect these to scale the imbalance or cancel terms, but they have no effect.
**Suggested correction:** Remove the fields, or wire them into the formulas as:

$$\lambda^L_\text{bid} = L \cdot (1 - \sigma_I \cdot I), \qquad \lambda^C_\text{bid} = C \cdot \sigma_C \cdot Q^b_\text{total}$$

where $\sigma_I$ = `imbalance_sensitivity`, $\sigma_C$ = `cancel_sensitivity`.

---

### Finding #2 — clampNonNegative Allows Phantom Events on Empty Book

**Severity:** Minor
**Description:** When all depths are 0, the mathematical cancel rate is $C \cdot 0 = 0$. But `clampNonNegative` forces it to $10^{-9}$. If sampled (probability $\sim 10^{-11}$), the cancel fires on an empty book. The apply logic clamps `d = 0` harmlessly, but if `idx == 0`, this triggers `shiftBidBook()` on an already-empty best level, causing a cascade shift with `initial_depth` reinit. An impossible market event (cancelling nothing) causes a price move.
**Location:** `simple_imbalance_intensity.cpp:13-15`, `multi_level_book.cpp:80-87`
**Why it matters:** Theoretically incorrect — a cancel on a zero-depth level should not occur. In practice the probability is negligible ($\sim 10^{-11}$ per event), but the code path is logically wrong.
**Suggested correction:** In `multi_level_book.cpp`, the cancel handler already checks `if (d >= e.qty) d -= e.qty; else d = 0;` — when d is already 0 and e.qty is 1, it sets d to 0 and then the `if (idx == 0 && d == 0)` triggers a shift. Guard with: only shift if the depth **changed** from nonzero to zero.

---

### Finding #3 — UI Drift Diagnostic Uses Wrong Formula

**Severity:** Important
**Description:** The UI computes the "drift at best" diagnostic as:

```cpp
const double net_bid_drift = intens.exec_sell + intens.cancel_bid - intens.add_bid;
```

This treats the full `cancel_bid` (total across all levels) and full `add_bid` (total across all levels) as if they all target the best level. The correct formula for drift at best is:

$$\text{drain} - \text{replenish} = \left[\lambda^M_\text{sell} + C \cdot q^b_0\right] - \left[L(1-I) \cdot P_0\right]$$

**Concrete example at defaults** ($I=0$, $q^b_0=50$, $Q^b_\text{total}=250$):

| | UI formula | True at-best formula |
|---|---|---|
| Exec drain | $M\varepsilon = 1.0$ | $1.0$ |
| Cancel drain | $C \cdot 250 = 25.0$ | $C \cdot 50 = 5.0$ |
| Add replenish | $L = 20.0$ | $L \cdot P_0 = 8.57$ |
| **Net drain** | $1 + 25 - 20 = +6.0$ | $1 + 5 - 8.57 = -2.57$ |
| **Interpretation** | "Draining" | **"Accumulating"** |

**The sign flips.** The UI says the system is draining when it is actually accumulating at the best level. The "Depth accumulating regime" warning (line 418) checks `net_bid_drift < 0.0`, meaning it only fires when the UI formula says "accumulating." Since the UI overestimates drain, it **fails to warn** precisely when it should.

**Location:** `tools/qrsdp_ui/main.cpp:410-411`
**Suggested correction:** Replace with effective at-best rates:

```cpp
const double P0_bid = computeP0(ui_alpha, ui_levels_per_side);
const double eff_cancel_bid = params.base_C * state.features.q_bid_best;
const double eff_add_bid = intens.add_bid * P0_bid;
const double net_bid_drift = intens.exec_sell + eff_cancel_bid - eff_add_bid;
```

---

### Finding #4 — Default Parameters Create Non-Shifting Regime

**Severity:** Important
**Description:** The default UI parameters ($L=20, M=5, C=0.1, \varepsilon=0.2$, `initial_depth=50`, $K=5$, $\alpha=0.5$) produce an equilibrium best-level depth of:

$$q^{b*}_0 = \frac{L \cdot P_0 - M\varepsilon}{C} = \frac{8.57 - 1.0}{0.1} = 75.7$$

The system accumulates depth toward ~76. Price shifts require first-passage to 0, which is exponentially unlikely in a 30-second session. Users launching the UI for the first time will see a flat price line and conclude the simulator is broken.

The Debug Preset ($L=5, M=30$) produces $q^{b*}_0 = -38.6$ (always draining), which works correctly. But it is a hidden button, not the default.

**Location:** `tools/qrsdp_ui/main.cpp:112-120` (default parameter values)
**Suggested correction:** Either:
- (a) Change defaults to the Debug Preset values, or
- (b) Make the Debug Preset the initial state, or
- (c) Add a startup warning when $L \cdot P_0 > M\varepsilon$ indicating shifts will be rare.

---

### Finding #5 — Imbalance Uses Best-Level Depth Only; Cancel Uses Total Depth

**Severity:** Minor (modeling asymmetry, not a bug)
**Description:** Imbalance is computed from best-level depths only:

$$I = \frac{q^b_0 - q^a_0}{q^b_0 + q^a_0 + \varepsilon}$$

But cancel intensity uses total depth across all levels:

$$\lambda^C_\text{bid} = C \cdot \sum_k q^b_k$$

This means the imbalance signal (which drives add and execute rates) can be zero even when one side has massive total depth, as long as the **best** levels are balanced. Meanwhile, cancel intensity reflects the full book.

**Why it matters:** Consider a state where $q^b_0 = q^a_0 = 5$ (balanced at best, $I=0$) but total bid depth is 500 and total ask depth is 50. The imbalance signal says "balanced" ($I=0$), so exec rates are symmetric. But cancel_bid = $C \cdot 500 = 50$ while cancel_ask = $C \cdot 50 = 5$. The bid side drains 10x faster via cancels, not because of any market signal, but because of accumulated deep-level inventory.

This is not necessarily wrong — it's a modeling choice. But the add/execute rates being driven by best-level-only imbalance while cancel is driven by total depth creates a disconnect. The model has no mechanism to adjust add/execute behavior based on deep-level inventory.

**Location:** `multi_level_book.cpp:59-60` (imbalance), `simple_imbalance_intensity.cpp:29-42` (intensities)
**Suggested correction:** Document this as an intentional modeling choice, or consider an alternative imbalance definition using total depths: $I_\text{total} = (Q^b_\text{total} - Q^a_\text{total}) / (Q^b_\text{total} + Q^a_\text{total} + \varepsilon)$.

---

### Finding #6 — Add Rate Increases Toward Depleted Side (Counterintuitive)

**Severity:** Minor (modeling question)
**Description:** When $I > 0$ (bid heavier), $\lambda^L_\text{bid} = L(1-I)$ **decreases** and $\lambda^L_\text{ask} = L(1+I)$ **increases**. The model sends more limit orders to the **lighter** (ask) side.

Simultaneously, $\lambda^M_\text{sell} = M(\varepsilon + I)$ increases — more market sells hit the heavier bid side.

The combined effect is mean-reverting: the heavy side gets more executions (draining it) and fewer adds, while the light side gets more adds and fewer executions.

**This is mathematically coherent as a mean-reverting model**, but empirically, when the bid side is heavy, real markets often see more adds on the bid side (momentum traders joining the queue), not fewer. The model assumes pure mean-reversion, which limits its realism for trending regimes.

**Not a bug.** Documented here for completeness.

---

### Summary Table

| # | Finding | Severity | Category |
|---|---|---|---|
| 1 | Dead `imbalance_sensitivity` / `cancel_sensitivity` fields | Minor | Dead code |
| 2 | Phantom cancel events on empty book via clamp floor | Minor | Edge case |
| 3 | UI drift diagnostic uses wrong effective-at-best formula | Important | Diagnostic bug |
| 4 | Default parameters create non-shifting regime | Important | Usability |
| 5 | Imbalance (best-only) vs cancel (total) depth disconnect | Minor | Modeling |
| 6 | Add rate mean-reverts (intentional but limits realism) | Minor | Modeling |

---

Which issue would you like to fix first?
