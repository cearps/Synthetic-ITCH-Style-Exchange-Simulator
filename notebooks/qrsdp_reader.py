"""
Read .qrsdp binary event log files produced by the QRSDP simulator.

Supports:
- File header parsing
- Lazy chunk-by-chunk iteration (constant memory)
- Full-day reads
- Manifest-based multi-day iteration with date filtering
"""

import json
import struct
from pathlib import Path
from typing import Dict, Generator, Optional, Tuple

import lz4.block
import numpy as np

# ---------------------------------------------------------------------------
# Binary format constants (must match src/io/event_log_format.h)
# ---------------------------------------------------------------------------

MAGIC = b"QRSDPLOG"
FILE_HEADER_SIZE = 64
CHUNK_HEADER_SIZE = 32
RECORD_SIZE = 26

RECORD_DTYPE = np.dtype([
    ("ts_ns", "<u8"),
    ("type", "u1"),
    ("side", "u1"),
    ("price_ticks", "<i4"),
    ("qty", "<u4"),
    ("order_id", "<u8"),
])
assert RECORD_DTYPE.itemsize == RECORD_SIZE

_HEADER_STRUCT = struct.Struct("<8s HH I Q i I I I I I I I Q")
assert _HEADER_STRUCT.size == FILE_HEADER_SIZE

_CHUNK_HEADER_STRUCT = struct.Struct("<I I I I Q Q")
assert _CHUNK_HEADER_STRUCT.size == CHUNK_HEADER_SIZE

EVENT_TYPES = {
    0: "ADD_BID",
    1: "ADD_ASK",
    2: "CANCEL_BID",
    3: "CANCEL_ASK",
    4: "EXECUTE_BUY",
    5: "EXECUTE_SELL",
}


# ---------------------------------------------------------------------------
# File header
# ---------------------------------------------------------------------------

def read_header(path: str | Path) -> Dict:
    """Parse the 64-byte file header and return a dict of session metadata."""
    with open(path, "rb") as f:
        raw = f.read(FILE_HEADER_SIZE)
    if len(raw) < FILE_HEADER_SIZE:
        raise ValueError(f"file too small for header: {path}")

    fields = _HEADER_STRUCT.unpack(raw)
    magic = fields[0]
    if magic != MAGIC:
        raise ValueError(f"bad magic: {magic!r} (expected {MAGIC!r})")

    return {
        "magic": magic,
        "version_major": fields[1],
        "version_minor": fields[2],
        "record_size": fields[3],
        "seed": fields[4],
        "p0_ticks": fields[5],
        "tick_size": fields[6],
        "session_seconds": fields[7],
        "levels_per_side": fields[8],
        "initial_spread_ticks": fields[9],
        "initial_depth": fields[10],
        "chunk_capacity": fields[11],
        "header_flags": fields[12],
        "reserved": fields[13],
    }


# ---------------------------------------------------------------------------
# Chunk iteration
# ---------------------------------------------------------------------------

def iter_chunks(path: str | Path) -> Generator[np.ndarray, None, None]:
    """Lazily yield one numpy structured array per LZ4-compressed chunk."""
    with open(path, "rb") as f:
        header_raw = f.read(FILE_HEADER_SIZE)
        if len(header_raw) < FILE_HEADER_SIZE:
            return
        magic = header_raw[:8]
        if magic != MAGIC:
            raise ValueError(f"bad magic: {magic!r}")

        while True:
            ch_raw = f.read(CHUNK_HEADER_SIZE)
            if len(ch_raw) < CHUNK_HEADER_SIZE:
                break

            (uncompressed_size, compressed_size, record_count,
             _flags, _first_ts, _last_ts) = _CHUNK_HEADER_STRUCT.unpack(ch_raw)

            if compressed_size == 0:
                break

            payload = f.read(compressed_size)
            if len(payload) < compressed_size:
                break

            decompressed = lz4.block.decompress(payload, uncompressed_size=uncompressed_size)
            records = np.frombuffer(decompressed, dtype=RECORD_DTYPE, count=record_count)
            yield records.copy()


def read_day(path: str | Path) -> np.ndarray:
    """Read all records from a single .qrsdp file into one numpy array."""
    chunks = list(iter_chunks(path))
    if not chunks:
        return np.empty(0, dtype=RECORD_DTYPE)
    return np.concatenate(chunks)


# ---------------------------------------------------------------------------
# Manifest and multi-day iteration
# ---------------------------------------------------------------------------

def load_manifest(run_dir: str | Path) -> Dict:
    """Load manifest.json from a run directory."""
    manifest_path = Path(run_dir) / "manifest.json"
    with open(manifest_path) as f:
        return json.load(f)


def iter_days(
    run_dir: str | Path,
    start_date: Optional[str] = None,
    end_date: Optional[str] = None,
) -> Generator[Tuple[str, np.ndarray], None, None]:
    """
    Iterate (date, records) over sessions in a run directory.

    Filters to sessions whose date is >= start_date and <= end_date
    (both inclusive, YYYY-MM-DD strings). If None, no filtering on that end.
    """
    run_dir = Path(run_dir)
    manifest = load_manifest(run_dir)

    for session in manifest["sessions"]:
        date = session["date"]
        if start_date and date < start_date:
            continue
        if end_date and date > end_date:
            continue
        records = read_day(run_dir / session["file"])
        yield date, records
