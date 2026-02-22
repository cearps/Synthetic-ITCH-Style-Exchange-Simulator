#include "qrsdp/qrsdp_producer.h"
#include "qrsdp/curve_intensity_model.h"
#include <cmath>
#include <cstdint>
#include <vector>

namespace qrsdp {

namespace {

constexpr uint32_t kDefaultInitialDepth = 50;
constexpr uint32_t kDefaultInitialSpreadTicks = 2;

}

QrsdpProducer::QrsdpProducer(IRng& rng, IOrderBook& book, IIntensityModel& intensityModel,
                             IEventSampler& eventSampler, IAttributeSampler& attributeSampler)
    : rng_(&rng), book_(&book), intensityModel_(&intensityModel),
      eventSampler_(&eventSampler), attributeSampler_(&attributeSampler) {}

void QrsdpProducer::startSession(const TradingSession& session) {
    rng_->seed(session.seed);
    BookSeed seed;
    seed.p0_ticks = session.p0_ticks;
    seed.levels_per_side = session.levels_per_side;
    seed.initial_depth = session.initial_depth > 0 ? session.initial_depth : kDefaultInitialDepth;
    seed.initial_spread_ticks = session.initial_spread_ticks > 0 ? session.initial_spread_ticks
                                                                : kDefaultInitialSpreadTicks;
    book_->seed(seed);
    session_seconds_ = static_cast<double>(session.session_seconds);
    t_ = 0.0;
    order_id_ = 1;
    events_written_ = 0;
    shift_count_ = 0;
    theta_reinit_ = session.queue_reactive.theta_reinit;
    reinit_mean_ = session.queue_reactive.reinit_depth_mean > 0.0
                       ? session.queue_reactive.reinit_depth_mean
                       : 10.0;
}

bool QrsdpProducer::stepOneEvent(IEventSink& sink) {
    if (t_ >= session_seconds_) return false;
    BookState state;
    state.features = book_->features();
    const size_t num_levels = book_->numLevels();
    state.bid_depths.reserve(num_levels);
    state.ask_depths.reserve(num_levels);
    for (size_t k = 0; k < num_levels; ++k) {
        state.bid_depths.push_back(book_->bidDepthAtLevel(k));
        state.ask_depths.push_back(book_->askDepthAtLevel(k));
    }
    const Intensities intens = intensityModel_->compute(state);
    const double lambda_total = intens.total();
    const double dt = eventSampler_->sampleDeltaT(lambda_total);
    t_ += dt;
    if (t_ >= session_seconds_) return false;

    EventType type;
    size_t level_hint = kLevelHintNone;
    std::vector<double> per_level;
    if (intensityModel_->getPerLevelIntensities(per_level) && !per_level.empty()) {
        const size_t idx = eventSampler_->sampleIndexFromWeights(per_level);
        const int K = static_cast<int>((per_level.size() - 2) / 4);
        CurveIntensityModel::decodePerLevelIndex(idx, K, type, level_hint);
    } else {
        type = eventSampler_->sampleType(intens);
    }
    const EventAttrs attrs = attributeSampler_->sample(type, *book_, state.features, level_hint);
    SimEvent ev;
    ev.type = type;
    ev.side = attrs.side;
    ev.price_ticks = attrs.price_ticks;
    ev.qty = attrs.qty;
    ev.order_id = order_id_++;
    const int32_t prev_bid = book_->bestBid().price_ticks;
    const int32_t prev_ask = book_->bestAsk().price_ticks;
    book_->apply(ev);
    const int32_t new_bid = book_->bestBid().price_ticks;
    const int32_t new_ask = book_->bestAsk().price_ticks;
    const bool bid_shifted = (new_bid != prev_bid);
    const bool ask_shifted = (new_ask != prev_ask);
    const bool shift_occurred = bid_shifted || ask_shifted;
    bool reinit_happened = false;
    if (shift_occurred) {
        ++shift_count_;
        if (theta_reinit_ > 0.0 && rng_->uniform() < theta_reinit_) {
            book_->reinitialize(*rng_, reinit_mean_);
            reinit_happened = true;
        }
    }
    uint32_t flags = kFlagNone;
    if (new_bid < prev_bid) flags |= kFlagShiftDown;
    if (new_ask > prev_ask) flags |= kFlagShiftUp;
    if (reinit_happened)    flags |= kFlagReinit;
    EventRecord rec;
    rec.ts_ns = static_cast<uint64_t>(t_ * 1e9);
    rec.type = static_cast<uint8_t>(type);
    rec.side = static_cast<uint8_t>(attrs.side);
    rec.price_ticks = attrs.price_ticks;
    rec.qty = attrs.qty;
    rec.order_id = ev.order_id;
    rec.flags = flags;
    sink.append(rec);
    ++events_written_;
    return true;
}

SessionResult QrsdpProducer::runSession(const TradingSession& session, IEventSink& sink) {
    startSession(session);
    while (stepOneEvent(sink)) {}
    const Level bid = book_->bestBid();
    const Level ask = book_->bestAsk();
    const int32_t close_ticks = (bid.price_ticks + ask.price_ticks) / 2;
    return SessionResult{close_ticks, events_written_};
}

}  // namespace qrsdp
