#pragma once

#include "qrsdp/i_event_sampler.h"
#include "qrsdp/irng.h"
#include "qrsdp/records.h"

namespace qrsdp {

/// Δt ~ Exp(λ_total), event type ~ categorical(λ_i / λ_total). Uses injected IRng.
class CompetingIntensitySampler : public IEventSampler {
public:
    explicit CompetingIntensitySampler(IRng& rng);
    double sampleDeltaT(double lambdaTotal) override;
    EventType sampleType(const Intensities&) override;

private:
    IRng* rng_;
};

}  // namespace qrsdp
