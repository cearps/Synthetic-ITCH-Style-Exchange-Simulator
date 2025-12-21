# ADR 0004: Realistic Price Dynamics in Event Producer

## Status

Accepted

## Context

Issue #21 required implementing the full simulation pipeline with realistic market behavior. Initial price sampling implementation produced prices that simply oscillated around a starting position (e.g., 10000-10100), lacking the trends, volatility, and realistic movement patterns observed in real stock markets.

The event producer needed to generate limit order prices that:
- Show realistic trends (upward or downward movement over time)
- Exhibit volatility clustering (periods of high/low volatility)
- Demonstrate mean reversion (prices tend to return toward a long-term mean, but slowly)
- Create price movements that are more continuous and less random-walk-like

Constraints:
- Must remain deterministic (same seed = same price sequence)
- Should work with existing QR-SDP state-dependent intensity model
- Must integrate with order book state for realistic limit order placement
- Performance should not significantly degrade simulation speed

## Decision

We will implement a reference price model within the event producer that evolves using geometric Brownian motion with mean reversion. The reference price serves as the center of the price distribution for limit orders, allowing realistic trends and volatility while maintaining determinism.

### Implementation Approach

1. **Reference Price Tracking**: Maintain a floating-point reference price that evolves over time
   - Initialized at a reasonable mid-price (e.g., 10050 ticks)
   - Updated on each event using geometric Brownian motion with mean reversion

2. **Price Evolution Model**: 
   - Geometric Brownian Motion: `dS = S * (μ * dt + σ * dW)`
   - Mean Reversion: `-θ * (S - S₀) * dt` where θ is weak (allows trends to persist)
   - Volatility Clustering: GARCH-like model where volatility persists (high vol → high vol)

3. **Price Sampling**: Limit orders are sampled around the reference price:
   - 30% at best bid/ask (liquidity provision)
   - 30% near reference price (within 3 ticks)
   - 25% slightly away from reference (1-5 ticks)
   - 15% deeper (aggressive limit orders, 5+ ticks away)

4. **Parameters**:
   - Annual drift: Random -5% to +5% (creates trends)
   - Annual volatility: 30% (higher than typical 20% for more visible movement)
   - Mean reversion speed: θ = 0.05 (weak, allows trends to develop)
   - Volatility persistence: 90% (GARCH-like clustering)

## Alternatives considered

- **Pure random walk around fixed price**
  - Rejected: Produces oscillation without trends, doesn't match real market behavior

- **Fixed drift with constant volatility**
  - Rejected: Too simplistic, doesn't capture volatility clustering observed in markets

- **External price feed or pre-computed price series**
  - Rejected: Breaks determinism (would need to store entire price series), adds complexity

- **Order book-driven price discovery only**
  - Rejected: Without external price pressure, prices would be too constrained to best bid/ask levels

- **Strong mean reversion (θ = 0.5)**
  - Rejected: Pulls prices back to mean too quickly, prevents realistic trends from developing

## Consequences

- **Pros:**
  - Realistic price behavior with trends, volatility, and mean reversion
  - Maintains determinism (seed-based RNG for all random components)
  - Integrates naturally with existing QR-SDP model
  - Parameters can be tuned for different market regimes
  - Price movements are more continuous and realistic

- **Cons:**
  - Additional computational overhead (reference price update on each event)
  - Parameters need calibration for different market conditions
  - Reference price is separate from order book state (could diverge in extreme scenarios)
  - Simplified model doesn't capture all market microstructure effects

- **Follow-on work:**
  - Calibrate parameters for different market regimes (high volatility, trending, mean-reverting)
  - Add configuration options for drift, volatility, mean reversion strength
  - Consider making reference price update frequency configurable
  - Add validation to prevent reference price from diverging too far from order book mid-price
  - Consider adding jump components for sudden price movements

## Implementation Details

### Reference Price Update

The reference price is updated in `QRSDPEventProducer::update_reference_price()`:

```cpp
// Geometric Brownian Motion with mean reversion
double dt = delta_ns / nanoseconds_per_year;
double dW = normal_dist_(rng_) * sqrt(dt);
double drift_term = price_drift_ * dt;
double diffusion_term = scaled_vol * dW;
double mean_reversion_term = -theta * (reference_price_ - S0) * dt;

double log_price = log(reference_price_);
log_price += drift_term + diffusion_term + mean_reversion_term / reference_price_;
reference_price_ = exp(log_price);
```

### Price Sampling

Limit order prices are sampled in `QRSDPEventProducer::sample_price_for_add()` using the reference price as the distribution center, with adjustments to respect order book constraints (e.g., buy orders must be below best ask).

### Volatility Clustering

Volatility is updated using a GARCH-like model:
```cpp
current_volatility_ = 0.90 * current_volatility_ + 0.10 * base_volatility + shock;
```

This creates persistence where high volatility periods tend to be followed by high volatility periods.

## Notes / References

- Issue #21: Full simulation pipeline implementation
- Geometric Brownian Motion: Standard model for asset price evolution
- Mean Reversion: Ornstein-Uhlenbeck process for price dynamics
- GARCH models: Generalized Autoregressive Conditional Heteroskedasticity for volatility clustering
- Implementation: `src/producer/event_producer.cpp` (update_reference_price, sample_price_for_add)
- Visualization: CSV export and Python tools show realistic price trends and volatility


