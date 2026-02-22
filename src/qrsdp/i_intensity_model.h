#pragma once

#include "qrsdp/records.h"

namespace qrsdp {

/// Intensities from book state. Deterministic; no RNG.
class IIntensityModel {
public:
    virtual ~IIntensityModel() = default;
    virtual Intensities compute(const BookFeatures&) const = 0;
};

}  // namespace qrsdp
