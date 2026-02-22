#pragma once

#include "core/records.h"
#include "book/i_order_book.h"
#include <cstddef>

namespace qrsdp {

/// No level hint: sampler chooses level (legacy / ADD with exp(-alpha*k), CANCEL by depth).
constexpr size_t kLevelHintNone = static_cast<size_t>(-1);

/// Samples side, price_ticks, qty (and order_id for adds) given event type and book state.
/// When level_hint != kLevelHintNone, use that level for ADD/CANCEL; EXECUTE always at best (0).
class IAttributeSampler {
public:
    virtual ~IAttributeSampler() = default;
    virtual EventAttrs sample(EventType, const IOrderBook&, const BookFeatures&,
                             size_t level_hint = kLevelHintNone) = 0;
};

}  // namespace qrsdp
