#include "sampler/competing_intensity_sampler.h"
#include <cmath>
#include <limits>
#include <vector>

namespace qrsdp {

namespace {

constexpr double kMinU = 1e-10;
constexpr double kSafeDeltaT = 1e9;

}  // namespace

CompetingIntensitySampler::CompetingIntensitySampler(IRng& rng) : rng_(&rng) {}

double CompetingIntensitySampler::sampleDeltaT(double lambdaTotal) {
    if (lambdaTotal <= 0.0 || !std::isfinite(lambdaTotal)) return kSafeDeltaT;
    double u = rng_->uniform();
    if (u <= 0.0 || u >= 1.0) u = kMinU;
    if (u < kMinU) u = kMinU;
    return -std::log(u) / lambdaTotal;
}

EventType CompetingIntensitySampler::sampleType(const Intensities& intens) {
    const double total = intens.total();
    if (total <= 0.0 || !std::isfinite(total)) return EventType::ADD_BID;
    const double u = rng_->uniform();
    double cum = 0.0;
    const EventType types[] = {
        EventType::ADD_BID, EventType::ADD_ASK, EventType::CANCEL_BID,
        EventType::CANCEL_ASK, EventType::EXECUTE_BUY, EventType::EXECUTE_SELL
    };
    for (EventType t : types) {
        cum += intens.at(t);
        if (u < cum / total) return t;
    }
    return EventType::EXECUTE_SELL;
}

size_t CompetingIntensitySampler::sampleIndexFromWeights(const std::vector<double>& weights) {
    if (weights.empty()) return 0;
    double total = 0.0;
    for (double w : weights) {
        if (std::isfinite(w) && w > 0.0) total += w;
    }
    if (total <= 0.0) return 0;
    const double u = rng_->uniform();
    if (u <= 0.0 || u >= 1.0) return 0;
    double cum = 0.0;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (std::isfinite(weights[i]) && weights[i] > 0.0) {
            cum += weights[i];
            if (u < cum / total) return i;
        }
    }
    return weights.size() - 1;
}

}  // namespace qrsdp
