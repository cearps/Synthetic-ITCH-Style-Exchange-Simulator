#pragma once

#include <cstdint>

namespace qrsdp {

/// Deterministic RNG interface; sampler and producer use for reproducibility.
class IRng {
public:
    virtual ~IRng() = default;
    /// Uniform [0, 1).
    virtual double uniform() = 0;
    /// Reseed (e.g. per session).
    virtual void seed(uint64_t s) = 0;
};

}  // namespace qrsdp
