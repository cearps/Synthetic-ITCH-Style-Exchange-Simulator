#pragma once

#include "qrsdp/event_types.h"
#include <cstdint>
#include <vector>

namespace qrsdp {

// --- EventRecord flags ---
constexpr uint32_t kFlagNone      = 0x0;
constexpr uint32_t kFlagShiftUp   = 0x1;  // best ask moved up (ask-side depletion)
constexpr uint32_t kFlagShiftDown = 0x2;  // best bid moved down (bid-side depletion)
constexpr uint32_t kFlagReinit    = 0x4;  // book depths were reinitialized after this shift

// --- EventRecord (fixed width, packed) ---
#pragma pack(push, 1)
struct EventRecord {
    uint64_t ts_ns;
    uint8_t  type;
    uint8_t  side;
    int32_t  price_ticks;
    uint32_t qty;
    uint64_t order_id;
    uint32_t flags;
};
#pragma pack(pop)

// --- TradingSession input ---
struct IntensityParams {
    double base_L;
    double base_C;
    double base_M;
    double imbalance_sensitivity;
    double cancel_sensitivity;
    double epsilon_exec;  // baseline execution intensity when imbalance ~ 0 (default 0.05)
};

/// HLR2014 Model III queue-reactive: optional reinit after shift.
struct QueueReactiveParams {
    double theta = 0.0;           // probability of adopting mid move as ref (0 = N/A)
    double theta_reinit = 0.0;   // probability to reinitialize book after a shift (0 = off)
    double reinit_depth_mean = 10.0;  // mean depth when reinitializing (e.g. Poisson)
};

struct TradingSession {
    uint64_t seed;
    int32_t  p0_ticks;
    uint32_t session_seconds;
    uint32_t levels_per_side;
    uint32_t tick_size;
    uint32_t initial_spread_ticks;  // spread at t=0 (default 2: best_bid=p0-1, best_ask=p0+1)
    uint32_t initial_depth;         // 0 = use producer default (50)
    IntensityParams intensity_params;
    QueueReactiveParams queue_reactive;
};

// --- SessionResult output ---
struct SessionResult {
    int32_t  close_ticks;
    uint64_t events_written;
};

// --- Book seeding ---
struct BookSeed {
    int32_t  p0_ticks;
    uint32_t levels_per_side;
    uint32_t initial_depth;
    uint32_t initial_spread_ticks;  // spread at t=0 (e.g. 2 => best_bid=p0-1, best_ask=p0+1)
};

// --- Features from book state (input to intensity model) ---
struct BookFeatures {
    int32_t  best_bid_ticks;
    int32_t  best_ask_ticks;
    uint32_t q_bid_best;
    uint32_t q_ask_best;
    int      spread_ticks;
    double   imbalance;
};

// --- Full book state (extensible for HLR per-level queues) ---
struct BookState {
    BookFeatures features;
    /// Per-level queue depths for HLR curve model. Empty = use features only (legacy).
    std::vector<uint32_t> bid_depths;
    std::vector<uint32_t> ask_depths;
};

// --- Intensities (6 rates for competing risks) ---
struct Intensities {
    double add_bid;
    double add_ask;
    double cancel_bid;
    double cancel_ask;
    double exec_buy;
    double exec_sell;

    double total() const {
        return add_bid + add_ask + cancel_bid + cancel_ask + exec_buy + exec_sell;
    }

    double at(EventType t) const {
        switch (t) {
            case EventType::ADD_BID:     return add_bid;
            case EventType::ADD_ASK:     return add_ask;
            case EventType::CANCEL_BID:  return cancel_bid;
            case EventType::CANCEL_ASK:  return cancel_ask;
            case EventType::EXECUTE_BUY:  return exec_buy;
            case EventType::EXECUTE_SELL: return exec_sell;
            default: return 0.0;
        }
    }
};

// --- Level (best bid/ask) ---
struct Level {
    int32_t  price_ticks;
    uint32_t depth;
};

// --- Internal event applied to the book ---
struct SimEvent {
    EventType type;
    Side      side;
    int32_t   price_ticks;
    uint32_t  qty;
    uint64_t  order_id;
};

// --- Attributes sampled for an event ---
struct EventAttrs {
    Side     side;
    int32_t  price_ticks;
    uint32_t qty;
    uint64_t order_id;
};

}  // namespace qrsdp
