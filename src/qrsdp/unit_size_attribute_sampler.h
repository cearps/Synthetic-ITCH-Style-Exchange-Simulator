#pragma once

#include "qrsdp/i_attribute_sampler.h"
#include "qrsdp/irng.h"
#include "qrsdp/records.h"
#include <array>
#include <cstddef>

namespace qrsdp {

constexpr size_t kAttrSamplerMaxLevels = 64;

/// v1: qty=1 always; level k with prob ∝ exp(-alpha*k); EXECUTE at best opposite.
class UnitSizeAttributeSampler : public IAttributeSampler {
public:
    UnitSizeAttributeSampler(IRng& rng, double alpha);
    EventAttrs sample(EventType, const IOrderBook&, const BookFeatures&,
                     size_t level_hint = kLevelHintNone) override;

private:
    IRng* rng_;
    double alpha_;
    std::array<double, kAttrSamplerMaxLevels> weight_buf_{};
    size_t sampleLevelIndex(size_t num_levels);
    /// For CANCEL: sample level with prob proportional to depth; only levels with depth > 0.
    size_t sampleCancelLevelIndex(bool is_bid, const IOrderBook& book);
};

}  // namespace qrsdp
