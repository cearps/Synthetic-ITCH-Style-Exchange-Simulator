#include "qrsdp/simple_imbalance_intensity.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace qrsdp {

namespace {

constexpr double kEpsilon = 1e-9;

double clampNonNegative(double x) {
    if (std::isnan(x) || std::isinf(x) || x < 0.0) return kEpsilon;
    return std::max(x, kEpsilon);
}

}  // namespace

SimpleImbalanceIntensity::SimpleImbalanceIntensity(const IntensityParams& params)
    : params_(params) {}

Intensities SimpleImbalanceIntensity::compute(const BookState& state) const {
    const BookFeatures& f = state.features;
    const double I = std::isnan(f.imbalance) ? 0.0 : f.imbalance;
    const double q_bid = static_cast<double>(f.q_bid_best);
    const double q_ask = static_cast<double>(f.q_ask_best);

    const double add_bid = params_.base_L * (1.0 - I);
    const double add_ask = params_.base_L * (1.0 + I);
    const double eps_exec = (params_.epsilon_exec > 0.0) ? params_.epsilon_exec : 0.05;
    const double exec_sell = params_.base_M * (eps_exec + std::max(I, 0.0));
    const double exec_buy = params_.base_M * (eps_exec + std::max(-I, 0.0));
    const double cancel_bid = params_.base_C * q_bid;
    const double cancel_ask = params_.base_C * q_ask;

    Intensities out;
    out.add_bid = clampNonNegative(add_bid);
    out.add_ask = clampNonNegative(add_ask);
    out.cancel_bid = clampNonNegative(cancel_bid);
    out.cancel_ask = clampNonNegative(cancel_ask);
    out.exec_buy = clampNonNegative(exec_buy);
    out.exec_sell = clampNonNegative(exec_sell);
    return out;
}

}  // namespace qrsdp
