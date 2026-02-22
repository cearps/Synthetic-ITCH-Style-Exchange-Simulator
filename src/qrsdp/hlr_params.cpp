#include "qrsdp/hlr_params.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace qrsdp {

namespace {

std::vector<double> makeTable(int n_max, double (*f)(int)) {
    std::vector<double> t;
    t.reserve(static_cast<size_t>(n_max) + 1);
    for (int n = 0; n <= n_max; ++n) {
        t.push_back(std::max(0.0, f(n)));
    }
    return t;
}

// Add at best: flat except lower at n=0.
double addBest(int n) {
    if (n == 0) return 2.0;
    return 5.0;
}
// Add deeper: decreasing with n.
double addDeeper(int n) {
    return 4.0 / (1.0 + 0.1 * static_cast<double>(n));
}
// Cancel: increasing concave then flatten.
double cancelCurve(int n) {
    if (n == 0) return 0.0;
    return 0.05 * std::sqrt(static_cast<double>(n)) + 0.02 * n;
}
// Market at best: decreasing with n (liquidity scarcity).
double marketCurve(int n) {
    if (n == 0) return 0.5;
    return 2.0 / (1.0 + 0.2 * static_cast<double>(n));
}

}  // namespace

HLRParams makeDefaultHLRParams(int K, int n_max) {
    HLRParams p;
    p.K = std::max(1, K);
    p.n_max = std::max(1, n_max);

    const size_t k = static_cast<size_t>(p.K);
    p.lambda_L_bid.resize(k);
    p.lambda_L_ask.resize(k);
    p.lambda_C_bid.resize(k);
    p.lambda_C_ask.resize(k);

    for (int i = 0; i < p.K; ++i) {
        const bool is_best = (i == 0);
        auto add_table = makeTable(p.n_max, is_best ? addBest : addDeeper);
        auto cancel_table = makeTable(p.n_max, cancelCurve);
        // Tail: flat for add (bounded), cancel dominates at large n when we use increasing cancel
        p.lambda_L_bid[static_cast<size_t>(i)].setTable(add_table, IntensityCurve::TailRule::FLAT);
        p.lambda_L_ask[static_cast<size_t>(i)].setTable(add_table, IntensityCurve::TailRule::FLAT);
        p.lambda_C_bid[static_cast<size_t>(i)].setTable(cancel_table, IntensityCurve::TailRule::FLAT);
        p.lambda_C_ask[static_cast<size_t>(i)].setTable(cancel_table, IntensityCurve::TailRule::FLAT);
    }

    auto market_table = makeTable(p.n_max, marketCurve);
    p.lambda_M_buy.setTable(market_table, IntensityCurve::TailRule::FLAT);
    p.lambda_M_sell.setTable(market_table, IntensityCurve::TailRule::FLAT);

    return p;
}

}  // namespace qrsdp
