#pragma once

#include "core/records.h"

#include <cstdint>
#include <string>
#include <vector>

namespace qrsdp {
namespace itch {

/// Encodes EventRecords into ITCH 5.0 binary messages for a single symbol.
class ItchEncoder {
public:
    /// @param symbol   Ticker symbol (max 8 chars, right-padded with spaces).
    /// @param locate   Stock locate code (unique per symbol in the session).
    /// @param tick_size Tick size in price-4 units (e.g. 100 means 1 tick = $0.0100).
    ItchEncoder(const std::string& symbol, uint16_t locate, uint32_t tick_size);

    /// Encode an EventRecord into the appropriate ITCH message bytes.
    std::vector<uint8_t> encode(const EventRecord& rec) const;

    /// Encode a System Event message (e.g. start/end of session).
    std::vector<uint8_t> encodeSystemEvent(char event_code, uint64_t ts_ns) const;

    /// Encode a Stock Directory message for this symbol.
    std::vector<uint8_t> encodeStockDirectory(uint64_t ts_ns) const;

    uint64_t nextMatchNumber() const { return match_number_; }

private:
    char     symbol_[8];
    uint16_t locate_;
    uint32_t tick_size_;
    mutable uint64_t match_number_ = 1;
};

}  // namespace itch
}  // namespace qrsdp
