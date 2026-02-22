#include "qrsdp/curve_intensity_model.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace qrsdp {

namespace {

constexpr double kEpsilon = 1e-12;

}

CurveIntensityModel::CurveIntensityModel(HLRParams params) : params_(std::move(params)) {
    last_K_ = params_.K;
    last_per_level_.resize(static_cast<size_t>(4 * params_.K + 2), 0.0);
}

Intensities CurveIntensityModel::compute(const BookState& state) const {
    const int K = params_.K;
    const size_t ku = static_cast<size_t>(K);
    if (state.bid_depths.size() < ku || state.ask_depths.size() < ku) {
        Intensities out;
        out.add_bid = out.add_ask = out.cancel_bid = out.cancel_ask = kEpsilon;
        out.exec_buy = out.exec_sell = kEpsilon;
        return out;
    }

    double add_bid = 0.0, add_ask = 0.0, cancel_bid = 0.0, cancel_ask = 0.0;
    double exec_buy = 0.0, exec_sell = 0.0;

    last_per_level_.assign(static_cast<size_t>(4 * K + 2), 0.0);
    last_K_ = K;

    for (int i = 0; i < K; ++i) {
        const size_t si = static_cast<size_t>(i);
        const size_t n_bid = state.bid_depths[si];
        const size_t n_ask = state.ask_depths[si];

        double lb = 0.0, la = 0.0, cb = 0.0, ca = 0.0;
        if (si < params_.lambda_L_bid.size()) lb = params_.lambda_L_bid[si].value(n_bid);
        if (si < params_.lambda_L_ask.size()) la = params_.lambda_L_ask[si].value(n_ask);
        if (si < params_.lambda_C_bid.size()) cb = params_.lambda_C_bid[si].value(n_bid);
        if (si < params_.lambda_C_ask.size()) ca = params_.lambda_C_ask[si].value(n_ask);

        add_bid += lb;
        add_ask += la;
        cancel_bid += cb;
        cancel_ask += ca;

        last_per_level_[si] = lb;
        last_per_level_[ku + si] = la;
        last_per_level_[2 * ku + si] = cb;
        last_per_level_[3 * ku + si] = ca;
    }
    exec_buy = params_.lambda_M_buy.value(state.ask_depths[0]);
    exec_sell = params_.lambda_M_sell.value(state.bid_depths[0]);
    last_per_level_[4 * ku] = exec_buy;
    last_per_level_[4 * ku + 1] = exec_sell;

    Intensities out;
    out.add_bid = std::max(add_bid, kEpsilon);
    out.add_ask = std::max(add_ask, kEpsilon);
    out.cancel_bid = std::max(cancel_bid, kEpsilon);
    out.cancel_ask = std::max(cancel_ask, kEpsilon);
    out.exec_buy = std::max(exec_buy, kEpsilon);
    out.exec_sell = std::max(exec_sell, kEpsilon);
    return out;
}

bool CurveIntensityModel::getPerLevelIntensities(std::vector<double>& weights_out) const {
    if (last_per_level_.empty()) return false;
    weights_out = last_per_level_;
    return true;
}

void CurveIntensityModel::decodePerLevelIndex(size_t index, int K, EventType& type_out, size_t& level_out) {
    const size_t ku = static_cast<size_t>(K);
    if (index < ku) {
        type_out = EventType::ADD_BID;
        level_out = index;
        return;
    }
    if (index < 2 * ku) {
        type_out = EventType::ADD_ASK;
        level_out = index - ku;
        return;
    }
    if (index < 3 * ku) {
        type_out = EventType::CANCEL_BID;
        level_out = index - 2 * ku;
        return;
    }
    if (index < 4 * ku) {
        type_out = EventType::CANCEL_ASK;
        level_out = index - 3 * ku;
        return;
    }
    if (index == 4 * ku) {
        type_out = EventType::EXECUTE_BUY;
        level_out = 0;
        return;
    }
    type_out = EventType::EXECUTE_SELL;
    level_out = 0;
}

}  // namespace qrsdp
