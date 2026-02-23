"""
Compute OHLC (Open-High-Low-Close) bars from tick-level mid-price data
at multiple time resolutions for interactive charting.
"""

from typing import Dict

import numpy as np
import pandas as pd

# Pre-defined resolution tiers (label -> nanoseconds)
RESOLUTIONS = {
    "1s": 1_000_000_000,
    "10s": 10_000_000_000,
    "1min": 60_000_000_000,
    "5min": 300_000_000_000,
}


def compute_ohlc(
    ts_ns: np.ndarray,
    mid_ticks: np.ndarray,
    interval_ns: int,
    event_counts: bool = True,
) -> pd.DataFrame:
    """
    Aggregate tick-level mid-price into OHLC bars.

    Parameters
    ----------
    ts_ns : array of uint64 timestamps in nanoseconds
    mid_ticks : array of float64 mid-prices
    interval_ns : bar width in nanoseconds
    event_counts : if True, include a 'volume' column with event counts per bar

    Returns
    -------
    DataFrame with columns: time_ns, time_s, open, high, low, close, volume
    """
    if len(ts_ns) == 0:
        return pd.DataFrame(columns=["time_ns", "time_s", "open", "high", "low", "close", "volume"])

    t0 = int(ts_ns[0])
    bins = ((ts_ns.astype(np.int64) - t0) // interval_ns).astype(np.int64)

    df = pd.DataFrame({"bin": bins, "mid": mid_ticks})
    grouped = df.groupby("bin")["mid"]
    ohlc = grouped.agg(["first", "max", "min", "last", "count"])
    ohlc.columns = ["open", "high", "low", "close", "volume"]

    ohlc["time_ns"] = t0 + ohlc.index.values * interval_ns
    ohlc["time_s"] = ohlc["time_ns"] / 1e9
    ohlc = ohlc.reset_index(drop=True)

    return ohlc[["time_ns", "time_s", "open", "high", "low", "close", "volume"]]


def multi_resolution_ohlc(
    ts_ns: np.ndarray,
    mid_ticks: np.ndarray,
) -> Dict[str, pd.DataFrame]:
    """Pre-compute OHLC bars at all standard resolutions."""
    return {
        label: compute_ohlc(ts_ns, mid_ticks, interval_ns)
        for label, interval_ns in RESOLUTIONS.items()
    }


def select_resolution(
    bars: Dict[str, pd.DataFrame],
    visible_seconds: float,
) -> tuple:
    """
    Pick the best resolution for a given visible time window.

    Returns (label, DataFrame) for the resolution that gives a reasonable
    number of bars in the visible range.
    """
    if visible_seconds < 300:       # < 5 min
        key = "1s"
    elif visible_seconds < 1800:    # < 30 min
        key = "10s"
    elif visible_seconds < 10800:   # < 3 hours
        key = "1min"
    else:
        key = "5min"

    return key, bars[key]
