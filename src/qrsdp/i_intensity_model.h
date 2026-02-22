#pragma once

#include "qrsdp/records.h"

namespace qrsdp {

/// Intensities from book state. Deterministic; no RNG.
/// Implementations may use state.features only (legacy) or full per-level state (HLR).
class IIntensityModel {
public:
    virtual ~IIntensityModel() = default;
    virtual Intensities compute(const BookState& state) const = 0;
};

}  // namespace qrsdp
