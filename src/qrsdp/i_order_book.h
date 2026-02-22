#pragma once

#include "qrsdp/records.h"
#include <cstddef>

namespace qrsdp {

/// Order book: state, features, and event application. v1 = counts only (no FIFO).
class IOrderBook {
public:
    virtual ~IOrderBook() = default;
    virtual void seed(const BookSeed&) = 0;
    virtual BookFeatures features() const = 0;
    virtual void apply(const SimEvent&) = 0;
    virtual Level bestBid() const = 0;
    virtual Level bestAsk() const = 0;
    virtual size_t numLevels() const = 0;
    virtual int32_t bidPriceAtLevel(size_t k) const = 0;
    virtual int32_t askPriceAtLevel(size_t k) const = 0;
    virtual uint32_t bidDepthAtLevel(size_t k) const = 0;
    virtual uint32_t askDepthAtLevel(size_t k) const = 0;
};

}  // namespace qrsdp
