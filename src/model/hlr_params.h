#pragma once

#include "model/intensity_curve.h"
#include <cstddef>
#include <vector>

namespace qrsdp {

/// HLR2014 Model I parameters: K levels per side, n_max for curves, per-level λ^L, λ^C, λ^M.
struct HLRParams {
    int K = 5;
    int n_max = 100;

    /// Add (limit) intensity per level: λ^L_bid[i](n), λ^L_ask[i](n). Size K.
    std::vector<IntensityCurve> lambda_L_bid;
    std::vector<IntensityCurve> lambda_L_ask;
    /// Cancel intensity per level: λ^C_bid[i](n), λ^C_ask[i](n). Size K.
    std::vector<IntensityCurve> lambda_C_bid;
    std::vector<IntensityCurve> lambda_C_ask;
    /// Market intensity at best: λ^M_buy(n) at best ask, λ^M_sell(n) at best bid.
    IntensityCurve lambda_M_buy;
    IntensityCurve lambda_M_sell;
};

/// Build default starter curves (qualitative HLR): add flat/lower at n=0, cancel concave, market decreasing.
HLRParams makeDefaultHLRParams(int K = 5, int n_max = 100);

}  // namespace qrsdp
