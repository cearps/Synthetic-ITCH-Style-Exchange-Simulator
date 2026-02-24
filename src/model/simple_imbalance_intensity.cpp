#include "model/simple_imbalance_intensity.h"
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

    double total_bid_depth = 0.0;
    double total_ask_depth = 0.0;
    for (auto d : state.bid_depths) total_bid_depth += static_cast<double>(d);
    for (auto d : state.ask_depths) total_ask_depth += static_cast<double>(d);
    if (total_bid_depth == 0.0) total_bid_depth = static_cast<double>(f.q_bid_best);
    if (total_ask_depth == 0.0) total_ask_depth = static_cast<double>(f.q_ask_best);

    const double sI = (params_.imbalance_sensitivity > 0.0) ? params_.imbalance_sensitivity : 1.0;
    const double sC = (params_.cancel_sensitivity > 0.0) ? params_.cancel_sensitivity : 1.0;

    // Spread-dependent feedback: wide spread attracts limit orders, dampens executions.
    // Uses exp(±sS * (spread - 2)) so that spread=2 is neutral (multiplier=1).
    const double sS = params_.spread_sensitivity;
    const double spread_delta = static_cast<double>(f.spread_ticks) - 2.0;
    const double add_spread_mult = (sS > 0.0) ? std::exp(sS * spread_delta) : 1.0;
    const double exec_spread_mult = (sS > 0.0) ? std::exp(-sS * spread_delta) : 1.0;

    const double add_bid = params_.base_L * (1.0 - sI * I) * add_spread_mult;
    const double add_ask = params_.base_L * (1.0 + sI * I) * add_spread_mult;
    const double eps_exec = (params_.epsilon_exec > 0.0) ? params_.epsilon_exec : 0.05;
    const double exec_sell = params_.base_M * (eps_exec + std::max(sI * I, 0.0)) * exec_spread_mult;
    const double exec_buy = params_.base_M * (eps_exec + std::max(-sI * I, 0.0)) * exec_spread_mult;
    const double cancel_bid = params_.base_C * sC * total_bid_depth;
    const double cancel_ask = params_.base_C * sC * total_ask_depth;

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
