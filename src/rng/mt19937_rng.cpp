#include "rng/mt19937_rng.h"

namespace qrsdp {

Mt19937Rng::Mt19937Rng(uint64_t seed) : gen_(seed), dist_(0.0, 1.0) {}

double Mt19937Rng::uniform() {
    return dist_(gen_);
}

void Mt19937Rng::seed(uint64_t s) {
    gen_.seed(s);
}

}  // namespace qrsdp
