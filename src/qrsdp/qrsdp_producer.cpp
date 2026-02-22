#include "qrsdp/qrsdp_producer.h"
#include <cmath>
#include <cstdint>

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
}

bool QrsdpProducer::stepOneEvent(IEventSink& sink) {
    if (t_ >= session_seconds_) return false;
    BookState state;
    state.features = book_->features();
    const Intensities intens = intensityModel_->compute(state);
    const double lambda_total = intens.total();
    const double dt = eventSampler_->sampleDeltaT(lambda_total);
    t_ += dt;
    if (t_ >= session_seconds_) return false;
    const EventType type = eventSampler_->sampleType(intens);
    const EventAttrs attrs = attributeSampler_->sample(type, *book_, state.features);
    SimEvent ev;
    ev.type = type;
    ev.side = attrs.side;
    ev.price_ticks = attrs.price_ticks;
    ev.qty = attrs.qty;
    ev.order_id = order_id_++;
    book_->apply(ev);
    EventRecord rec;
    rec.ts_ns = static_cast<uint64_t>(t_ * 1e9);
    rec.type = static_cast<uint8_t>(type);
    rec.side = static_cast<uint8_t>(attrs.side);
    rec.price_ticks = attrs.price_ticks;
    rec.qty = attrs.qty;
    rec.order_id = ev.order_id;
    rec.flags = 0;
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
