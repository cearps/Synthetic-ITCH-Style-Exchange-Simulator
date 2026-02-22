#include "qrsdp/multi_level_book.h"
#include "qrsdp/irng.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace qrsdp {

namespace {

constexpr double kImbalanceEps = 1e-9;

/// Simple Poisson(mean) draw; returns nonnegative integer.
uint32_t poissonSample(IRng& rng, double mean) {
    if (mean <= 0.0) return 0;
    if (mean > 1e6) return static_cast<uint32_t>(mean);
    double u = rng.uniform();
    if (u <= 0.0 || u >= 1.0) u = 0.5;
    double p = std::exp(-mean);
    double s = p;
    uint32_t k = 0;
    while (u > s) {
        ++k;
        p *= mean / static_cast<double>(k);
        s += p;
    }
    return k;
}

}  // namespace

void MultiLevelBook::seed(const BookSeed& s) {
    num_levels_ = std::min(static_cast<size_t>(s.levels_per_side), kMaxLevels);
    if (num_levels_ == 0) num_levels_ = 1;
    initial_depth_ = s.initial_depth > 0 ? s.initial_depth : 50u;
    const uint32_t spread = s.initial_spread_ticks > 0 ? s.initial_spread_ticks : 2u;
    const int half = static_cast<int>(spread / 2);

    const int32_t best_bid = s.p0_ticks - half;
    const int32_t best_ask = s.p0_ticks + static_cast<int>(spread) - half;

    for (size_t k = 0; k < num_levels_; ++k) {
        bid_levels_[k].price_ticks = static_cast<int32_t>(best_bid - static_cast<int>(k));
        bid_levels_[k].depth = initial_depth_;
        ask_levels_[k].price_ticks = static_cast<int32_t>(best_ask + static_cast<int>(k));
        ask_levels_[k].depth = initial_depth_;
    }
}

BookFeatures MultiLevelBook::features() const {
    if (num_levels_ == 0) {
        return BookFeatures{0, 0, 0, 0, 0, 0.0};
    }
    const int32_t best_bid = bid_levels_[0].price_ticks;
    const int32_t best_ask = ask_levels_[0].price_ticks;
    const uint32_t q_bid = bid_levels_[0].depth;
    const uint32_t q_ask = ask_levels_[0].depth;
    const int spread = best_ask - best_bid;
    const double sum = static_cast<double>(q_bid) + static_cast<double>(q_ask) + kImbalanceEps;
    const double imbalance = (static_cast<double>(q_bid) - static_cast<double>(q_ask)) / sum;
    return BookFeatures{best_bid, best_ask, q_bid, q_ask, spread, imbalance};
}

void MultiLevelBook::apply(const SimEvent& e) {
    switch (e.type) {
        case EventType::ADD_BID: {
            const int idx = bidIndexForPrice(e.price_ticks);
            if (idx >= 0 && static_cast<size_t>(idx) < num_levels_) {
                bid_levels_[static_cast<size_t>(idx)].depth += e.qty;
            }
            break;
        }
        case EventType::ADD_ASK: {
            const int idx = askIndexForPrice(e.price_ticks);
            if (idx >= 0 && static_cast<size_t>(idx) < num_levels_) {
                ask_levels_[static_cast<size_t>(idx)].depth += e.qty;
            }
            break;
        }
        case EventType::CANCEL_BID: {
            const int idx = bidIndexForPrice(e.price_ticks);
            if (idx >= 0 && static_cast<size_t>(idx) < num_levels_) {
                auto& d = bid_levels_[static_cast<size_t>(idx)].depth;
                if (d >= e.qty) d -= e.qty;
                else d = 0;
            }
            break;
        }
        case EventType::CANCEL_ASK: {
            const int idx = askIndexForPrice(e.price_ticks);
            if (idx >= 0 && static_cast<size_t>(idx) < num_levels_) {
                auto& d = ask_levels_[static_cast<size_t>(idx)].depth;
                if (d >= e.qty) d -= e.qty;
                else d = 0;
            }
            break;
        }
        case EventType::EXECUTE_BUY: {
            if (num_levels_ > 0) {
                const int32_t best_ask = ask_levels_[0].price_ticks;
                if (e.price_ticks != best_ask) {
                    std::fprintf(stderr, "QRSDP: EXECUTE_BUY target price %d != best ask %d (not k=0)\n",
                                 e.price_ticks, best_ask);
                }
                if (ask_levels_[0].depth > 0) {
                    --ask_levels_[0].depth;
                    if (ask_levels_[0].depth == 0) shiftAskBook();
                }
            }
            break;
        }
        case EventType::EXECUTE_SELL: {
            if (num_levels_ > 0) {
                const int32_t best_bid = bid_levels_[0].price_ticks;
                if (e.price_ticks != best_bid) {
                    std::fprintf(stderr, "QRSDP: EXECUTE_SELL target price %d != best bid %d (not k=0)\n",
                                 e.price_ticks, best_bid);
                }
                if (bid_levels_[0].depth > 0) {
                    --bid_levels_[0].depth;
                    if (bid_levels_[0].depth == 0) shiftBidBook();
                }
            }
            break;
        }
        default:
            break;
    }
}

Level MultiLevelBook::bestBid() const {
    if (num_levels_ == 0) return Level{0, 0};
    return Level{bid_levels_[0].price_ticks, bid_levels_[0].depth};
}

Level MultiLevelBook::bestAsk() const {
    if (num_levels_ == 0) return Level{0, 0};
    return Level{ask_levels_[0].price_ticks, ask_levels_[0].depth};
}

size_t MultiLevelBook::numLevels() const {
    return num_levels_;
}

int32_t MultiLevelBook::bidPriceAtLevel(size_t k) const {
    if (k >= num_levels_) return bid_levels_[num_levels_ - 1].price_ticks;
    return bid_levels_[k].price_ticks;
}

int32_t MultiLevelBook::askPriceAtLevel(size_t k) const {
    if (k >= num_levels_) return ask_levels_[num_levels_ - 1].price_ticks;
    return ask_levels_[k].price_ticks;
}

uint32_t MultiLevelBook::bidDepthAtLevel(size_t k) const {
    if (k >= num_levels_) return 0;
    return bid_levels_[k].depth;
}

uint32_t MultiLevelBook::askDepthAtLevel(size_t k) const {
    if (k >= num_levels_) return 0;
    return ask_levels_[k].depth;
}

void MultiLevelBook::shiftBidBook() {
    for (size_t i = 0; i + 1 < num_levels_; ++i) {
        bid_levels_[i] = bid_levels_[i + 1];
    }
    bid_levels_[num_levels_ - 1].price_ticks = bid_levels_[num_levels_ - 2].price_ticks - 1;
    bid_levels_[num_levels_ - 1].depth = initial_depth_;
}

void MultiLevelBook::shiftAskBook() {
    for (size_t i = 0; i + 1 < num_levels_; ++i) {
        ask_levels_[i] = ask_levels_[i + 1];
    }
    ask_levels_[num_levels_ - 1].price_ticks = ask_levels_[num_levels_ - 2].price_ticks + 1;
    ask_levels_[num_levels_ - 1].depth = initial_depth_;
}

void MultiLevelBook::reinitialize(IRng& rng, double depth_mean) {
    const double mu = depth_mean > 0.0 ? depth_mean : static_cast<double>(initial_depth_);
    for (size_t k = 0; k < num_levels_; ++k) {
        bid_levels_[k].depth = poissonSample(rng, mu);
        ask_levels_[k].depth = poissonSample(rng, mu);
    }
}

int MultiLevelBook::bidIndexForPrice(int32_t price_ticks) const {
    if (num_levels_ == 0) return -1;
    const int32_t best = bid_levels_[0].price_ticks;
    const int idx = best - price_ticks;
    if (idx < 0 || static_cast<size_t>(idx) >= num_levels_) return -1;
    return idx;
}

int MultiLevelBook::askIndexForPrice(int32_t price_ticks) const {
    if (num_levels_ == 0) return -1;
    const int32_t best = ask_levels_[0].price_ticks;
    const int idx = price_ticks - best;
    if (idx < 0 || static_cast<size_t>(idx) >= num_levels_) return -1;
    return idx;
}

}  // namespace qrsdp
