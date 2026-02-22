#pragma once

#include "qrsdp/i_intensity_model.h"
#include "qrsdp/hlr_params.h"
#include "qrsdp/records.h"
#include <vector>

namespace qrsdp {

/// HLR2014 Model I: queue-size-dependent intensities from curves per level.
/// Requires BookState.bid_depths and ask_depths filled (size >= params.K).
class CurveIntensityModel : public IIntensityModel {
public:
    explicit CurveIntensityModel(HLRParams params);

    Intensities compute(const BookState& state) const override;
    bool getPerLevelIntensities(std::vector<double>& weights_out) const override;

    /// Decode per-level index [0..4*K+1] to (EventType, level). K from last compute.
    static void decodePerLevelIndex(size_t index, int K, EventType& type_out, size_t& level_out);

private:
    HLRParams params_;
    mutable std::vector<double> last_per_level_;
    mutable int last_K_ = 0;
};

}  // namespace qrsdp
