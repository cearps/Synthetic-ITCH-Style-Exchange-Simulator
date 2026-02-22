#pragma once

#include "qrsdp/records.h"
#include "qrsdp/event_types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace qrsdp {

/// Scaffold for parsing ITCH-like event streams and reconstructing queue state (level I/II).
/// Later: read from file/stream, decode message types, maintain bid/ask depths per level.
struct EventLogParser {
    /// Reset state for a new symbol/session.
    void reset();

    /// Process one event record (e.g. from our EventRecord or decoded ITCH).
    /// Returns true if consumed. Later: decode ITCH message and push event.
    bool push(const EventRecord& rec);

    /// Reconstructed queue depths after processing all events so far.
    /// bid_depths[i] = depth at level i (0 = best bid). Empty until first event.
    std::vector<uint32_t> bid_depths;
    std::vector<uint32_t> ask_depths;

    /// Current best bid/ask ticks (0 if unknown).
    int32_t best_bid_ticks = 0;
    int32_t best_ask_ticks = 0;

    /// Number of events pushed.
    uint64_t event_count = 0;
};

}  // namespace qrsdp
