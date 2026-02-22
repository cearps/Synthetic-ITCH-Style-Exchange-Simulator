#pragma once

#include "rng/irng.h"
#include <cstdint>
#include <random>

namespace qrsdp {

/// Deterministic RNG: std::mt19937_64 with uniform [0, 1).
class Mt19937Rng : public IRng {
public:
    explicit Mt19937Rng(uint64_t seed = 0);
    double uniform() override;
    void seed(uint64_t s) override;

private:
    std::mt19937_64 gen_;
    std::uniform_real_distribution<double> dist_;
};

}  // namespace qrsdp
