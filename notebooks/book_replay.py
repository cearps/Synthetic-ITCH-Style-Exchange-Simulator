"""
Replay limit order book events to reconstruct mid-price, spread,
and best bid/ask time series.

Mirrors the C++ MultiLevelBook structure: a fixed-size array of
price levels per side, with shift mechanics when best level empties.
"""

from typing import Dict

import numpy as np

ADD_BID, ADD_ASK = 0, 1
CANCEL_BID, CANCEL_ASK = 2, 3
EXEC_BUY, EXEC_SELL = 4, 5


class _Level:
    __slots__ = ("price", "depth")

    def __init__(self, price: int, depth: int):
        self.price = price
        self.depth = depth


class _MiniBook:
    """
    Minimal order book matching C++ MultiLevelBook shift semantics.

    - Fixed number of levels per side
    - Shift when best level empties (cascade up to 64 times)
    - Opposite side prices shift by 1 tick on each shift step
    """

    def __init__(
        self,
        p0_ticks: int,
        levels_per_side: int,
        initial_spread_ticks: int,
        initial_depth: int,
    ):
        self.num_levels = levels_per_side
        self.initial_depth = initial_depth

        half = initial_spread_ticks // 2
        best_bid = p0_ticks - half
        best_ask = p0_ticks + initial_spread_ticks - half

        self.bids = [
            _Level(best_bid - k, initial_depth) for k in range(levels_per_side)
        ]
        self.asks = [
            _Level(best_ask + k, initial_depth) for k in range(levels_per_side)
        ]

    def _bid_index(self, price: int) -> int:
        if not self.bids:
            return -1
        idx = self.bids[0].price - price
        if 0 <= idx < self.num_levels:
            return idx
        return -1

    def _ask_index(self, price: int) -> int:
        if not self.asks:
            return -1
        idx = price - self.asks[0].price
        if 0 <= idx < self.num_levels:
            return idx
        return -1

    def _shift_bid(self):
        for _ in range(64):
            if self.num_levels <= 1:
                self.bids[0].price -= 1
                self.bids[0].depth = self.initial_depth
            else:
                for i in range(self.num_levels - 1):
                    self.bids[i].price = self.bids[i + 1].price
                    self.bids[i].depth = self.bids[i + 1].depth
                self.bids[-1].price = self.bids[-2].price - 1
                self.bids[-1].depth = self.initial_depth
            if self.bids[0].depth > 0:
                break

    def _shift_ask(self):
        for _ in range(64):
            if self.num_levels <= 1:
                self.asks[0].price += 1
                self.asks[0].depth = self.initial_depth
            else:
                for i in range(self.num_levels - 1):
                    self.asks[i].price = self.asks[i + 1].price
                    self.asks[i].depth = self.asks[i + 1].depth
                self.asks[-1].price = self.asks[-2].price + 1
                self.asks[-1].depth = self.initial_depth
            if self.asks[0].depth > 0:
                break

    def _improve_bid(self, price, qty):
        for i in range(self.num_levels - 1, 0, -1):
            self.bids[i].price = self.bids[i - 1].price
            self.bids[i].depth = self.bids[i - 1].depth
        self.bids[0].price = price
        self.bids[0].depth = qty

    def _improve_ask(self, price, qty):
        for i in range(self.num_levels - 1, 0, -1):
            self.asks[i].price = self.asks[i - 1].price
            self.asks[i].depth = self.asks[i - 1].depth
        self.asks[0].price = price
        self.asks[0].depth = qty

    def apply(self, event_type: int, price: int, qty: int):
        if event_type == ADD_BID:
            best_bid = self.bids[0].price if self.bids else 0
            best_ask = self.asks[0].price if self.asks else 0
            if price > best_bid and price < best_ask:
                self._improve_bid(price, qty)
            else:
                idx = self._bid_index(price)
                if 0 <= idx < self.num_levels:
                    self.bids[idx].depth += qty

        elif event_type == ADD_ASK:
            best_bid = self.bids[0].price if self.bids else 0
            best_ask = self.asks[0].price if self.asks else 0
            if price < best_ask and price > best_bid:
                self._improve_ask(price, qty)
            else:
                idx = self._ask_index(price)
                if 0 <= idx < self.num_levels:
                    self.asks[idx].depth += qty

        elif event_type == CANCEL_BID:
            idx = self._bid_index(price)
            if 0 <= idx < self.num_levels:
                was_nonzero = self.bids[idx].depth > 0
                self.bids[idx].depth = max(0, self.bids[idx].depth - qty)
                if idx == 0 and self.bids[0].depth == 0 and was_nonzero:
                    self._shift_bid()

        elif event_type == CANCEL_ASK:
            idx = self._ask_index(price)
            if 0 <= idx < self.num_levels:
                was_nonzero = self.asks[idx].depth > 0
                self.asks[idx].depth = max(0, self.asks[idx].depth - qty)
                if idx == 0 and self.asks[0].depth == 0 and was_nonzero:
                    self._shift_ask()

        elif event_type == EXEC_BUY:
            if self.asks and self.asks[0].depth > 0:
                self.asks[0].depth -= 1
                if self.asks[0].depth == 0:
                    self._shift_ask()

        elif event_type == EXEC_SELL:
            if self.bids and self.bids[0].depth > 0:
                self.bids[0].depth -= 1
                if self.bids[0].depth == 0:
                    self._shift_bid()

    @property
    def best_bid(self) -> int:
        return self.bids[0].price if self.bids else 0

    @property
    def best_ask(self) -> int:
        return self.asks[0].price if self.asks else 0


def replay_book(
    events: np.ndarray,
    p0_ticks: int = 10000,
    levels_per_side: int = 5,
    initial_spread_ticks: int = 2,
    initial_depth: int = 5,
) -> Dict[str, np.ndarray]:
    """
    Replay events through a minimal book and return price time series.

    Parameters
    ----------
    events : np.ndarray
        Structured array with RECORD_DTYPE fields (ts_ns, type, side,
        price_ticks, qty, order_id).
    p0_ticks, levels_per_side, initial_spread_ticks, initial_depth :
        Book initialisation parameters from the file header.

    Returns
    -------
    dict with keys:
        ts_ns       : uint64 array of timestamps
        mid_ticks   : float64 array of mid-prices (best_bid + best_ask) / 2
        best_bid    : int32 array of best bid prices
        best_ask    : int32 array of best ask prices
        spread_ticks: int32 array of spreads (best_ask - best_bid)
    """
    n = len(events)
    if n == 0:
        empty = np.empty(0)
        return {
            "ts_ns": np.empty(0, dtype=np.uint64),
            "mid_ticks": empty,
            "best_bid": np.empty(0, dtype=np.int32),
            "best_ask": np.empty(0, dtype=np.int32),
            "spread_ticks": np.empty(0, dtype=np.int32),
        }

    ts_out = events["ts_ns"].copy()
    mid_out = np.empty(n, dtype=np.float64)
    bid_out = np.empty(n, dtype=np.int32)
    ask_out = np.empty(n, dtype=np.int32)
    spread_out = np.empty(n, dtype=np.int32)

    book = _MiniBook(p0_ticks, levels_per_side, initial_spread_ticks, initial_depth)

    types = events["type"]
    prices = events["price_ticks"]
    qtys = events["qty"]

    for i in range(n):
        book.apply(int(types[i]), int(prices[i]), int(qtys[i]))

        bb = book.best_bid
        ba = book.best_ask
        bid_out[i] = bb
        ask_out[i] = ba
        mid_out[i] = (bb + ba) / 2.0
        spread_out[i] = ba - bb

    return {
        "ts_ns": ts_out,
        "mid_ticks": mid_out,
        "best_bid": bid_out,
        "best_ask": ask_out,
        "spread_ticks": spread_out,
    }
