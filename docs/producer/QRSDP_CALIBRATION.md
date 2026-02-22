# QRSDP Calibration Scaffold

This document describes the **calibration module** added for HLR2014-style intensity estimation and curve I/O: event log parsing, intensity estimation from sojourn data, and saving/loading intensity curves as JSON.

---

## 1. Overview

The scaffold supports future calibration from real or simulated event streams:

1. **EventLogParser** — consume event records and reconstruct queue depths (level I/II).
2. **IntensityEstimator** — accumulate sojourn data (queue size, dwell time, event type) and compute MLE intensity estimates.
3. **Curve JSON I/O** — save and load `IntensityCurve` tables to/from JSON for use in `CurveIntensityModel` / `HLRParams`.

**Location:** `src/qrsdp/event_log_parser.*`, `intensity_estimator.*`, `intensity_curve_io.*`

---

## 2. Event log parser

**Header:** `src/qrsdp/event_log_parser.h`

The parser maintains **reconstructed bid/ask depths** and best bid/ask from a stream of `EventRecord`s. It is a scaffold for later ITCH-like decoding (e.g. level I/II from exchange messages).

### API

| Method / field | Purpose |
|----------------|--------|
| `reset()` | Clear state for a new symbol/session. |
| `push(rec)` | Process one `EventRecord`; update `bid_depths`, `ask_depths`, best bid/ask. Returns `true` if consumed. |
| `bid_depths`, `ask_depths` | Reconstructed depths per level (index 0 = best). |
| `best_bid_ticks`, `best_ask_ticks` | Current best prices (0 if unknown). |
| `event_count` | Number of events pushed. |

### Event handling

- **ADD_BID / ADD_ASK** — increment depth at the level corresponding to `price_ticks`; set best bid/ask on first add.
- **CANCEL_BID / CANCEL_ASK** — decrement depth at level (clamped to 0).
- **EXECUTE_BUY / EXECUTE_SELL** — decrement depth at best ask / best bid respectively.

**Limitation:** The scaffold does not yet handle **reference price shifts** (e.g. when best queue empties and the book moves). A full ITCH-style parser would update best bid/ask and level indices on shift.

---

## 3. Intensity estimator

**Header:** `src/qrsdp/intensity_estimator.h`

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

### Usage sketch

```cpp
qrsdp::IntensityEstimator est;
est.reset();
// For each sojourn (e.g. from replay or live stream):
//   - queue size n, dwell time dt, and the event type that occurred
est.recordSojourn(n, dt, EventType::ADD_BID);
// ...
double lambda_tot = est.lambdaTotal(n);
double lambda_add = est.lambdaType(n, EventType::ADD_BID);
```

You can then build an `IntensityCurve` from the estimates (e.g. for n = 0 .. nMaxObserved()) and pass it into `HLRParams` or save via curve JSON I/O.

---

## 4. Curve JSON format and I/O

**Header:** `src/qrsdp/intensity_curve_io.h`

### JSON format

A single curve is stored as:

```json
{"values": [v0, v1, v2, ...], "tail": "FLAT"}
```

or

```json
{"values": [v0, v1, ...], "tail": "ZERO"}
```

- **values** — array of nonnegative numbers; index i corresponds to queue size n = i (0, 1, 2, …).
- **tail** — behaviour for n > last index: `"FLAT"` uses the last value; `"ZERO"` uses 0.

### API

| Function | Purpose |
|----------|--------|
| `saveCurveToJson(path, curve)` | Write the curve’s table and tail rule to a JSON file. Returns `false` on I/O error or if curve is empty. |
| `loadCurveFromJson(path, curve)` | Read JSON from `path`, set curve via `setTable(...)` and tail. Returns `false` on error. |

### Example: save default HLR curves

```cpp
#include "qrsdp/hlr_params.h"
#include "qrsdp/intensity_curve_io.h"

qrsdp::HLRParams p = qrsdp::makeDefaultHLRParams(5, 100);
qrsdp::saveCurveToJson("lambda_M_buy.json", p.lambda_M_buy);
```

### Example: load a curve into HLRParams

```cpp
qrsdp::IntensityCurve curve;
if (qrsdp::loadCurveFromJson("calibrated_add_best.json", curve)) {
    p.lambda_L_bid[0] = std::move(curve);
}
```

No external JSON library is used; the implementation uses minimal in-repo parsing and formatting.

---

## 5. Tests

**File:** `tests/qrsdp/test_calibration.cpp`

- **EventLogParser.ResetAndPush** — reset and push one ADD_BID; checks event_count and best_bid_ticks.
- **IntensityEstimator.LambdaTotalAndType** — record sojourns at n=5, check Λ̂(5) and λ̂_type(5).
- **IntensityCurveIo.SaveAndLoad** — save a 3-point curve to JSON, load it back, check values and nMax().

Run the full test suite (e.g. `ctest` or the `tests` target) to execute these.

---

## 6. Roadmap (for later)

- **EventLogParser:** extend to read raw ITCH or similar messages; handle reference price shifts and level re-mapping.
- **IntensityEstimator:** optional weighting (e.g. by time of day), per-level curves (bid/ask, level index).
- **Curve I/O:** optional HLRParams-level JSON (all curves for one symbol/side), validation and schema hints.
- **Calibration pipeline:** replay EventRecord stream → EventLogParser + sojourn extraction → IntensityEstimator → export curves to JSON → load into `CurveIntensityModel` for simulation.
