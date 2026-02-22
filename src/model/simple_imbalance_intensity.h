#pragma once

#include "model/i_intensity_model.h"
#include "core/records.h"

namespace qrsdp {

/// Simple imbalance-driven intensities: add mean-reverts, exec follows pressure, cancel ∝ queue.
class SimpleImbalanceIntensity : public IIntensityModel {
public:
    explicit SimpleImbalanceIntensity(const IntensityParams& params);
    Intensities compute(const BookState& state) const override;

private:
    IntensityParams params_;
};

}  // namespace qrsdp
