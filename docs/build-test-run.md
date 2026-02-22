# Build, Test, and Run

## Prerequisites

| Requirement | Minimum | Notes |
|---|---|---|
| CMake | 3.15+ | |
| C++17 compiler | MSVC 2017+, GCC 7+, Clang 5+ | |
| Git | any | FetchContent pulls GoogleTest, GLFW, ImGui, ImPlot |
| OpenGL | 3.x+ | Required only for `qrsdp_ui` |

Optional:

- Docker Desktop (for headless Linux builds / CI)

## Build Targets

| Target | Description | CMake option |
|---|---|---|
| `simulator_lib` | Static library (all QRSDP components) | always built |
| `qrsdp_cli` | Headless CLI session runner | always built |
| `tests` | Google Test suite (51 cases) | `BUILD_TESTING=ON` (default) |
| `qrsdp_ui` | ImGui real-time debugging UI | `BUILD_QRSDP_UI=ON` (default) |

---

## Building

### Windows (MSVC / Visual Studio)

Visual Studio generators produce multi-config builds. Use `--config Release` (or `Debug`) at build time rather than at configure time.

```powershell
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

Outputs:

```
build/Release/qrsdp_cli.exe
build/Release/tests.exe
build/tools/qrsdp_ui/Release/qrsdp_ui.exe
```

> **Tip:** If rebuilding fails with `LNK1168: cannot open ... for writing`, close the running executable first.

### macOS (Xcode or Makefiles)

**Xcode generator** (multi-config, default on macOS when Xcode is installed):

```bash
mkdir build && cd build
cmake .. -G Xcode
cmake --build . --config Release
```

**Unix Makefiles** (single-config, lighter weight):

```bash
mkdir build && cd build
cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Outputs (Makefiles):

```
build/qrsdp_cli
build/tests
build/tools/qrsdp_ui/qrsdp_ui
```

Outputs (Xcode):

```
build/Release/qrsdp_cli
build/Release/tests
build/tools/qrsdp_ui/Release/qrsdp_ui
```

> **Note:** macOS links OpenGL via the system framework. No extra install is needed; CMake's `find_package(OpenGL)` handles it.

### Linux (GCC / Clang)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Outputs:

```
build/qrsdp_cli
build/tests
build/tools/qrsdp_ui/qrsdp_ui
```

> **Note:** The UI target requires OpenGL dev headers. On Debian/Ubuntu:
> `sudo apt install libgl-dev libglfw3-dev` (GLFW is also fetched by CMake, but the system GL headers are still needed).

### Headless Build (skip the UI)

To build without `qrsdp_ui` (e.g. on a headless server with no GPU):

```bash
cmake .. -DBUILD_QRSDP_UI=OFF
```

### Docker (Linux, headless only)

```bash
docker-compose -f docker/docker-compose.yml build build
docker-compose -f docker/docker-compose.yml run --rm test
docker-compose -f docker/docker-compose.yml run --rm simulator
```

The Docker image builds with GCC on Ubuntu 22.04. The UI is not included (no display server). The `simulator` service runs `qrsdp_cli 12345 30` by default.

---

## Running

### CLI — `qrsdp_cli`

Runs a single QRSDP session and prints a one-line summary.

```
Usage: qrsdp_cli [seed] [seconds]
```

| Arg | Default | Description |
|---|---|---|
| `seed` | 42 | RNG seed (`uint64_t`) |
| `seconds` | 30 | Simulated trading session length |

**Windows:**

```powershell
.\build\Release\qrsdp_cli.exe 42 30
```

**macOS / Linux:**

```bash
./build/qrsdp_cli 42 30
```

Example output:

```
seed=42  seconds=30  events=78432  close=10012  shifts=217
```

### Debugging UI — `qrsdp_ui`

Real-time visualisation of price, book depth, intensities, drift diagnostics, and event counts. Supports both the Legacy (SimpleImbalance) and HLR2014 (CurveIntensity) models.

**Windows:**

```powershell
.\build\tools\qrsdp_ui\Release\qrsdp_ui.exe
```

**macOS / Linux:**

```bash
./build/tools/qrsdp_ui/qrsdp_ui
```

> The UI requires a display and GPU with OpenGL 3.x support. It will not run over SSH or in Docker without a display server.

### Tests

**Windows:**

```powershell
.\build\Release\tests.exe
# or via CTest
cd build && ctest -C Release --output-on-failure
```

**macOS / Linux:**

```bash
./build/tests
# or via CTest
cd build && ctest --output-on-failure
```

---

## Source Layout

```
src/
  core/          event_types.h, records.h
  rng/           irng.h, mt19937_rng.h/.cpp
  book/          i_order_book.h, multi_level_book.h/.cpp
  model/         i_intensity_model.h, simple_imbalance_intensity,
                 curve_intensity_model, hlr_params, intensity_curve
  calibration/   intensity_estimator, intensity_curve_io
  sampler/       i_event_sampler.h, i_attribute_sampler.h,
                 competing_intensity_sampler, unit_size_attribute_sampler
  io/            i_event_sink.h, in_memory_sink
  producer/      i_producer.h, qrsdp_producer
  main.cpp       Headless CLI entry point

tests/qrsdp/    9 test files, 51 test cases
tools/qrsdp_ui/  ImGui + ImPlot real-time debugging UI
```

## Test Categories

- **Records** — packed struct layout, flag constants
- **Interfaces** — pure-virtual compilation checks
- **Book** — seed, apply, shift, cascade, reinitialize
- **Intensity** — SimpleImbalance formula verification
- **CurveIntensity** — HLR2014 per-level curves
- **Calibration** — intensity estimation, curve JSON I/O
- **Sampler** — exponential delta-t, categorical type selection
- **AttributeSampler** — level selection for adds/cancels
- **Producer** — determinism, invariants, shift detection, reinit
