# QRSDP Calibration Scaffold

This document describes the **calibration module** for HLR2014-style intensity estimation and curve I/O: how to calibrate from simulated event streams (replay + book state), the intensity estimator, and saving/loading intensity curves as JSON.

The queue-reactive intensity model implemented here follows the framework of Huang, Lehalle & Rosenbaum (2015) [[1]](#references), where the limit order book is modelled as a collection of queues whose event intensities depend on the current queue size. For a broader survey of LOB simulation approaches (Poisson, Hawkes, agent-based, deep learning, SPDEs) and the stylised facts used to validate them, see Jain et al. (2024) [[2]](#references).

---

## 1. Overview

The scaffold supports calibration from **simulated** event streams:

1. **Calibration CLI** (`qrsdp_calibrate`) — reads `.qrsdp` event log files, replays through a `MultiLevelBook`, records per-level sojourns, estimates intensity curves via MLE, and saves a complete `HLRParams` JSON file.
2. **IntensityEstimator** — accumulates sojourn data (queue size, dwell time, event type) and computes MLE intensity estimates.
3. **HLR Params JSON I/O** — save and load full `HLRParams` (all per-level curves + metadata) to/from a single JSON file.
4. **Curve JSON I/O** — save and load individual `IntensityCurve` tables.

**Location:** `src/calibration/`, `src/model/hlr_params.*`

---

## 2. Quick Start: End-to-End Calibration

### Step 1: Generate simple-model training data

```bash
./build/qrsdp_run --seed 100 --days 5 --seconds 3600 \
    --model simple --output output/cal_training
```

### Step 2: Calibrate HLR curves

```bash
./build/qrsdp_calibrate \
    --input output/cal_training/2026-01-02.qrsdp \
    --input output/cal_training/2026-01-05.qrsdp \
    --input output/cal_training/2026-01-06.qrsdp \
    --input output/cal_training/2026-01-07.qrsdp \
    --input output/cal_training/2026-01-08.qrsdp \
    --output output/cal_training/hlr_curves.json \
    --verbose
```

### Step 3: Run HLR with calibrated curves

```bash
./build/qrsdp_run --seed 200 --days 5 --seconds 23400 \
    --hlr-curves output/cal_training/hlr_curves.json \
    --output output/hlr_calibrated
```

The `--hlr-curves` flag auto-switches to `--model hlr` if not already set.

---

## 3. Calibration CLI: `qrsdp_calibrate`

```
Usage: qrsdp_calibrate [options]
  --input <file>       Input .qrsdp file (may be repeated)
  --output <file>      Output JSON curves file (default: hlr_curves.json)
  --levels <K>         Levels per side for curves (default: from file header)
  --n-max <n>          Max queue size for tables (default: 100)
  --spread-sens <f>    Spread sensitivity for output (default: 0.3)
  --verbose            Print per-level summaries
```

### How it works

For each input file, the tool:

1. Reads the `.qrsdp` file header to get book configuration (p0, levels, initial depth).
2. Seeds a `MultiLevelBook` with those parameters.
3. For each event record in order:
   - Maps the event to a `(level, side)` pair by matching the event price to book levels.
   - Computes the dwell time since the last event at that level.
   - Records a sojourn `(queue_depth, dt, event_type)` into the per-level `IntensityEstimator`.
   - Applies the event to the book.
   - On price shifts (best bid/ask change), re-snapshots all level trackers to handle index renumbering.
4. After all events, extracts per-level/type intensity curves from the estimators.
5. Saves the complete `HLRParams` as a single JSON file.

### Per-level estimator design

Each `(level, side)` pair gets its own `IntensityEstimator`. The event types recorded are:

- **Bid level k**: `ADD_BID`, `CANCEL_BID`, and `EXECUTE_SELL` (at level 0 only)
- **Ask level k**: `ADD_ASK`, `CANCEL_ASK`, and `EXECUTE_BUY` (at level 0 only)

This decomposition yields separate `λ^L_i(n)`, `λ^C_i(n)`, and `λ^M(n)` curves for each level.

---

## 4. Intensity estimator

**Header:** `src/calibration/intensity_estimator.h`

Implements the HLR MLE idea: estimate total intensity and type-specific intensities from sojourns (queue size, dwell time, and the event type that ended the sojourn).

### Formulas

- **Λ̂(n)** = 1 / mean(Δt | q = n) — total intensity at queue size n.
- **λ̂_type(n)** = Λ̂(n) × freq(type | q = n) — intensity for a specific event type at queue size n.

### API

| Method | Purpose |
|--------|--------|
| `reset()` | Clear all accumulated data. |
| `recordSojourn(n, dt_sec, type)` | Record one sojourn: queue size `n`, dwell time `dt_sec`, and `EventType` that occurred. |
| `lambdaTotal(n)` | Λ̂(n). Returns 0 if no observations for n. |
| `lambdaType(n, type)` | λ̂_type(n). Returns 0 if no observations. |
| `nMaxObserved()` | Largest n with at least one observation. |

---

## 5. HLR Params JSON format and I/O

**Header:** `src/model/hlr_params.h`

### JSON format

A complete HLR parameter set is stored as:

```json
{
  "K": 5,
  "n_max": 100,
  "spread_sensitivity": 0.3,
  "imbalance_sensitivity": 1.0,
  "lambda_L_bid": [
    [v0, v1, v2, ...],
    [v0, v1, v2, ...]
  ],
  "lambda_L_ask": [ ... ],
  "lambda_C_bid": [ ... ],
  "lambda_C_ask": [ ... ],
  "lambda_M_buy": [v0, v1, v2, ...],
  "lambda_M_sell": [v0, v1, v2, ...]
}
```

- **K** — number of levels per side.
- **n_max** — max queue size for table entries.
- **spread_sensitivity** — spread-dependent feedback strength (see §6).
- **imbalance_sensitivity** — imbalance-driven execution feedback strength (see §6).
- **lambda_L_\*** — per-level add intensity curves (array of K arrays).
- **lambda_C_\*** — per-level cancel intensity curves.
- **lambda_M_\*** — market order intensity curves (single arrays).

### API

| Function | Purpose |
|----------|--------|
| `saveHLRParamsToJson(path, params)` | Save complete HLRParams to JSON. |
| `loadHLRParamsFromJson(path, params)` | Load HLRParams from JSON. |

### Example: save calibrated curves

```cpp
qrsdp::HLRParams p;
// ... populate from IntensityEstimator ...
qrsdp::saveHLRParamsToJson("hlr_curves.json", p);
```

### Example: load and use

```cpp
qrsdp::HLRParams p;
if (qrsdp::loadHLRParamsFromJson("hlr_curves.json", p)) {
    auto model = std::make_unique<CurveIntensityModel>(std::move(p));
}
```

---

## 6. Feedback mechanisms

The `CurveIntensityModel` applies two feedback mechanisms that keep the model stable and realistic.

### Spread-dependent feedback (`spread_sensitivity`)

Multipliers applied to add and exec intensities, controlled by `spread_sensitivity` (default 0.3):

- **Add multiplier** = exp(sS × (spread − 2)): wide spread → more limit orders (price improvement).
- **Exec multiplier** = exp(−sS × (spread − 2)): wide spread → fewer market orders.

Neutral at spread=2 ticks (one tick each side of mid). This mirrors the `SimpleImbalanceIntensity` spread feedback.

### Imbalance-driven feedback (`imbalance_sensitivity`)

Multipliers applied to execution intensities, controlled by `imbalance_sensitivity` (default 1.0):

- **Imbalance** = (total_bid_depth − total_ask_depth) / (total_bid_depth + total_ask_depth), in \[−1, 1\].
- **exec_sell multiplier** = 1 + iS × max(imbalance, 0): boosted when bid side is heavier (pushes price down).
- **exec_buy multiplier** = 1 + iS × max(−imbalance, 0): boosted when ask side is heavier (pushes price up).

This creates mean-reverting price dynamics: when one side accumulates depth, executions on that side increase, draining it back towards balance.

---

## 7. Default HLR curves

`makeDefaultHLRParams()` produces curves tuned to match the empirical findings of HLR2014:

| Curve | Shape | Rationale |
|-------|-------|-----------|
| addBest(n) | 15 / (1 + 0.12n) | Decreasing — arrival rate falls with existing depth |
| addDeeper(n) | 5 / (1 + 0.2n) | Slower at deeper levels to keep total event rate reasonable |
| cancelCurve(n) | 0.3n / (1 + 0.02n) | Concave — saturates at ~15, matching empirical shape |
| marketCurve(n) | 0 at n=0, 8 constant | Constant rate independent of depth, so queues can drain |

Steady-state best-level depth ≈ 5–6. Total event rate ≈ 69/s (comparable to SimpleImbalance's ~72/s). Event mix: ~50% adds, ~19% cancels, ~31% executions. Price shifts at ~10.7/s.

---

## 8. Individual Curve JSON I/O

**Header:** `src/calibration/intensity_curve_io.h`

A single curve is stored as:

```json
{"values": [v0, v1, v2, ...], "tail": "FLAT"}
```

| Function | Purpose |
|----------|--------|
| `saveCurveToJson(path, curve)` | Save one curve to JSON. |
| `loadCurveFromJson(path, curve)` | Load one curve from JSON. |

---

## 9. Tests

**File:** `tests/qrsdp/test_calibration.cpp`

- **IntensityEstimator.LambdaTotalAndType** — record sojourns at n=5, verify Λ̂(5) and λ̂_type(5).
- **IntensityCurveIo.SaveAndLoad** — save a 3-point curve to JSON, load it back, verify values.
- **HLRParamsIo.SaveAndLoadRoundTrip** — save full default HLRParams, load back, verify all curves match.
- **HLRParamsIo.LoadBadPathFails** — loading nonexistent file returns false.
- **HLRParams.DefaultsHaveSpreadSensitivity** — verify defaults have spread_sensitivity=0.3 and marketCurve(0)=0.

Run: `ctest` or the `tests` target (94 tests total).

---

## 10. Roadmap

- **Smoothing/regularization:** kernel smoothing or parametric fitting to reduce noise in estimated curves from limited data.
- **Event-log parser:** when adding ITCH support, introduce a parser that reconstructs queue state from raw feeds.
- **Multi-security calibration:** calibrate per-security curves from multi-security runs.
- **Time-of-day weighting:** optional intraday weighting in the estimator.

---

## References

1. W. Huang, C.-A. Lehalle, and M. Rosenbaum, "Simulating and Analyzing Order Book Data: The Queue-Reactive Model," *Journal of the American Statistical Association*, vol. 110, no. 509, pp. 107–122, 2015. [arXiv:1312.0563](https://arxiv.org/abs/1312.0563)

2. K. Jain, N. Firoozye, J. Kochems, and P. Treleaven, "Limit Order Book Simulations: A Review," *arXiv preprint*, 2024. [arXiv:2402.17359](https://arxiv.org/abs/2402.17359)
