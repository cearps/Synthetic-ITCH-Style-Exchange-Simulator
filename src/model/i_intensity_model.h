#pragma once

#include "core/records.h"
#include <vector>

namespace qrsdp {

/// Intensities from book state. Deterministic; no RNG.
/// Implementations may use state.features only (legacy) or full per-level state (HLR).
class IIntensityModel {
public:
    virtual ~IIntensityModel() = default;
    virtual Intensities compute(const BookState& state) const = 0;

    /// Optional: per-level intensities for (level, type) categorical sampling.
    /// If true, weights_out has 4*K+2 entries: [add_bid_0..add_bid_{K-1}, add_ask_0.., cancel_bid_0.., cancel_ask_0.., exec_buy, exec_sell].
    /// Producer samples index then decodes to (EventType, level). Default: false.
    virtual bool getPerLevelIntensities(std::vector<double>& weights_out) const {
        (void)weights_out;
        return false;
    }
};

}  // namespace qrsdp
