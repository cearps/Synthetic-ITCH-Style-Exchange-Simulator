# QRSDP Debug UI

Minimal real-time debugging UI for the QRSDP producer: ImGui + ImPlot + GLFW + OpenGL3. Lets you see why price shifts do or do not occur and inspect order book evolution.

## Build

From the **repository root** (not from `tools/qrsdp_ui`). The `tools/` directory must be present (the Docker test image does not copy it; build qrsdp_ui locally):

```bash
mkdir -p build && cd build
cmake -DBUILD_QRSDP_UI=ON ..
cmake --build . --target qrsdp_ui
```

First run will fetch **Dear ImGui**, **ImPlot**, and **GLFW** via CMake FetchContent (requires network and git).

To disable the UI and avoid fetching these deps:

```bash
cmake -DBUILD_QRSDP_UI=OFF ..
```

If `tools/qrsdp_ui` is missing, the qrsdp_ui target is not added (e.g. in CI/Docker).

## Run

From the build directory:

```bash
./qrsdp_ui
```

Or from repo root:

```bash
./build/qrsdp_ui
```

## What the UI shows

- **Controls (left):** Seed, session length, levels, tick size, initial depth/spread. Model selector: **Legacy (SimpleImbalance)** or **HLR2014 (CurveIntensity)**. Model-specific controls appear based on selection. Buttons: **Reset**, **Step 1**, **Step N**, **Run** / Pause, **Debug Preset**, **Production Preset**. Slider: max events per frame.
  - **SimpleImbalance controls:** base_L, base_M, base_C, epsilon_exec, spread_sens.
  - **HLR2014 controls:** spread_sens (HLR), imbalance_sens (HLR), theta_reinit, reinit_mean, curve preset, Nmax.
  - **Attribute sampler:** alpha (level decay), spread_improve (spread-improving order coefficient).
- **Top-of-book / Diagnostics (top-right):** Time, event count, best bid/ask, spread, depths, imbalance, event-type counts, shift counters (up/down/total), last shift time and prices, invariant warnings (red if bid >= ask, negative depth, or spread < 1).
- **Price over time:** ImPlot graph of mid (and optional bid/ask lines) vs time; optional shift markers (scatter when a shift occurs).
- **Depth at best:** Two lines over "event index" for best bid depth and best ask depth.
- **Order book ladder:** Top N levels each side (price + depth); best bid/ask rows highlighted.
- **Recent events:** Last 200 events (t, type, side, price, qty, order_id). Rows marked "SHIFT UP" / "SHIFT DOWN" when a shift occurs.

## Quick check that shifts appear

1. Click **Production Preset** (initial_depth=5, spread feedback enabled, realistic parameters).
2. Click **Run**.
3. Watch **Price over time** and **Shifts** in diagnostics; you should see shift markers and increasing shift counts.

Alternatively, click **Debug Preset** (initial_depth=1, no spread feedback) for faster shifts with minimal depth.


## Requirements

- C++17
- CMake 3.15+
- OpenGL 3.3 (or compatible)
- On Linux: X11 / Wayland (GLFW handles it)
- On Windows: build with Visual Studio or MinGW; link OpenGL32
- On macOS: OpenGL framework
