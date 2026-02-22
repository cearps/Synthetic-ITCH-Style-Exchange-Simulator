#pragma once

#include <cstdint>

namespace qrsdp {

/// Event type for competing-intensity sampling (6-way categorical).
enum class EventType : uint8_t {
    ADD_BID = 0,
    ADD_ASK = 1,
    CANCEL_BID = 2,
    CANCEL_ASK = 3,
    EXECUTE_BUY = 4,
    EXECUTE_SELL = 5,
    COUNT = 6
};

inline constexpr int kNumEventTypes = static_cast<int>(EventType::COUNT);

/// Side for records and attributes.
enum class Side : uint8_t {
    BID = 0,
    ASK = 1,
    NA = 2
};

}  // namespace qrsdp
