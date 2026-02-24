#pragma once

#include "model/intensity_curve.h"
#include <cstddef>
#include <string>
#include <vector>

namespace qrsdp {

/// HLR2014 Model I parameters: K levels per side, n_max for curves, per-level λ^L, λ^C, λ^M.
struct HLRParams {
    int K = 5;
    int n_max = 100;

    /// Spread-dependent feedback strength (like SimpleImbalance spread_sensitivity).
    /// When > 0, boosts add intensity and dampens exec intensity when spread > 2 ticks.
    double spread_sensitivity = 0.3;

    /// Imbalance-driven feedback: when > 0, executions are boosted on the heavier
    /// side and dampened on the lighter side, creating mean-reverting price dynamics.
    double imbalance_sensitivity = 1.0;

    /// Add (limit) intensity per level: λ^L_bid[i](n), λ^L_ask[i](n). Size K.
    std::vector<IntensityCurve> lambda_L_bid;
    std::vector<IntensityCurve> lambda_L_ask;
    /// Cancel intensity per level: λ^C_bid[i](n), λ^C_ask[i](n). Size K.
    std::vector<IntensityCurve> lambda_C_bid;
    std::vector<IntensityCurve> lambda_C_ask;
    /// Market intensity at best: λ^M_buy(n) at best ask, λ^M_sell(n) at best bid.
    IntensityCurve lambda_M_buy;
    IntensityCurve lambda_M_sell;

    /// True if curves have been populated (loaded from JSON or built from defaults).
    bool hasCurves() const { return !lambda_L_bid.empty(); }
};

/// Build default starter curves (qualitative HLR): add flat/lower at n=0, cancel concave, market decreasing.
HLRParams makeDefaultHLRParams(int K = 5, int n_max = 100);

/// Save full HLRParams (all curves + metadata) to a single JSON file.
bool saveHLRParamsToJson(const std::string& path, const HLRParams& params);

/// Load full HLRParams from JSON file. Returns true on success.
bool loadHLRParamsFromJson(const std::string& path, HLRParams& params);

}  // namespace qrsdp
