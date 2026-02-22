#include "qrsdp/unit_size_attribute_sampler.h"
#include <cmath>
#include <algorithm>

namespace qrsdp {

UnitSizeAttributeSampler::UnitSizeAttributeSampler(IRng& rng, double alpha)
    : rng_(&rng), alpha_(alpha) {}

size_t UnitSizeAttributeSampler::sampleLevelIndex(size_t num_levels) {
    if (num_levels == 0) return 0;
    if (num_levels == 1) return 0;
    const size_t n = std::min(num_levels, kAttrSamplerMaxLevels);
    double total = 0.0;
    for (size_t k = 0; k < n; ++k) {
        weight_buf_[k] = std::exp(-alpha_ * static_cast<double>(k));
        total += weight_buf_[k];
    }
    if (total <= 0.0) return 0;
    const double u = rng_->uniform();
    double cum = 0.0;
    for (size_t k = 0; k < n; ++k) {
        cum += weight_buf_[k];
        if (u < cum / total) return k;
    }
    return n - 1;
}

size_t UnitSizeAttributeSampler::sampleCancelLevelIndex(bool is_bid, const IOrderBook& book) {
    const size_t n = std::min(book.numLevels(), kAttrSamplerMaxLevels);
    if (n == 0) return 0;
    double total = 0.0;
    for (size_t k = 0; k < n; ++k) {
        const uint32_t d = is_bid ? book.bidDepthAtLevel(k) : book.askDepthAtLevel(k);
        weight_buf_[k] = static_cast<double>(d);
        total += weight_buf_[k];
    }
    if (total <= 0.0) return 0;
    const double u = rng_->uniform();
    double cum = 0.0;
    for (size_t k = 0; k < n; ++k) {
        cum += weight_buf_[k];
        if (u < cum / total) return k;
    }
    return n - 1;
}

EventAttrs UnitSizeAttributeSampler::sample(EventType type, const IOrderBook& book,
                                            const BookFeatures& f) {
    EventAttrs out;
    out.qty = 1;
    out.order_id = 0;

    switch (type) {
        case EventType::ADD_BID:
            out.side = Side::BID;
            out.price_ticks = book.bidPriceAtLevel(sampleLevelIndex(book.numLevels()));
            break;
        case EventType::ADD_ASK:
            out.side = Side::ASK;
            out.price_ticks = book.askPriceAtLevel(sampleLevelIndex(book.numLevels()));
            break;
        case EventType::CANCEL_BID:
            out.side = Side::BID;
            out.price_ticks = book.bidPriceAtLevel(sampleCancelLevelIndex(true, book));
            break;
        case EventType::CANCEL_ASK:
            out.side = Side::ASK;
            out.price_ticks = book.askPriceAtLevel(sampleCancelLevelIndex(false, book));
            break;
        case EventType::EXECUTE_BUY:
            out.side = Side::ASK;
            out.price_ticks = f.best_ask_ticks;
            break;
        case EventType::EXECUTE_SELL:
            out.side = Side::BID;
            out.price_ticks = f.best_bid_ticks;
            break;
        default:
            out.side = Side::NA;
            out.price_ticks = 0;
            break;
    }
    return out;
}

}  // namespace qrsdp
