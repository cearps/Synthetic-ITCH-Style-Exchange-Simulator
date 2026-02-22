#include "qrsdp/intensity_estimator.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace qrsdp {

void IntensityEstimator::reset() {
    cells_.clear();
}

void IntensityEstimator::recordSojourn(uint32_t n, double dt_sec, EventType type) {
    const size_t idx = static_cast<size_t>(n);
    if (idx >= cells_.size()) cells_.resize(idx + 1);
    Cell& c = cells_[idx];
    c.sum_dt += dt_sec;
    c.count += 1;
    const size_t ti = static_cast<size_t>(type);
    if (ti < static_cast<size_t>(EventType::COUNT)) c.count_by_type[ti] += 1;
}

double IntensityEstimator::lambdaTotal(uint32_t n) const {
    if (n >= cells_.size()) return 0.0;
    const Cell& c = cells_[n];
    if (c.count == 0 || c.sum_dt <= 0.0) return 0.0;
    return static_cast<double>(c.count) / c.sum_dt;
}

double IntensityEstimator::lambdaType(uint32_t n, EventType type) const {
    const double lambda_tot = lambdaTotal(n);
    if (lambda_tot <= 0.0) return 0.0;
    if (n >= cells_.size()) return 0.0;
    const Cell& c = cells_[n];
    const size_t ti = static_cast<size_t>(type);
    if (ti >= static_cast<size_t>(EventType::COUNT)) return 0.0;
    if (c.count == 0) return 0.0;
    const double freq = static_cast<double>(c.count_by_type[ti]) / static_cast<double>(c.count);
    return lambda_tot * freq;
}

size_t IntensityEstimator::nMaxObserved() const {
    for (size_t i = cells_.size(); i > 0; --i) {
        if (cells_[i - 1].count > 0) return i - 1;
    }
    return 0;
}

}  // namespace qrsdp
