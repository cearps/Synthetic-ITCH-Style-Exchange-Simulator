# QRSDP Calibration Scaffold

This document describes the **calibration module** for HLR2014-style intensity estimation and curve I/O: how to calibrate from simulated event streams (replay + book state), the intensity estimator, and saving/loading intensity curves as JSON.

---

## 1. Overview

The scaffold supports calibration from **simulated** event streams today:

1. **Calibration from simulator** — replay `EventRecord`s through a `MultiLevelBook`; at each event you have the book state (queue sizes) and can record sojourns for the estimator. No separate event-log parser is needed.
2. **IntensityEstimator** — accumulate sojourn data (queue size, dwell time, event type) and compute MLE intensity estimates.
3. **Curve JSON I/O** — save and load `IntensityCurve` tables to/from JSON for use in `CurveIntensityModel` / `HLRParams`.

**Location:** `src/qrsdp/intensity_estimator.*`, `intensity_curve_io.*`

When we add support for **external** event logs (e.g. ITCH), we will introduce an event-log parser that reconstructs queue state from the raw stream; until then, calibration uses replay + book state only.

---

## 2. Calibration from simulator: replay + book state

When the event stream comes from our own producer (e.g. `InMemorySink` of `EventRecord`s), you already have a way to get queue state: **replay the same events through a `MultiLevelBook`** (or the same book).

### Pipeline

1. Run a session: producer writes `EventRecord`s to `InMemorySink`.
2. Replay: seed a `MultiLevelBook` with the same `BookSeed`, then for each record in order:
   - You have **queue state before the event** (from the book: `bidDepthAtLevel(k)`, `askDepthAtLevel(k)`).
   - You have **time** from the previous record (or session start) so you can compute **dwell time** Δt at that state.
   - Build a `SimEvent` from the record and **apply** it to the book.
   - The **event type** is in the record.
3. For each event, record a sojourn: queue size **n** (e.g. at best level), **Δt** since last event, and **type** that occurred. Feed these into `IntensityEstimator::recordSojourn(n, dt, type)`.

No separate “parser” that reconstructs the book from the log is required—the book *is* the state. An event-log parser will be added when we support **external** feeds (e.g. ITCH) where we only have the event stream and must reconstruct the book.

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

### Usage sketch (replay)

```cpp
qrsdp::IntensityEstimator est;
est.reset();
// Replay: for each EventRecord (after applying previous), you have book state and dt
double t_prev = 0.0;
for (const auto& rec : sink.events()) {
    double t = rec.ts_ns * 1e-9;
    double dt = t - t_prev;
    uint32_t n = book.bidDepthAtLevel(0);  // e.g. best bid queue size
    est.recordSojourn(n, dt, static_cast<EventType>(rec.type));
    // apply rec to book, then ...
    t_prev = t;
}
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

- **IntensityEstimator.LambdaTotalAndType** — record sojourns at n=5, check Λ̂(5) and λ̂_type(5).
- **IntensityCurveIo.SaveAndLoad** — save a 3-point curve to JSON, load it back, check values and nMax().

Run the full test suite (e.g. `ctest` or the `tests` target) to execute these.

---

## 6. Roadmap (for later)

- **Event-log parser:** when adding ITCH (or similar) support, introduce a parser that reconstructs queue state from the raw event stream so we can calibrate from external data without replaying through our book.
- **IntensityEstimator:** optional weighting (e.g. by time of day), per-level curves (bid/ask, level index).
- **Curve I/O:** optional HLRParams-level JSON (all curves for one symbol/side), validation and schema hints.
- **Calibration pipeline:** replay EventRecord stream → book state + sojourn extraction → IntensityEstimator → export curves to JSON → load into `CurveIntensityModel` for simulation. (For external logs: event parser → reconstructed state + sojourns → same pipeline.)
