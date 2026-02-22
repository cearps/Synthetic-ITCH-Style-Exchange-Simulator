#pragma once

#include <cstddef>
#include <vector>

namespace qrsdp {

/// Queue-size-dependent intensity: table for n = 0..n_max and a tail rule beyond.
/// HLR2014: λ^X_i(n) for add/cancel/market per level i.
class IntensityCurve {
public:
    /// Tail rule beyond n_max: use last table value (flat) or zero.
    enum class TailRule { FLAT, ZERO };

    IntensityCurve() = default;

    /// Build from table; values for n = 0..n_max. Tail rule for n > n_max.
    void setTable(std::vector<double> values, TailRule tail = TailRule::FLAT);

    /// Lookup with nonnegativity clamp. Returns 0 for n < 0.
    double value(size_t n) const;

    size_t nMax() const { return n_max_; }
    bool empty() const { return table_.empty(); }

private:
    std::vector<double> table_;
    size_t n_max_ = 0;
    TailRule tail_ = TailRule::FLAT;

    static constexpr double kMinIntensity = 1e-12;
};

}  // namespace qrsdp
