#pragma once

#include "book/i_order_book.h"
#include "rng/irng.h"
#include "core/records.h"
#include <array>
#include <cstddef>

namespace qrsdp {

constexpr size_t kMaxLevels = 64;

/// Counts-only order book: L levels per side, no FIFO. Satisfies bid < ask, spread >= 1.
class MultiLevelBook : public IOrderBook {
public:
    void seed(const BookSeed&) override;
    BookFeatures features() const override;
    void apply(const SimEvent&) override;
    Level bestBid() const override;
    Level bestAsk() const override;
    size_t numLevels() const override;
    int32_t bidPriceAtLevel(size_t k) const override;
    int32_t askPriceAtLevel(size_t k) const override;
    uint32_t bidDepthAtLevel(size_t k) const override;
    uint32_t askDepthAtLevel(size_t k) const override;
    void reinitialize(IRng& rng, double depth_mean) override;

private:
    struct LevelSlot {
        int32_t  price_ticks;
        uint32_t depth;
    };
    std::array<LevelSlot, kMaxLevels> bid_levels_{};
    std::array<LevelSlot, kMaxLevels> ask_levels_{};
    size_t num_levels_ = 0;
    uint32_t initial_depth_ = 50;

    void shiftBidBook();
    void shiftAskBook();
    int bidIndexForPrice(int32_t price_ticks) const;
    int askIndexForPrice(int32_t price_ticks) const;
};

}  // namespace qrsdp
