# Chunk Size Tuning

Comparison of LZ4 chunk sizes for the `.qrsdp` event log format. All runs use the same single-day session (seed=42, 23400s, ~2.26M events) to isolate the effect of chunk size.

## Results

| Chunk Size | Records/Chunk | Chunks | File Size | Compression | Write ev/s | Read ev/s |
|:-----------|-------------:|---------:|----------:|------------:|-----------:|----------:|
| 1024       | 1,024        | 2,210   | 28.74 MB  | 1.95x       | 3.9M       | 45.0M     |
| **4096**   | **4,096**    | **553** | **27.35 MB** | **2.05x** | **3.9M**  | **36.4M** |
| 16384      | 16,384       | 139     | 27.30 MB  | 2.05x       | 3.9M       | 32.3M     |

Raw record size: 26 bytes. Uncompressed payload: 56.10 MB for all runs.

## Analysis

**Compression ratio** improves from 1.95x (1K) to 2.05x (4K and 16K). The jump from 1K to 4K is meaningful because LZ4 benefits from larger input blocks -- more redundancy to exploit. Beyond 4K the gains plateau, since the event data is already fairly random (timestamps, order IDs) and the compressible fields (type, side, price) are well-captured in 4K-record windows.

**Write throughput** is essentially constant across all chunk sizes (~3.9M events/sec). The producer loop dominates; LZ4 compression is fast enough not to be the bottleneck.

**Read throughput** is highest at 1K (45M ev/s) and decreases with larger chunks (36M at 4K, 32M at 16K). This is because `readAll()` decompresses each chunk into a separate vector and then copies into the result. Smaller chunks mean smaller individual decompressions with better cache locality.

**Random-access granularity** is inversely proportional to chunk size. Each chunk is the smallest unit for timestamp-range queries:

| Chunk Size | Time Span per Chunk | Index Entries |
|:-----------|:-------------------:|--------------:|
| 1024       | ~10.6 s             | 2,210         |
| 4096       | ~42.3 s             | 553           |
| 16384      | ~168.3 s            | 139           |

For a 6.5-hour session at ~97 events/sec, a 4096-record chunk spans about 42 seconds of trading time. This is fine-grained enough for most analysis queries (e.g. "give me events in the first 5 minutes") while keeping the index small.

## Decision

**Default: 4096 records per chunk.**

- Best balance of compression (2.05x) and index granularity (~42s per chunk).
- File size essentially identical to 16K (27.35 MB vs 27.30 MB -- 0.2% difference).
- Read throughput is 13% faster than 16K due to better cache behaviour.
- 1K chunks sacrifice 5% compression for minimal benefit; the extra 1,657 index entries add overhead without practical value.
- 4K is also the page size on most systems, aligning nicely with OS I/O.

The chunk size can be overridden with `--chunk-size` in `qrsdp_run` for specialised use cases.
