 Queue-Reactive, State-Dependent Poisson (QR-SDP) Event Producer

This document explains the **initial market event producer algorithm** for the Synthetic ITCH Exchange Simulator, based on a Queue-Reactive, State-Dependent Poisson (QR-SDP) model.

## Overview

The producer's job is to generate **order flow** (adds, cancels, aggressive takes) in a way that is:

- **Deterministic** under a seed (replayable)
- **Reactive** to the evolving limit order book (LOB)
- **Simple enough** to implement and calibrate early
- **Upgradeable** later (Hawkes processes, agent-based models) without changing the downstream pipeline

## Where the Producer Fits

The full pipeline is:

1. **Event Producer (this component)** generates _intent_ events:

   - `AddLimit` (passive orders)
   - `Cancel` (passive order removals)
   - `AggressiveOrder` (market/crossing orders)

2. **Matching Engine + LOB** processes these events and generates fills/trades + updated book state.

3. **Deterministic Event Log** records all events (inputs + resulting executions).

4. **Encoder → UDP Streamer** converts logged events into ITCH-like binary messages over UDP.

Because the **event log is the source of truth**, the producer can be replaced later without changing:

- encoding rules,
- UDP streaming,
- consumer decoders,
- or strategy backtests.

## Why QR-SDP as the v0 Producer?

The QR-SDP producer is a strong starting point because it provides:

### 1) Endogenous price formation

Instead of simulating a price path first and "decorating" a book around it, QR-SDP simulates **order flow** and lets price emerge from the matching engine. This aligns with the queue-reactive model approach, where the limit order book is viewed as a Markov queuing system where order flow intensities depend on the current state of the order book [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf).

### 2) Realistic reactivity

Order arrival and cancellation rates vary with book state (spread, queue depth, imbalance), which reproduces many stylized microstructure effects without requiring complex agent logic. This state-dependent behavior is central to queue-reactive models.

### 3) Determinism and testability

Poisson-driven event simulation is easy to implement deterministically via seeded RNG and simulated time advancement.

### 4) Upgrade path

The QR-SDP framework is compatible with later upgrades:

- Poisson → Hawkes (self-excitation)
- rule-driven flow → agent-driven flow

while keeping the same event interface.

## QR-SDP Model in Plain English

**Queue-Reactive**: event rates depend on the current queues in the book (sizes at best bid/ask, depth, imbalance).

**State-Dependent**: rates depend on discrete or continuous features of the current LOB state (spread, imbalance, queue sizes, recent activity).

**Poisson**: events arrive randomly with intensities (rates). Over a short interval Δt, the probability of an event is approximately proportional to `λ(state) * Δt`.

This approach is inspired by queue-reactive models that view the limit order book as a Markov queuing system where order flow intensities depend only on the current state of the order book [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf).

## Key Concept: Competing Poisson Clocks

Model each "event family" as its own Poisson process:

- `AddLimitBid` (passive bid orders)
- `AddLimitAsk` (passive ask orders)
- `CancelBid` (cancels of resting bid orders)
- `CancelAsk` (cancels of resting ask orders)
- `AggressiveBuy` (crossing buy / market buy)
- `AggressiveSell` (crossing sell / market sell)

Each family has an intensity:

```
λ_k = λ_k(state)
```

As the book changes, these intensities change. At any point in time, the next event is whichever "clock" rings first.

## State Representation (v0)

The producer reads only a small subset of book state features (cheap but effective):

### Best-level state

- `bid1_price`, `ask1_price`
- `bid1_qty`, `ask1_qty`

### Derived features

- **spread** (in ticks): `spread = ask1_price - bid1_price`
- **imbalance**: `(bid1_qty - ask1_qty) / (bid1_qty + ask1_qty)`
- optional: **activity proxy**: recent event rate or short-term mid return

> **v0 intentionally focuses on top-of-book** to simplify and stabilize the model. This is similar to queue-reactive models that focus on the behavior of queues at different distances from a reference price [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf).

## Choosing the Intensity Functions λ(state)

For v0, use **piecewise-constant intensities** (lookup tables) rather than continuous functions.

### Step 1: Bucket the state into bins

**Spread bucket**

- `S1`: 1 tick
- `S2`: 2 ticks
- `S3`: 3+ ticks

**Imbalance bucket**

- `I--`: strongly ask-heavy (imbalance < -0.6)
- `I-`: mildly ask-heavy (-0.6 ≤ imbalance < -0.2)
- `I0`: balanced (-0.2 ≤ imbalance ≤ 0.2)
- `I+`: mildly bid-heavy (0.2 < imbalance ≤ 0.6)
- `I++`: strongly bid-heavy (imbalance > 0.6)

**Queue size bucket (per side)**

- `Qsmall`: small queue (e.g., qty < 100 shares)
- `Qmed`: medium queue (e.g., 100 ≤ qty < 1000 shares)
- `Qlarge`: large queue (e.g., qty ≥ 1000 shares)

A single state key might look like:

```
(spread_bucket=S2, imbalance_bucket=I+, bidQ=Qmed, askQ=Qsmall)
```

### Step 2: Assign a base rate per event family per state bin

Example behaviors:

- If spread is tight (1 tick), aggressive takes might be higher
- If ask queue is huge, cancels on ask might increase
- If bid side is dominant, passive ask adds might increase to rebalance

Formally:

```
λ_add_bid = table_add_bid[state_bin]
λ_add_ask = table_add_ask[state_bin]
λ_cancel_bid = table_cancel_bid[state_bin]
λ_cancel_ask = table_cancel_ask[state_bin]
λ_take_buy = table_take_buy[state_bin]
λ_take_sell = table_take_sell[state_bin]
```

### Why tables first?

- Extremely easy to calibrate later (counts / time in state)
- Transparent and debuggable
- Easy to tweak and observe changes
- Aligns with empirical calibration approaches used in queue-reactive models [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf)

## Sampling the Next Event Time and Type

At time `t`, compute the intensities for all event families:

```
λ_1, λ_2, ..., λ_K
```

Compute total rate:

```
Λ = Σ λ_k
```

If `Λ == 0`, you must define a fallback (e.g., nudge adds) or treat it as an invalid state.

### Sample time to next event

For Poisson superposition, the time to next event is exponential:

```
Δt ~ Exponential(rate = Λ)
t_next = t + Δt
```

### Sample event family

Choose event type `k` with probability:

```
P(event = k) = λ_k / Λ
```

This is the core "competing clocks" mechanic.

## Generating Event Parameters

Once the event family is chosen, generate its parameters.

### A) Passive AddLimit events

Parameters:

- `side` (bid/ask)
- `price` (which level)
- `qty` (order size)
- `order_id` (deterministic unique id)

#### Price selection (v0)

Use a simple depth distribution:

- ~80% at best
- ~15% at best ± 1 tick
- ~5% deeper levels (geometric decay)

If the spread is large enough, allow "inside-spread" price improvement:

- If `spread >= 2 ticks`, sometimes place at `bid1+1` (for bids) or `ask1-1` (for asks)

#### Size selection (v0)

Use a discrete distribution:

- Small sizes most frequent
- Occasional larger sizes
- Rounded to lot size

This can be a fixed histogram initially, then calibrated.

### B) Cancel events

Parameters:

- `order_id` (target)
- `qty` (partial cancel) or `ALL`

#### Cancel target selection (v0)

Start simple:

- Maintain a per-side list of live orders at best (or top N levels)
- Sample cancel target weighted by:
  - time-in-book (older orders more likely),
  - size (larger orders have higher cancel probability),
  - or uniform random among eligible orders

#### Cancel size

- Often partial (e.g. 20–50% of remaining qty)
- Sometimes full

> **Important**: cancels must never remove more than available qty.

### C) Aggressive (crossing) orders

Parameters:

- `side` (buy/sell)
- `qty` (taker size)
- optional: "limit price" far through the book (for a crossing limit style)

v0 approach:

- Aggressive buys consume at ask1 (and beyond if needed)
- Aggressive sells consume at bid1 (and beyond if needed)

Size distribution can be heavier-tailed than passive adds.

## Determinism Rules (Critical)

To keep runs reproducible:

1. **Single seed** controls all randomness.
2. Always call RNG in a **stable order** (avoid iteration over hash maps).
3. Order IDs are generated deterministically (e.g., monotonic counter).
4. Sim time is driven only by sampled `Δt` from the model (no wall clock).
5. Producer reads the LOB state **only after** the previous event has been applied.

If determinism breaks, your "replay from log" invariants will fail.

## Algorithm Pseudocode (v0 QR-SDP Producer)

```text
init(seed)
t = 0
order_id_counter = 1

while t < horizon:
  state = book.top_of_book_features()

  # 1) compute intensities for this state
  rates = [
    λ_add_bid(state),
    λ_add_ask(state),
    λ_cancel_bid(state),
    λ_cancel_ask(state),
    λ_take_buy(state),
    λ_take_sell(state)
  ]
  Λ = sum(rates)
  assert Λ > 0

  # 2) sample time to next event
  Δt = exponential(rate=Λ)
  t = t + Δt

  # 3) sample which event occurs
  k = categorical(weights=rates)

  # 4) generate event parameters based on k and current state
  event = generate_event(k, state, order_id_counter)
  if event creates a new order_id:
    order_id_counter += 1

  # 5) emit to matching engine + append to event log
  matcher.apply(event)
  event_log.append(t, event)

end
```

## How QR-SDP Drives Realism (What You'll See)

Even with v0 tables, you can reproduce useful "market-like" behaviours:

### Spread-dependent aggression

- Tighter spreads → more taking
- Wider spreads → more passive placement and price improvement

### Imbalance effects

If bid queue dominates, you often see:

- more passive asks joining,
- more aggressive sells,
- higher cancel pressure on the dominant side

### Queue size feedback

- Huge queues → higher cancels and more price improvement (competition to get filled)

These emerge naturally once λ depends on the book state, consistent with queue-reactive model observations [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf).

## Calibration Strategy (Later, Not Required for v0)

Once you have real decoded ITCH data:

1. Reconstruct state bins over time.
2. Count events by family in each bin.
3. Estimate:

```python
λ_k(bin) = count_k_in_bin / time_spent_in_bin
```

That gives you the lookup tables directly.

This approach is consistent with empirical estimation methods used in queue-reactive models, where parameters are estimated from market data by analyzing the time spent in different states and the events that occur in each state [[1]](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf).

## v0 Scope Checklist

- ✅ Competing Poisson clocks with state-dependent table rates
- ✅ Deterministic sim time + seed
- ✅ Minimal event set: add/cancel/aggressive
- ✅ Simple parameter generation for price level + size
- ✅ Stable order-id + event ordering
- ✅ Producer is isolated behind an interface (swap later)

## Common Pitfalls

- **Non-deterministic iteration order** (hash maps) causing seed drift
- **Cancels targeting orders that no longer exist** (race conditions)
- **Allowing "impossible" negative queues or crossing states**
- **Overcomplicating the intensity functions too early**
- **Coupling producer to encoder instead of event log interface**

## References

1. Huang, W., Lehalle, C.-A., & Rosenbaum, M. (2015). Simulating and analyzing order book data: The queue-reactive model. _Quantitative Finance_, 15(5), 795-812. [PDF](file://Simulating_and_Analyzing_Order_Book_Data_The_Queue.pdf)

## Implementation Notes

The `QRSDPEventProducer` class implements the `IEventProducer` interface defined in `src/producer/event_producer.h`. The implementation should follow the algorithm described above, ensuring:

- Deterministic behavior through seeded random number generation
- State-dependent intensity calculation using lookup tables
- Competing Poisson clock mechanics for event type selection
- Proper event parameter generation based on current book state

See the source code in `src/producer/event_producer.cpp` for the current implementation status.
