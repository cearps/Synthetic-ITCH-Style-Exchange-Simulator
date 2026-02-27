#include "io/event_log_reader.h"
#include "io/event_log_format.h"
#include "book/multi_level_book.h"
#include "calibration/intensity_estimator.h"
#include "model/hlr_params.h"
#include "model/intensity_curve.h"
#include "core/records.h"
#include "core/event_types.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void printUsage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "Calibrate HLR intensity curves from .qrsdp event log files.\n\n"
        "  --input <file>       Input .qrsdp file (may be repeated)\n"
        "  --output <file>      Output JSON curves file (default: hlr_curves.json)\n"
        "  --levels <K>         Levels per side for curves (default: from file header)\n"
        "  --n-max <n>          Max queue size for tables (default: 100)\n"
        "  --spread-sens <f>    Spread sensitivity for output (default: 0.3)\n"
        "  --verbose            Print per-level summaries\n"
        "  --help               Show this help\n",
        prog);
}

namespace {

struct LevelTracker {
    double last_event_time = 0.0;
    uint32_t last_depth = 0;
    bool initialized = false;
};

int findBidLevel(const qrsdp::MultiLevelBook& book, int32_t price) {
    for (size_t k = 0; k < book.numLevels(); ++k) {
        if (book.bidPriceAtLevel(k) == price) return static_cast<int>(k);
    }
    return -1;
}

int findAskLevel(const qrsdp::MultiLevelBook& book, int32_t price) {
    for (size_t k = 0; k < book.numLevels(); ++k) {
        if (book.askPriceAtLevel(k) == price) return static_cast<int>(k);
    }
    return -1;
}

void snapshotLevels(const qrsdp::MultiLevelBook& book, double t,
                    std::vector<LevelTracker>& bid_trackers,
                    std::vector<LevelTracker>& ask_trackers) {
    const size_t K = book.numLevels();
    bid_trackers.resize(K);
    ask_trackers.resize(K);
    for (size_t k = 0; k < K; ++k) {
        bid_trackers[k].last_depth = book.bidDepthAtLevel(k);
        bid_trackers[k].last_event_time = t;
        bid_trackers[k].initialized = true;
        ask_trackers[k].last_depth = book.askDepthAtLevel(k);
        ask_trackers[k].last_event_time = t;
        ask_trackers[k].initialized = true;
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> input_files;
    std::string output_file = "hlr_curves.json";
    int levels_override = 0;
    int n_max = 100;
    double spread_sens = 0.3;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", arg);
                std::exit(1);
            }
            return argv[++i];
        };

        if (std::strcmp(arg, "--input") == 0)        input_files.emplace_back(next());
        else if (std::strcmp(arg, "--output") == 0)   output_file = next();
        else if (std::strcmp(arg, "--levels") == 0)   levels_override = std::atoi(next());
        else if (std::strcmp(arg, "--n-max") == 0)    n_max = std::atoi(next());
        else if (std::strcmp(arg, "--spread-sens") == 0) spread_sens = std::atof(next());
        else if (std::strcmp(arg, "--verbose") == 0)  verbose = true;
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg);
            printUsage(argv[0]);
            return 1;
        }
    }

    if (input_files.empty()) {
        std::fprintf(stderr, "error: at least one --input file required\n");
        printUsage(argv[0]);
        return 1;
    }

    int K = levels_override;
    if (K <= 0) {
        qrsdp::EventLogReader probe(input_files[0]);
        K = static_cast<int>(probe.header().levels_per_side);
        if (K <= 0) K = 5;
    }
    const size_t ku = static_cast<size_t>(K);

    std::printf("=== qrsdp_calibrate ===\n");
    std::printf("inputs: %zu file(s), K=%d, n_max=%d, output=%s\n",
                input_files.size(), K, n_max, output_file.c_str());

    // Per-(level, side) estimators. Bid and ask each get K estimators.
    // Market orders are recorded into the level-0 estimators as EXECUTE_SELL (bid)
    // and EXECUTE_BUY (ask).
    std::vector<qrsdp::IntensityEstimator> bid_estimators(ku);
    std::vector<qrsdp::IntensityEstimator> ask_estimators(ku);
    for (auto& e : bid_estimators) e.reset();
    for (auto& e : ask_estimators) e.reset();

    uint64_t total_events = 0;
    uint64_t total_sojourns_recorded = 0;

    for (const auto& input_path : input_files) {
        std::printf("  reading %s ...\n", input_path.c_str());

        qrsdp::EventLogReader reader(input_path);
        const qrsdp::FileHeader& hdr = reader.header();
        const int file_K = static_cast<int>(hdr.levels_per_side);
        const int use_K = std::min(K, file_K > 0 ? file_K : K);

        qrsdp::MultiLevelBook book;
        qrsdp::BookSeed bseed{};
        bseed.p0_ticks = hdr.p0_ticks;
        bseed.levels_per_side = hdr.levels_per_side;
        bseed.initial_depth = hdr.initial_depth > 0 ? hdr.initial_depth : 5;
        bseed.initial_spread_ticks = hdr.initial_spread_ticks > 0 ? hdr.initial_spread_ticks : 2;
        book.seed(bseed);

        std::vector<LevelTracker> bid_trackers;
        std::vector<LevelTracker> ask_trackers;
        snapshotLevels(book, 0.0, bid_trackers, ask_trackers);

        auto records = reader.readAll();
        total_events += records.size();

        for (const auto& rec : records) {
            const double t = static_cast<double>(rec.ts_ns) * 1e-9;
            const auto type = static_cast<qrsdp::EventType>(rec.type);

            int level = -1;
            bool is_bid_side = false;

            switch (type) {
                case qrsdp::EventType::ADD_BID:
                case qrsdp::EventType::CANCEL_BID: {
                    is_bid_side = true;
                    level = findBidLevel(book, rec.price_ticks);
                    if (level < 0 && type == qrsdp::EventType::ADD_BID) {
                        // Spread-improving add: treat as new level 0
                        level = 0;
                    }
                    break;
                }
                case qrsdp::EventType::ADD_ASK:
                case qrsdp::EventType::CANCEL_ASK: {
                    is_bid_side = false;
                    level = findAskLevel(book, rec.price_ticks);
                    if (level < 0 && type == qrsdp::EventType::ADD_ASK) {
                        level = 0;
                    }
                    break;
                }
                case qrsdp::EventType::EXECUTE_SELL: {
                    is_bid_side = true;
                    level = 0;
                    break;
                }
                case qrsdp::EventType::EXECUTE_BUY: {
                    is_bid_side = false;
                    level = 0;
                    break;
                }
                default:
                    break;
            }

            if (level >= 0 && level < use_K) {
                const size_t lk = static_cast<size_t>(level);
                auto& tracker = is_bid_side ? bid_trackers[lk] : ask_trackers[lk];
                auto& estimator = is_bid_side ? bid_estimators[lk] : ask_estimators[lk];

                if (tracker.initialized) {
                    const double dt = t - tracker.last_event_time;
                    if (dt > 0.0) {
                        estimator.recordSojourn(tracker.last_depth, dt, type);
                        ++total_sojourns_recorded;
                    }
                }

                tracker.last_event_time = t;
                tracker.last_depth = is_bid_side
                    ? book.bidDepthAtLevel(lk)
                    : book.askDepthAtLevel(lk);
                tracker.initialized = true;
            }

            // Apply the event to the book
            const int32_t prev_bid = book.bestBid().price_ticks;
            const int32_t prev_ask = book.bestAsk().price_ticks;

            qrsdp::SimEvent ev{};
            ev.type = type;
            ev.side = static_cast<qrsdp::Side>(rec.side);
            ev.price_ticks = rec.price_ticks;
            ev.qty = rec.qty;
            ev.order_id = rec.order_id;
            book.apply(ev);

            const int32_t new_bid = book.bestBid().price_ticks;
            const int32_t new_ask = book.bestAsk().price_ticks;
            if (new_bid != prev_bid || new_ask != prev_ask) {
                snapshotLevels(book, t, bid_trackers, ask_trackers);
            } else {
                // Update only the affected level's depth
                if (level >= 0 && level < use_K) {
                    const size_t lk = static_cast<size_t>(level);
                    if (is_bid_side) {
                        bid_trackers[lk].last_depth = book.bidDepthAtLevel(lk);
                    } else {
                        ask_trackers[lk].last_depth = book.askDepthAtLevel(lk);
                    }
                }
            }
        }
    }

    std::printf("  total events: %llu, sojourns recorded: %llu\n",
                (unsigned long long)total_events, (unsigned long long)total_sojourns_recorded);

    // Build HLRParams from estimated curves
    qrsdp::HLRParams params;
    params.K = K;
    params.n_max = n_max;
    params.spread_sensitivity = spread_sens;
    params.lambda_L_bid.resize(ku);
    params.lambda_L_ask.resize(ku);
    params.lambda_C_bid.resize(ku);
    params.lambda_C_ask.resize(ku);

    auto extractCurve = [&](const qrsdp::IntensityEstimator& est, qrsdp::EventType type,
                            int n_table) -> qrsdp::IntensityCurve {
        std::vector<double> values;
        values.reserve(static_cast<size_t>(n_table + 1));
        for (int n = 0; n <= n_table; ++n) {
            values.push_back(est.lambdaType(static_cast<uint32_t>(n), type));
        }
        qrsdp::IntensityCurve curve;
        curve.setTable(std::move(values), qrsdp::IntensityCurve::TailRule::FLAT);
        return curve;
    };

    for (int i = 0; i < K; ++i) {
        const size_t si = static_cast<size_t>(i);
        params.lambda_L_bid[si] = extractCurve(bid_estimators[si], qrsdp::EventType::ADD_BID, n_max);
        params.lambda_L_ask[si] = extractCurve(ask_estimators[si], qrsdp::EventType::ADD_ASK, n_max);
        params.lambda_C_bid[si] = extractCurve(bid_estimators[si], qrsdp::EventType::CANCEL_BID, n_max);
        params.lambda_C_ask[si] = extractCurve(ask_estimators[si], qrsdp::EventType::CANCEL_ASK, n_max);
    }

    params.lambda_M_buy = extractCurve(ask_estimators[0], qrsdp::EventType::EXECUTE_BUY, n_max);
    params.lambda_M_sell = extractCurve(bid_estimators[0], qrsdp::EventType::EXECUTE_SELL, n_max);

    if (verbose) {
        std::printf("\n--- Estimated curves ---\n");
        for (int i = 0; i < K; ++i) {
            const size_t si = static_cast<size_t>(i);
            auto maxObs = [](const qrsdp::IntensityEstimator& e) {
                return static_cast<int>(e.nMaxObserved());
            };
            std::printf("  Level %d (bid): nmax=%d\n", i, maxObs(bid_estimators[si]));
            std::printf("  Level %d (ask): nmax=%d\n", i, maxObs(ask_estimators[si]));

            auto printSample = [](const char* label, const qrsdp::IntensityCurve& c) {
                std::printf("    %s: n=0:%.2f n=1:%.2f n=5:%.2f n=10:%.2f n=20:%.2f n=50:%.2f\n",
                            label, c.value(0), c.value(1), c.value(5),
                            c.value(10), c.value(20), c.value(50));
            };
            printSample("L_bid", params.lambda_L_bid[si]);
            printSample("L_ask", params.lambda_L_ask[si]);
            printSample("C_bid", params.lambda_C_bid[si]);
            printSample("C_ask", params.lambda_C_ask[si]);
        }
        std::printf("  Market buy:  ");
        for (int n : {0, 1, 5, 10, 20, 50})
            std::printf("n=%d:%.2f ", n, params.lambda_M_buy.value(static_cast<size_t>(n)));
        std::printf("\n");
        std::printf("  Market sell: ");
        for (int n : {0, 1, 5, 10, 20, 50})
            std::printf("n=%d:%.2f ", n, params.lambda_M_sell.value(static_cast<size_t>(n)));
        std::printf("\n");
    }

    if (qrsdp::saveHLRParamsToJson(output_file, params)) {
        std::printf("\nWrote calibrated curves to %s\n", output_file.c_str());
    } else {
        std::fprintf(stderr, "error: failed to write %s\n", output_file.c_str());
        return 1;
    }

    return 0;
}
