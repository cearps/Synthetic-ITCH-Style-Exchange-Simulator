#pragma once

#include "core/event_types.h"
#include "core/records.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace qrsdp {

/// Scaffold for HLR MLE intensity estimation from event stream.
/// Λ̂(n) = 1 / mean(Δt | q=n),  λ̂_type(n) = Λ̂(n) * freq(type | q=n).
struct IntensityEstimator {
    /// Reset for a new calibration run.
    void reset();

    /// Record a sojourn: queue size n, dwell time dt_sec, and event type that occurred.
    void recordSojourn(uint32_t n, double dt_sec, EventType type);

    /// Compute Λ̂(n) = 1 / mean(Δt | q=n). Returns 0 if no observations for n.
    double lambdaTotal(uint32_t n) const;

    /// Compute λ̂ for given type at queue size n. Returns 0 if no observations.
    double lambdaType(uint32_t n, EventType type) const;

    /// Maximum n with any observations.
    size_t nMaxObserved() const;

private:
    struct Cell {
        double sum_dt = 0.0;
        uint64_t count = 0;
        uint64_t count_by_type[static_cast<size_t>(EventType::COUNT)] = {};
    };
    std::vector<Cell> cells_;
};

}  // namespace qrsdp
