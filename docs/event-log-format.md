# Event Log File Format (`.qrsdp`)

The purpose of this document is to specify the binary format for the append-only event logs produced by exchange session producers. The format is **producer-agnostic** — any model that emits limit order book events (adds, cancels, executions) can write to this format. These files are the persistent source of truth for all simulated exchange activity and are designed for:

- High-throughput sequential writes during simulation
- Efficient sequential reads for replay and analysis
- Optional random-access reads via a chunk index footer
- Cross-language consumption (C++ writer, Python reader)

All multi-byte integers are **little-endian**. The format is **not self-describing** — readers must know the version to interpret records correctly.

---

## Overview

```
┌──────────────────────────┐
│      File Header         │  64 bytes
├──────────────────────────┤
│      Chunk 0             │  chunk header + LZ4 payload
├──────────────────────────┤
│      Chunk 1             │
├──────────────────────────┤
│         ...              │
├──────────────────────────┤
│      Chunk N-1           │
├──────────────────────────┤
│  Chunk Index (optional)  │  32 bytes per chunk + 16 byte tail
└──────────────────────────┘
```

A file contains exactly **one session**. Multi-session datasets use one file per session within a run directory (see [Section 10: Multi-Session Datasets](#10-multi-session-datasets)).

---

## 1. File Header (64 bytes)

| Offset | Size | Type      | Field                  | Description                                                           |
| -----: | ---: | :-------- | :--------------------- | :-------------------------------------------------------------------- |
|      0 |    8 | `char[8]` | `magic`                | `"QRSDPLOG"` (ASCII, no null terminator)                              |
|      8 |    2 | `uint16`  | `version_major`        | Format major version (currently `1`)                                  |
|     10 |    2 | `uint16`  | `version_minor`        | Format minor version (currently `0`)                                  |
|     12 |    4 | `uint32`  | `record_size`          | `sizeof(EventRecord)` on the writing platform (currently `26`)        |
|     16 |    8 | `uint64`  | `seed`                 | RNG seed used for this session                                        |
|     24 |    4 | `int32`   | `p0_ticks`             | Opening mid-price in ticks                                            |
|     28 |    4 | `uint32`  | `tick_size`            | Tick size in price units (e.g. `100` = $0.01 if base unit is $0.0001) |
|     32 |    4 | `uint32`  | `session_seconds`      | Simulated session duration in seconds                                 |
|     36 |    4 | `uint32`  | `levels_per_side`      | Number of book levels per side at initialisation                      |
|     40 |    4 | `uint32`  | `initial_spread_ticks` | Spread at t=0 in ticks                                                |
|     44 |    4 | `uint32`  | `initial_depth`        | Queue depth per level at t=0                                          |
|     48 |    4 | `uint32`  | `chunk_capacity`       | Max records per chunk (default `4096`)                                |
|     52 |    4 | `uint32`  | `header_flags`         | Bit field (see below)                                                 |
|     56 |    8 | `uint64`  | `reserved`             | Must be `0`; reserved for future use                                  |

**Total: 64 bytes.**

### Header Flags

| Bit | Name             | Meaning |
|----:|:-----------------|:--------|
|   0 | `HAS_INDEX`      | A chunk index footer is present at the end of the file |
| 1–31| —                | Reserved, must be `0` |

### Validation

A reader **must** verify:
1. `magic == "QRSDPLOG"`
2. `version_major` is a supported version
3. `record_size > 0`

If `record_size` differs from the reader's compiled `sizeof(EventRecord)`, the file was produced by a different version and the reader should reject it or apply a migration.

---

## 2. EventRecord Layout (26 bytes, packed)

Each event in the log corresponds to one `EventRecord`, serialised with `#pragma pack(1)` (no padding).

| Offset | Size | Type     | Field         | Description |
|-------:|-----:|:---------|:--------------|:------------|
|      0 |    8 | `uint64` | `ts_ns`       | Nanosecond timestamp from session start (t=0 at session open) |
|      8 |    1 | `uint8`  | `type`        | Event type (see table below) |
|      9 |    1 | `uint8`  | `side`        | Side (see table below) |
|     10 |    4 | `int32`  | `price_ticks` | Price level in ticks |
|     14 |    4 | `uint32` | `qty`         | Order quantity |
|     18 |    8 | `uint64` | `order_id`    | Monotonically increasing order identifier |

**Total: 26 bytes.**

The record contains only the universal fields common to any limit order book event. Producer-specific annotations (e.g. book shifts, reinitialisations) are derivable by replaying the event stream through a book and do not belong in the interchange format.

### Event Types

| Value | Name           | Description |
|------:|:---------------|:------------|
|     0 | `ADD_BID`      | Limit order added on the bid side |
|     1 | `ADD_ASK`      | Limit order added on the ask side |
|     2 | `CANCEL_BID`   | Order cancelled from the bid side |
|     3 | `CANCEL_ASK`   | Order cancelled from the ask side |
|     4 | `EXECUTE_BUY`  | Marketable buy consuming best ask |
|     5 | `EXECUTE_SELL` | Marketable sell consuming best bid |

### Side Values

| Value | Name  |
|------:|:------|
|     0 | `BID` |
|     1 | `ASK` |
|     2 | `NA`  |

---

## 3. Chunk Layout

Records are grouped into chunks for compression. Each chunk is independently decompressible, enabling random access without reading the entire file.

### Chunk Header (32 bytes)

| Offset | Size | Type     | Field               | Description |
|-------:|-----:|:---------|:--------------------|:------------|
|      0 |    4 | `uint32` | `uncompressed_size` | Size of the raw payload in bytes (`record_count * record_size`) |
|      4 |    4 | `uint32` | `compressed_size`   | Size of the LZ4 compressed payload in bytes |
|      8 |    4 | `uint32` | `record_count`      | Number of `EventRecord`s in this chunk |
|     12 |    4 | `uint32` | `chunk_flags`       | Reserved, must be `0` |
|     16 |    8 | `uint64` | `first_ts_ns`       | Timestamp of the first record in the chunk |
|     24 |    8 | `uint64` | `last_ts_ns`        | Timestamp of the last record in the chunk |

**Total: 32 bytes.**

### Chunk Payload

Immediately following the chunk header are `compressed_size` bytes of LZ4-compressed data. When decompressed, the payload yields exactly `uncompressed_size` bytes, which is a contiguous array of `record_count` packed `EventRecord` structs.

### Compression

- **Algorithm:** LZ4 block compression (`LZ4_compress_default`)
- **Decompression:** `LZ4_decompress_safe` with `uncompressed_size` as the known output bound
- The last chunk in a file may contain fewer than `chunk_capacity` records (a partial chunk is valid)

### Invariants

- `uncompressed_size == record_count * record_size`
- `record_count <= chunk_capacity` (from file header)
- `first_ts_ns <= last_ts_ns`
- Timestamps within a chunk are monotonically non-decreasing
- Chunks appear in the file in timestamp order: `chunk[i].last_ts_ns <= chunk[i+1].first_ts_ns`

---

## 4. Chunk Index Footer (Optional)

When `header_flags & HAS_INDEX` is set, the file ends with a chunk index that maps each chunk to its file offset and timestamp range. This enables O(1) random access by chunk number and efficient binary search by timestamp.

### Index Entry (32 bytes, repeated per chunk)

| Offset | Size | Type     | Field          | Description |
|-------:|-----:|:---------|:---------------|:------------|
|      0 |    8 | `uint64` | `file_offset`  | Byte offset from start of file to this chunk's header |
|      8 |    8 | `uint64` | `first_ts_ns`  | First record timestamp in the chunk |
|     16 |    8 | `uint64` | `last_ts_ns`   | Last record timestamp in the chunk |
|     24 |    4 | `uint32` | `record_count` | Number of records in the chunk |
|     28 |    4 | `uint32` | `reserved`     | Must be `0` |

### Index Tail (16 bytes)

Written once, after all index entries:

| Offset | Size | Type       | Field                | Description |
|-------:|-----:|:-----------|:---------------------|:------------|
|      0 |    4 | `uint32`   | `chunk_count`        | Total number of chunks (and index entries) |
|      4 |    4 | `char[4]`  | `index_magic`        | `"QIDX"` (ASCII) |
|      8 |    8 | `uint64`   | `index_start_offset` | Byte offset from start of file to the first index entry |

### Reading the Index

1. Seek to `EOF - 16`
2. Read the 16-byte index tail
3. Verify `index_magic == "QIDX"`
4. Seek to `index_start_offset`
5. Read `chunk_count` index entries (each 32 bytes)

### Timestamp Lookup

To find the chunk containing a target timestamp `T`:

1. Binary search the index entries on `first_ts_ns` / `last_ts_ns`
2. Seek to the matching chunk's `file_offset`
3. Read and decompress that single chunk

---

## 5. Write Path

The writer (`BinaryFileSink`) follows this sequence:

1. **Open file**, write the 64-byte file header (with `header_flags = 0` initially)
2. **Accumulate records** into an in-memory buffer of capacity `chunk_capacity`
3. **When the buffer is full** (or the session ends):
   a. Write the 32-byte chunk header
   b. LZ4-compress the buffer
   c. Write the compressed payload
   d. Record the chunk's file offset, timestamps, and count for the index
4. **On session end**, flush any partial chunk
5. **Write the chunk index** footer (all index entries + tail)
6. **Seek back** to the file header and set `HAS_INDEX` in `header_flags`
7. **Close the file**

If the writer crashes before step 5, the file is still valid for sequential reading — the reader simply scans chunk headers from offset 64 until EOF. The index is a performance optimisation, not a correctness requirement.

---

## 6. Read Path

### Sequential Scan (No Index)

1. Read and validate the 64-byte file header
2. Set position to offset 64
3. Loop:
   a. Read 32-byte chunk header
   b. Read `compressed_size` bytes of payload
   c. Decompress with `LZ4_decompress_safe`
   d. Interpret the result as `record_count` contiguous `EventRecord` structs
   e. If EOF, stop

### Indexed Random Access

1. Read the file header
2. Read the chunk index footer (see Section 4)
3. To read chunk `k`: seek to `index[k].file_offset`, read chunk header + payload, decompress

### Python Reader

The Python reader uses `struct` for header parsing and the `lz4.block` module for decompression:

```python
import struct
import lz4.block
import numpy as np

RECORD_DTYPE = np.dtype([
    ('ts_ns',       '<u8'),
    ('type',        'u1'),
    ('side',        'u1'),
    ('price_ticks', '<i4'),
    ('qty',         '<u4'),
    ('order_id',    '<u8'),
])

FILE_HEADER_FMT = '<8sHHI QiIII II Q'
FILE_HEADER_SIZE = 64
CHUNK_HEADER_FMT = '<IIII QQ'
CHUNK_HEADER_SIZE = 32

def read_file_header(f):
    raw = f.read(FILE_HEADER_SIZE)
    fields = struct.unpack(FILE_HEADER_FMT, raw)
    magic = fields[0]
    assert magic == b'QRSDPLOG', f"Bad magic: {magic}"
    return {
        'version_major': fields[1],
        'version_minor': fields[2],
        'record_size':   fields[3],
        'seed':          fields[4],
        'p0_ticks':      fields[5],
        'tick_size':     fields[6],
        'session_seconds': fields[7],
        'levels_per_side': fields[8],
        'initial_spread_ticks': fields[9],
        'initial_depth': fields[10],
        'chunk_capacity': fields[11],
        'header_flags':  fields[12],
    }

def iter_chunks(f, header):
    """Yield numpy arrays of EventRecord, one per chunk."""
    while True:
        raw = f.read(CHUNK_HEADER_SIZE)
        if len(raw) < CHUNK_HEADER_SIZE:
            break
        unc_sz, comp_sz, rec_count, _, _, _ = struct.unpack(CHUNK_HEADER_FMT, raw)
        compressed = f.read(comp_sz)
        decompressed = lz4.block.decompress(compressed, uncompressed_size=unc_sz)
        yield np.frombuffer(decompressed, dtype=RECORD_DTYPE, count=rec_count)
```

---

## 7. File Size Estimation

For a session producing `N` events with a record size of 26 bytes:

| Metric | Formula |
|:-------|:--------|
| Raw payload | `N × 26` bytes |
| Number of chunks | `⌈N / chunk_capacity⌉` |
| Chunk header overhead | `⌈N / chunk_capacity⌉ × 32` bytes |
| File header | `64` bytes |
| Index footer | `⌈N / chunk_capacity⌉ × 32 + 16` bytes |

LZ4 compression ratio on `EventRecord` data is typically **1.5–3×** depending on price/timestamp locality. A full 6.5-hour trading day at ~1,000 events/sec produces ~23.4M events = ~608 MB raw, compressing to roughly **200–400 MB**.

---

## 8. Versioning Policy

- **Minor version bump** (e.g. 1.0 → 1.1): new fields appended to reserved space, new flag bits, new optional sections. Old readers can still parse the file by ignoring unknown fields.
- **Major version bump** (e.g. 1.x → 2.0): breaking changes to record layout, header structure, or compression. Old readers must reject the file.

The `record_size` field in the header serves as an additional forward-compatibility check — if a future version adds fields to `EventRecord`, the size will change and old readers can detect the mismatch.

---

## 9. Design Rationale

| Decision | Choice | Reasoning |
|:---------|:-------|:----------|
| Compression algorithm | LZ4 block | Extremely fast decompression (~4 GB/s), adequate ratio for sequential data. HC variant available for archival. |
| Chunk size | 4096 records (≈104 KB raw) | Balances compression ratio (larger = better) against random-access granularity (smaller = finer). Tunable via header field. |
| One file per session | Yes | Simplifies the reader and avoids cross-session chunk boundaries. Multi-session datasets use a directory of files. |
| Optional index | Yes | Sequential-scan readers don't need it; random-access readers benefit. Crash safety: file is valid without the index. |
| Little-endian | Yes | Native byte order on x86-64 and ARM64 (all target platforms). Avoids byte-swap overhead on write and read. |
| Fixed-width records | Yes | Enables `numpy.frombuffer` with a structured dtype, O(1) record indexing within a decompressed chunk, and trivial C `memcpy` serialisation. |
| No per-record flags | Removed | Shift/reinit flags were QRSDP-specific and derivable by replaying events through a book. Keeping the record producer-agnostic allows any LOB event source to use this format. |

---

## 10. Multi-Session Datasets

A long-duration run (weeks, months, a year) is stored as a directory of per-session files plus a manifest that ties them together.

### Directory Layout

```
data/run_42/
  manifest.json
  2026-01-02.qrsdp
  2026-01-05.qrsdp
  2026-01-06.qrsdp
  ...
  2026-12-31.qrsdp
```

Files are named by **synthetic trading date** (`YYYY-MM-DD.qrsdp`). Weekends and holidays are skipped, mirroring a real equity calendar. This convention:

- Sorts lexicographically into chronological order
- Enables date-range queries with filesystem globs (e.g. `2026-03-*.qrsdp` for March)
- Matches the convention used by market data vendors (LOBSTER, NASDAQ historical)

### Manifest (`manifest.json`)

The manifest is the authoritative index for the run. It is written by the session runner and consumed by analysis tools.

```json
{
  "format_version": "1.0",
  "run_id": "run_42",
  "producer": "qrsdp",
  "base_seed": 42,
  "seed_strategy": "sequential",
  "tick_size": 100,
  "p0_ticks": 100000,
  "session_seconds": 23400,
  "levels_per_side": 10,
  "initial_spread_ticks": 2,
  "initial_depth": 50,
  "sessions": [
    { "date": "2026-01-02", "seed": 42, "file": "2026-01-02.qrsdp" },
    { "date": "2026-01-05", "seed": 43, "file": "2026-01-05.qrsdp" },
    { "date": "2026-01-06", "seed": 44, "file": "2026-01-06.qrsdp" }
  ]
}
```

| Field              | Description |
|:-------------------|:------------|
| `format_version`   | Manifest schema version |
| `run_id`           | Human-readable identifier for the run |
| `producer`         | Name of the producer that generated the data (e.g. `"qrsdp"`, `"hawkes"`, `"agent"`) |
| `base_seed`        | Starting seed; individual session seeds are derived from this |
| `seed_strategy`    | How session seeds are derived: `"sequential"` (base + day index) or `"hashed"` (hash of base + date) |
| `tick_size`        | Shared across all sessions in the run |
| `p0_ticks`         | Opening price for the first session; subsequent sessions may use the prior day's close |
| `session_seconds`  | Duration of each session (e.g. `23400` = 6.5-hour trading day) |
| `sessions`         | Ordered array of per-session entries |
| `sessions[].date`  | Synthetic trading date |
| `sessions[].seed`  | Exact seed used (for reproducibility) |
| `sessions[].file`  | Filename relative to the run directory |

### Session Continuity

The session runner controls how sessions chain together:

- **Independent sessions**: each day starts from the same `p0_ticks` and `initial_depth`. Useful for Monte Carlo analysis of independent trading days.
- **Continuous sessions**: each day's opening price is the previous day's closing price (from `SessionResult.close_ticks`). Produces a multi-day price trajectory with overnight gaps.

The `p0_ticks` in each file's header reflects the actual opening price for that session, regardless of strategy. The manifest's top-level `p0_ticks` records the initial value only.

### Reading a Date Range (Python)

```python
import json
from pathlib import Path

def load_date_range(run_dir, start_date, end_date):
    run_dir = Path(run_dir)
    with open(run_dir / "manifest.json") as f:
        manifest = json.load(f)

    for session in manifest["sessions"]:
        if start_date <= session["date"] <= end_date:
            path = run_dir / session["file"]
            with open(path, "rb") as f:
                header = read_file_header(f)
                for chunk in iter_chunks(f, header):
                    yield session["date"], chunk
```
