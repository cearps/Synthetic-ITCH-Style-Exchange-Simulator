#pragma once

#include "sampler/i_attribute_sampler.h"
#include "rng/irng.h"
#include "core/records.h"
#include <array>
#include <cstddef>

namespace qrsdp {

constexpr size_t kAttrSamplerMaxLevels = 64;

/// v1: qty=1 always; level k with prob ∝ exp(-alpha*k); EXECUTE at best opposite.
/// When spread > 1 and spread_improve_coeff > 0, ADD events may target inside
/// the spread (price improvement) with probability min(1, (spread-1)*coeff).
class UnitSizeAttributeSampler : public IAttributeSampler {
public:
    UnitSizeAttributeSampler(IRng& rng, double alpha, double spread_improve_coeff = 0.0);
    EventAttrs sample(EventType, const IOrderBook&, const BookFeatures&,
                     size_t level_hint = kLevelHintNone) override;

private:
    IRng* rng_;
    double alpha_;
    double spread_improve_coeff_;
    std::array<double, kAttrSamplerMaxLevels> weight_buf_{};
    size_t sampleLevelIndex(size_t num_levels);
    size_t sampleCancelLevelIndex(bool is_bid, const IOrderBook& book);
};

}  // namespace qrsdp
