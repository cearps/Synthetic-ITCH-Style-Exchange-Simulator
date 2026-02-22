#pragma once

#include "sampler/i_event_sampler.h"
#include "rng/irng.h"
#include "core/records.h"

namespace qrsdp {

/// Δt ~ Exp(λ_total), event type ~ categorical(λ_i / λ_total). Uses injected IRng.
class CompetingIntensitySampler : public IEventSampler {
public:
    explicit CompetingIntensitySampler(IRng& rng);
    double sampleDeltaT(double lambdaTotal) override;
    EventType sampleType(const Intensities&) override;
    size_t sampleIndexFromWeights(const std::vector<double>& weights) override;

private:
    IRng* rng_;
};

}  // namespace qrsdp
