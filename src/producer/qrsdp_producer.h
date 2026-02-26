#pragma once

#include "producer/i_producer.h"
#include "rng/irng.h"
#include "book/i_order_book.h"
#include "model/i_intensity_model.h"
#include "sampler/i_event_sampler.h"
#include "sampler/i_attribute_sampler.h"
#include "core/records.h"

namespace qrsdp {

/// Runs one intraday session: continuous-time loop, append to sink, return close.
/// Also supports stepping: startSession() then stepOneEvent() for UI/debugging.
class QrsdpProducer : public IProducer {
public:
    QrsdpProducer(IRng& rng, IOrderBook& book, IIntensityModel& intensityModel,
                  IEventSampler& eventSampler, IAttributeSampler& attributeSampler);
    SessionResult runSession(const TradingSession&, IEventSink&) override;

    /// Stepping API (non-invasive): call startSession once, then stepOneEvent in a loop.
    void startSession(const TradingSession& session);
    /// Advances one event; appends to sink and returns true. Returns false if past session end.
    bool stepOneEvent(IEventSink& sink);
    double currentTime() const { return t_; }
    uint64_t eventsWrittenThisSession() const { return events_written_; }
    uint64_t shiftCountThisSession() const { return shift_count_; }

private:
    IRng* rng_;
    IOrderBook* book_;
    IIntensityModel* intensityModel_;
    IEventSampler* eventSampler_;
    IAttributeSampler* attributeSampler_;
    double session_seconds_ = 0.0;
    double t_ = 0.0;
    uint64_t order_id_ = 1;
    uint64_t events_written_ = 0;
    uint64_t shift_count_ = 0;
    double theta_reinit_ = 0.0;
    double reinit_mean_ = 10.0;
    uint64_t market_open_ns_ = 0;
};

}  // namespace qrsdp
