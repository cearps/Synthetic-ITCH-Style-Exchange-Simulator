# Dev Week Plan — Event Log, Replay, and Visualisation

**Duration:** 5 days (Monday–Friday)
**Objective:** Take the working QRSDP producer from in-memory output to persistent, disk-backed event logging with chunked compression, build replay infrastructure, and produce Jupyter notebooks for visualisation and analysis.

---

## What Already Exists

The following is complete, tested, and committed:

| Component | Status |
|---|---|
| QRSDP event producer (continuous-time competing-risk loop) | Done |
| Two intensity models (SimpleImbalance, HLR2014 CurveIntensity) | Done |
| Multi-level order book with shift mechanics and spread preservation | Done |
| Deterministic RNG (mt19937_64), seeded sessions | Done |
| `IEventSink` interface with `InMemorySink` implementation | Done |
| Packed `EventRecord` (31 bytes, shift/reinit flags) | Done |
| Calibration stubs (IntensityEstimator, curve JSON I/O) | Done |
| ImGui/ImPlot real-time debugging UI | Done |
| Google Test suite (51 tests across 9 files) | Done |
| Headless CLI (`qrsdp_cli`) | Done |
| Cross-platform build (Windows/MSVC, macOS, Linux, Docker) | Done |

**Key architectural fact:** the producer already writes to `IEventSink`. Everything this week plugs into that interface — the producer code does not need to change.

---

## What Needs Building

From the proposal, scoped to what is achievable and useful:

1. **Disk-backed event sink** — binary, append-only, chunked, LZ4-compressed
2. **Event log reader** — sequential and random-access block reads for replay
3. **Long-duration session runner** — multi-session daily loop driving the producer for realistic periods
4. **Python replay library** — read the binary log from Python/Jupyter
5. **Jupyter notebooks** — price visualisation, stylised facts, event distributions
6. **Documentation** — architecture decisions, storage format spec, performance measurements

**Out of scope this week:** ITCH encoder, UDP streaming, matching engine (the QRSDP producer subsumes the matching engine by applying events directly to the book), Hawkes/agent-based models, external trading agents.

---

## Daily Plan

### Day 1 (Monday) — Binary Event Log: Write Path

**Goal:** Replace `InMemorySink` with a disk-backed sink that writes `EventRecord` blocks to a binary file with chunk-level LZ4 compression.

**Tasks:**

1. **Define the log file format** (new doc: `docs/event-log-format.md`)
   - File header: magic bytes, version, record size, session metadata (seed, p0, tick_size, session_seconds)
   - Chunk layout: chunk header (uncompressed size, compressed size, record count, first/last timestamp) followed by LZ4-compressed payload of N `EventRecord`s
   - Footer / index: optional offset table for random access
   - Design for append-only, forward-sequential writes

2. **Add LZ4 dependency**
   - FetchContent or vendored `lz4.h` / `lz4.c` (BSD licence, single-file)
   - Wire into CMakeLists.txt

	1. **Implement `BinaryFileSink : IEventSink`** (`src/io/binary_file_sink.h/.cpp`)
   - Accumulates records into a chunk buffer (e.g. 4096 records per chunk)
   - On flush: LZ4-compress the buffer, write chunk header + compressed bytes
   - Flush on chunk full, session end, or explicit call
   - Write file header on construction, finalise on close

4. **Tests**
   - Write N records to a temp file via `BinaryFileSink`, reopen, verify byte-level header
   - Round-trip: write → read back raw bytes → decompress → compare records

**End of day:** `qrsdp_cli` can write a 30-second session to a `.qrsdp` binary file on disk.

---

### Day 2 (Tuesday) — Event Log Reader and Replay

**Goal:** Build the read side of the event log — sequential block reads and random-access by chunk index or timestamp range.

**Tasks:**

1. **Implement `EventLogReader`** (`src/io/event_log_reader.h/.cpp`)
   - Open file, parse header, build in-memory chunk index (offsets from header or by scanning chunk headers)
   - `readChunk(idx)` → decompress → return `std::vector<EventRecord>`
   - `readRange(ts_start, ts_end)` → iterate chunks overlapping the time range
   - Iterator-style API: `begin()` / `next()` for streaming through the whole log without loading everything

2. **Implement `EventLogWriter` wrapper** (if needed beyond `BinaryFileSink`)
   - Encapsulate session metadata writing
   - Support appending multiple sessions to one file (session boundary markers)

3. **Update `qrsdp_cli`**
   - Accept output path argument: `qrsdp_cli [seed] [seconds] [output.qrsdp]`
   - When output path is given, use `BinaryFileSink`; otherwise print summary as before

4. **Tests**
   - Write a session, read it back chunk-by-chunk, verify all records match `InMemorySink` output
   - Timestamp ordering: assert `ts_ns` is monotonically increasing across chunks
   - Random access: read chunk N directly, verify first record's `ts_ns`

**End of day:** Full write-read round-trip works. CLI can produce log files and a reader can consume them.

---

### Day 3 (Wednesday) — Long-Duration Sessions and Performance

**Goal:** Run the producer for realistic durations (simulated trading days, weeks) and measure throughput, file size, and compression ratio.

**Tasks:**

1. **Session runner / daily loop** (`src/producer/session_runner.h/.cpp`)
   - Drive the producer for multiple consecutive sessions (e.g. 23,400 seconds = 6.5 hours per trading day)
   - Each session: configure, run, flush to sink
   - Parameterise: number of days, session length, seed strategy (sequential or hashed)

2. **Performance benchmarking**
   - Time a single 6.5-hour session: measure events/sec throughput
   - Measure file size and compression ratio (raw vs LZ4)
   - Measure read-back throughput (records/sec for sequential scan)
   - Record results in `docs/performance-results.md`

3. **Tune chunk size**
   - Experiment with chunk sizes (1K, 4K, 16K records) for compression ratio vs random-access granularity
   - Pick a default, document reasoning

4. **Stress test**
   - Generate 1 day of data, verify log integrity (header, chunk count, record count)
   - If throughput allows, try 5 days or more

**End of day:** Can generate and persist a full simulated trading day (likely millions of events). Have concrete throughput and compression numbers.

---

### Day 4 (Thursday) — Python Reader and Jupyter Notebooks

**Goal:** Read event logs from Python and build analysis notebooks.

**Tasks:**

1. **Python log reader** (`notebooks/qrsdp_reader.py` or similar)
   - Read the binary file format defined on Day 1
   - Parse file header → session metadata
   - Decompress chunks → numpy structured array of `EventRecord`
   - Lazy chunk iteration for files that exceed memory
   - Dependency: `lz4` (pip package), `numpy`

2. **Notebook 1: Price and Book Visualisation** (`notebooks/01_price_visualisation.ipynb`)
   - Load a session log
   - Reconstruct mid-price time series from `EventRecord` (use shift flags, or track best bid/ask from add/cancel/execute events)
   - Plot: price over time, bid-ask spread over time
   - Plot: book depth heatmap (if feasible from event replay)

3. **Notebook 2: Event Distributions and Stylised Facts** (`notebooks/02_stylised_facts.ipynb`)
   - Event type distribution (pie/bar chart)
   - Inter-arrival time distribution (fit to exponential, overlay theoretical)
   - Return distribution at various time scales (tick-by-tick, 1s, 10s) — check for fat tails
   - Autocorrelation of returns (should be near zero) and of absolute returns (should be positive — volatility clustering proxy)
   - Shift frequency over time

4. **Notebook 3: Session Summary** (`notebooks/03_session_summary.ipynb`)
   - High-level stats: total events, duration, shifts, open/close price, max spread
   - Compression stats: raw size vs file size
   - Useful as a quick sanity check after any run

**End of day:** Can generate a log, open it in Jupyter, and produce publication-quality plots of price dynamics and event statistics.

---

### Day 5 (Friday) — Polish, Documentation, and Stretch Goals

**Goal:** Clean up, document everything, and tackle stretch goals if time allows.

**Tasks:**

1. **Documentation**
   - `docs/event-log-format.md` — finalise the binary format spec
   - `docs/performance-results.md` — throughput, compression, file sizes
   - Update `docs/build-test-run.md` — add instructions for log generation, Python setup, notebook usage
   - Update `README.md` — reflect new capabilities
   - Update `docs/README.md` — add new doc links

2. **Docker updates**
   - Update `Dockerfile.build` to support log output (volume mount for `data/`)
   - Optionally: a `Dockerfile.notebooks` with Python + Jupyter + lz4 for the analysis side

3. **Stretch goals** (in priority order, tackle what time allows)
   - **Log viewer CLI**: `qrsdp_log_info <file.qrsdp>` — print header, chunk count, record count, time range, compression ratio
   - **Book replay from log**: replay events through `MultiLevelBook` to reconstruct full book state at any point (useful for the Python reader and depth heatmaps)
   - **Multiple securities**: parameterise session runner for N independent securities writing to separate log files
   - **Session index file**: write a separate `.qrsdp.idx` file mapping chunk offsets for O(1) random access by timestamp

4. **Final commit and tag**
   - Clean up any temporary files
   - Run full test suite
   - Tag release: `v0.3.0-devweek`

---

## Technical Decisions to Make on Day 1

These should be resolved before writing code:

| Decision | Options | Recommendation |
|---|---|---|
| LZ4 variant | `LZ4_compress_default` (fast) vs `LZ4_compress_HC` (better ratio) | Default for writes, HC as option |
| Chunk size | 1K–64K records | Start with 4096, tune on Day 3 |
| File extension | `.qrsdp`, `.evtlog`, `.bin` | `.qrsdp` |
| Multi-session format | One file per session vs sessions concatenated | One file per session initially; simpler reader |
| Python reader | Pure Python vs pybind11 C++ extension | Pure Python + lz4 pip package; fast enough for analysis |
| Notebook format | Classic Jupyter vs JupyterLab | JupyterLab (modern, better plotting) |

---

## Dependencies to Add

| Library | Purpose | Integration |
|---|---|---|
| LZ4 (C) | Block compression for event log chunks | CMake FetchContent or vendored `lz4.c`/`lz4.h` |
| lz4 (Python) | Decompress chunks in notebooks | `pip install lz4` |
| numpy | Structured array for EventRecord | `pip install numpy` |
| matplotlib | Plotting in notebooks | `pip install matplotlib` |
| pandas | Optional, for time-series analysis | `pip install pandas` |

---

## Success Criteria (End of Week)

- [x] `qrsdp_cli 42 23400 output.qrsdp` produces a compressed binary event log for a full trading day
- [x] Event log can be read back in C++ with verified record integrity
- [x] Event log can be read in Python and loaded into numpy arrays
- [x] At least 2 Jupyter notebooks produce meaningful visualisations (price, event distributions)
- [x] Throughput and compression measurements are documented
- [x] All existing tests still pass; new tests cover the log write/read round-trip
- [x] Documentation is updated to reflect the new architecture

---

## Risk Mitigation

| Risk | Mitigation |
|---|---|
| LZ4 integration issues on Windows/MSVC | LZ4 is well-tested on MSVC; vendoring the single .c file avoids build system issues |
| Throughput too low for billion-event targets | The proposal notes targets may be adjusted; a full day (~millions of events) is realistic and sufficient for visualisation |
| Python reader complexity for large files | Lazy chunk iteration avoids loading entire logs into memory |
| Running out of time on notebooks | Day 4 notebooks are ordered by priority; Notebook 1 (price plot) alone is a useful deliverable |

---

## File Map (Expected New Files)

```
src/io/
  binary_file_sink.h / .cpp       Day 1
  event_log_reader.h / .cpp       Day 2
  event_log_format.h              Day 1 (header structs, magic bytes)

src/producer/
  session_runner.h / .cpp          Day 3

notebooks/
  qrsdp_reader.py                  Day 4
  01_price_visualisation.ipynb     Day 4
  02_stylised_facts.ipynb          Day 4
  03_session_summary.ipynb         Day 4
  requirements.txt                 Day 4

docs/
  event-log-format.md              Day 1, finalised Day 5
  performance-results.md           Day 3, finalised Day 5

data/                              Output directory for generated logs (gitignored)
```
