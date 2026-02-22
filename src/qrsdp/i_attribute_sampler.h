#pragma once

#include "qrsdp/records.h"
#include "qrsdp/i_order_book.h"

namespace qrsdp {

/// Samples side, price_ticks, qty (and order_id for adds) given event type and book state.
class IAttributeSampler {
public:
    virtual ~IAttributeSampler() = default;
    virtual EventAttrs sample(EventType, const IOrderBook&, const BookFeatures&) = 0;
};

}  // namespace qrsdp
