#include "model/intensity_curve.h"
#include <algorithm>
#include <cmath>

namespace qrsdp {

void IntensityCurve::setTable(std::vector<double> values, TailRule tail) {
    table_ = std::move(values);
    for (double& v : table_) {
        if (!std::isfinite(v) || v < 0.0) v = 0.0;
        if (v > 0.0 && v < kMinIntensity) v = kMinIntensity;
    }
    n_max_ = table_.empty() ? 0 : table_.size() - 1;
    tail_ = tail;
}

double IntensityCurve::value(size_t n) const {
    if (table_.empty()) return 0.0;
    if (n <= n_max_) {
        double v = table_[n];
        return std::max(v, 0.0);
    }
    switch (tail_) {
        case TailRule::FLAT:
            return std::max(table_.back(), 0.0);
        case TailRule::ZERO:
            return 0.0;
    }
    return 0.0;
}

}  // namespace qrsdp
