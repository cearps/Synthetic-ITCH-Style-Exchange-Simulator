#pragma once

#include "core/records.h"
#include <cstddef>
#include <vector>

namespace qrsdp {

/// Samples Δt ~ Exp(λ_total) and event type (categorical). Uses injected RNG.
class IEventSampler {
public:
    virtual ~IEventSampler() = default;
    /// Inter-arrival time in seconds.
    virtual double sampleDeltaT(double lambdaTotal) = 0;
    /// Which of the 6 event types occurred.
    virtual EventType sampleType(const Intensities&) = 0;
    /// Optional: sample index from categorical(weights). For HLR per-level sampling. Default: 0.
    virtual size_t sampleIndexFromWeights(const std::vector<double>& weights) {
        (void)weights;
        return 0;
    }
};

}  // namespace qrsdp
