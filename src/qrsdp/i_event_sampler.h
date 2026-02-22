#pragma once

#include "qrsdp/records.h"

namespace qrsdp {

/// Samples Δt ~ Exp(λ_total) and event type (categorical). Uses injected RNG.
class IEventSampler {
public:
    virtual ~IEventSampler() = default;
    /// Inter-arrival time in seconds.
    virtual double sampleDeltaT(double lambdaTotal) = 0;
    /// Which of the 6 event types occurred.
    virtual EventType sampleType(const Intensities&) = 0;
};

}  // namespace qrsdp
